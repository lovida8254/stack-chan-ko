#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "IdleTalk.h"
#include "Robot.h"
#include "Sfx.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

extern bool isOffline;   // main.cpp

#define TALK_SPIFFS_PATH  "/idletalk.json"

struct TalkConfig {
    bool enabled         = true;
    int  minQuietSec     = 180;   // 3 min
    int  maxQuietSec     = 600;   // 10 min
    bool approachEnabled = true;
    int  approachQuietSec = 30;   // must be quiet this long before greeting on approach
    std::vector<String> prompts;
    std::vector<String> approachPrompts;
};

static TalkConfig g_cfg;
static SemaphoreHandle_t g_mux = NULL;
static volatile uint32_t g_lastActivityMs = 0;
static volatile uint32_t g_quietTargetMs  = 0;   // current randomized quiet threshold
static volatile uint32_t g_lastApproachMs = 0;

static void lock()   { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); }
static void unlock() { if (g_mux) xSemaphoreGive(g_mux); }

static void set_defaults(TalkConfig& c) {
    // These are injected as user-role messages, so the model RESPONDS to them
    // (it doesn't read them aloud). Keep them as short, natural directives and
    // avoid the word "표정"/action narration so replies stay natural speech only.
    c.prompts.clear();
    c.prompts.push_back("심심하다~ 가족 중에 누가 있으면 '뭐 해?' 하고 가볍게 먼저 말 걸어줘. 한 문장으로 짧게.");
    c.prompts.push_back("문득 생각났다는 듯이 짧고 실없는 농담 하나만 툭 던져줘. 한 문장.");
    c.prompts.push_back("오늘 하루 어땠는지 가볍고 다정하게 한 문장으로 물어봐줘.");
    c.prompts.push_back("엉뚱한 질문 하나 해줘. 예를 들면 '치킨이랑 피자 중 뭐가 더 좋아?' 같은 거. 짧게.");
    c.prompts.push_back("혼잣말하듯이 귀여운 한마디를 툭 해줘. 너무 길지 않게.");
    c.approachPrompts.clear();
    c.approachPrompts.push_back("누가 가까이 다가왔어. 반갑게 '어, 왔구나!' 하고 한마디로 인사해줘.");
    c.approachPrompts.push_back("가까이 온 사람에게 '안녕! 나 보고 싶었어?' 하고 장난스럽게 짧게 말해줘.");
}

static void clampConfig(TalkConfig& c) {
    if (c.minQuietSec < 10) c.minQuietSec = 10;
    if (c.maxQuietSec < c.minQuietSec) c.maxQuietSec = c.minQuietSec;
    if (c.maxQuietSec > 7200) c.maxQuietSec = 7200;
    if (c.approachQuietSec < 0) c.approachQuietSec = 0;
}

static uint32_t pick_quiet_target(const TalkConfig& c) {
    int span = c.maxQuietSec - c.minQuietSec;
    uint32_t sec = c.minQuietSec + (span > 0 ? (uint32_t)random(span) : 0);
    return sec * 1000UL;
}

static bool is_speaking() {
#if defined(REALTIME_API)
    if (robot && robot->llm) return ((RealtimeLLMBase*)(robot->llm))->getAudioLevel() > 200;
#endif
    return false;
}

static bool speak(const String& prompt) {
#if defined(REALTIME_API)
    if (isOffline || robot == nullptr || robot->llm == nullptr) return false;
    ((RealtimeLLMBase*)(robot->llm))->pushUserText(prompt);
    Serial.printf("[talk] proactive: %s\n", prompt.c_str());
    return true;
#else
    return false;
#endif
}

void idle_talk_note_activity() { g_lastActivityMs = millis(); }

void idle_talk_on_approach() {
    bool enabled, approach; int approachQuietSec; String prompt;
    lock();
    enabled  = g_cfg.enabled;
    approach = g_cfg.approachEnabled;
    approachQuietSec = g_cfg.approachQuietSec;
    if (!g_cfg.approachPrompts.empty())
        prompt = g_cfg.approachPrompts[random(g_cfg.approachPrompts.size())];
    unlock();

    if (!enabled || !approach) return;
    // Approach sound effect (local; independent of online speech). Own 15s cooldown.
    { static uint32_t lastSfxMs = 0; uint32_t n = millis();
      if (n - lastSfxMs > 15000 && sfx_play_event("approach")) lastSfxMs = n; }
    if (prompt.length() == 0) return;
    uint32_t now = millis();
    if (now - g_lastActivityMs < (uint32_t)approachQuietSec * 1000UL) return;  // too soon after activity
    if (now - g_lastApproachMs < 20000) return;                                // cooldown
    if (is_speaking()) return;
    if (speak(prompt)) { g_lastApproachMs = now; g_lastActivityMs = now; }
}

static void load_from_spiffs() {
    TalkConfig c;
    set_defaults(c);
    if (SPIFFS.exists(TALK_SPIFFS_PATH)) {
        File f = SPIFFS.open(TALK_SPIFFS_PATH, "r");
        if (f) {
            String body = f.readString();
            f.close();
            DynamicJsonDocument doc(4096);
            if (!deserializeJson(doc, body)) {
                c.enabled          = doc["enabled"]          | c.enabled;
                c.minQuietSec      = doc["minQuietSec"]      | c.minQuietSec;
                c.maxQuietSec      = doc["maxQuietSec"]      | c.maxQuietSec;
                c.approachEnabled  = doc["approachEnabled"]  | c.approachEnabled;
                c.approachQuietSec = doc["approachQuietSec"] | c.approachQuietSec;
                JsonArray pa = doc["prompts"].as<JsonArray>();
                if (!pa.isNull()) {
                    c.prompts.clear();
                    for (JsonVariant v : pa) { String s = v.as<String>(); s.trim(); if (s.length()) c.prompts.push_back(s); }
                }
                JsonArray ap = doc["approachPrompts"].as<JsonArray>();
                if (!ap.isNull()) {
                    c.approachPrompts.clear();
                    for (JsonVariant v : ap) { String s = v.as<String>(); s.trim(); if (s.length()) c.approachPrompts.push_back(s); }
                }
            }
        }
    } else {
        Serial.println("[talk] no idletalk.json — using defaults");
    }
    clampConfig(c);
    lock();
    g_cfg = c;
    unlock();
}

static void buildJson(const TalkConfig& c, String& out) {
    DynamicJsonDocument doc(4096);
    doc["enabled"]          = c.enabled;
    doc["minQuietSec"]      = c.minQuietSec;
    doc["maxQuietSec"]      = c.maxQuietSec;
    doc["approachEnabled"]  = c.approachEnabled;
    doc["approachQuietSec"] = c.approachQuietSec;
    JsonArray pa = doc.createNestedArray("prompts");
    for (auto& s : c.prompts) pa.add(s);
    JsonArray ap = doc.createNestedArray("approachPrompts");
    for (auto& s : c.approachPrompts) ap.add(s);
    serializeJson(doc, out);
}

String idle_talk_get_json() {
    String out;
    lock();
    buildJson(g_cfg, out);
    unlock();
    return out;
}

bool idle_talk_set_json(const String& json) {
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, json)) return false;
    TalkConfig c;
    lock();
    c = g_cfg;
    unlock();
    if (doc.containsKey("enabled"))          c.enabled          = doc["enabled"];
    if (doc.containsKey("minQuietSec"))      c.minQuietSec      = doc["minQuietSec"];
    if (doc.containsKey("maxQuietSec"))      c.maxQuietSec      = doc["maxQuietSec"];
    if (doc.containsKey("approachEnabled"))  c.approachEnabled  = doc["approachEnabled"];
    if (doc.containsKey("approachQuietSec")) c.approachQuietSec = doc["approachQuietSec"];
    JsonArray pa = doc["prompts"].as<JsonArray>();
    if (!pa.isNull()) {
        c.prompts.clear();
        for (JsonVariant v : pa) { String s = v.as<String>(); s.trim(); if (s.length()) c.prompts.push_back(s); }
    }
    JsonArray ap = doc["approachPrompts"].as<JsonArray>();
    if (!ap.isNull()) {
        c.approachPrompts.clear();
        for (JsonVariant v : ap) { String s = v.as<String>(); s.trim(); if (s.length()) c.approachPrompts.push_back(s); }
    }
    clampConfig(c);

    String out;
    buildJson(c, out);
    File f = SPIFFS.open(TALK_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[talk] SPIFFS open(w) failed"); return false; }
    f.print(out);
    f.close();

    lock();
    g_cfg = c;
    g_quietTargetMs = pick_quiet_target(c);
    unlock();
    Serial.println("[talk] saved config to SPIFFS");
    return true;
}

// Polled from loop() (NOT a task) so pushUserText / webSocket.sendTXT is issued
// from the same main-loop context the scheduler and /speak_now web handler use —
// avoids adding a second task that writes the WebSocket concurrently.
extern volatile bool g_inAiMod;   // defined in RealtimeAiMod.cpp

void idle_talk_tick() {
    if (!g_inAiMod) return;        // 먼저 말 걸기는 AI 대화 모드에서만 (포토프레임 등에선 조용히)
    static uint32_t lastTickMs = 0;
    uint32_t now = millis();
    if (now - lastTickMs < 1000) return;   // 1 Hz is plenty for minute-scale timing
    lastTickMs = now;

    bool enabled; uint32_t target; String prompt;
    lock();
    enabled = g_cfg.enabled;
    if (g_quietTargetMs == 0) g_quietTargetMs = pick_quiet_target(g_cfg);
    target = g_quietTargetMs;
    unlock();

    if (!enabled)      { g_lastActivityMs = now; return; }
    if (isOffline)     { g_lastActivityMs = now; return; }
    if (is_speaking()) { g_lastActivityMs = now; return; }
    if (now - g_lastActivityMs < target) return;

    // Time to speak — pick a random prompt.
    lock();
    if (!g_cfg.prompts.empty())
        prompt = g_cfg.prompts[random(g_cfg.prompts.size())];
    unlock();

    if (prompt.length() && speak(prompt)) {
        g_lastActivityMs = now;
        lock();
        g_quietTargetMs = pick_quiet_target(g_cfg);
        unlock();
    }
}

void idle_talk_init() {
    if (g_mux == NULL) g_mux = xSemaphoreCreateMutex();
    load_from_spiffs();
    g_lastActivityMs = millis();
    Serial.println("[talk] ready (polled from loop)");
}
