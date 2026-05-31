#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "BatteryReaction.h"
#include "Sfx.h"          // 충전/배터리부족 효과음 이벤트(음성토글과 무관)
#include "Gesture.h"
#include "IdleMotion.h"
#include "IdleTalk.h"
#include "CameraVision.h"
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

using namespace m5avatar;
extern Avatar avatar;
extern bool isOffline;

#define BATT_SPIFFS_PATH  "/battery.json"

// loop-thread-only config (no mutex).
static bool   g_enabled        = true;
static int    g_lowThreshold   = 20;     // percent
static bool   g_lowReact       = true;
static int    g_lowIntervalMin = 15;
static bool   g_chargeReact    = true;
static String g_lowPrompt    = "지금 배터리가 얼마 안 남았어. 배고픈 듯이 '배고파~ 충전 좀 해줘' 하고 귀엽게 말해줘. 짧게.";
static String g_chargePrompt = "방금 충전기에 연결됐어. 밥 먹는 강아지처럼 '냠냠, 잘 먹겠습니다!' 하고 기분 좋게 짧게 말해줘.";

static void clampCfg() {
    if (g_lowThreshold < 1) g_lowThreshold = 1;
    if (g_lowThreshold > 99) g_lowThreshold = 99;
    if (g_lowIntervalMin < 1) g_lowIntervalMin = 1;
}

static bool is_speaking() {
#if defined(REALTIME_API)
    if (robot && robot->llm) return ((RealtimeLLMBase*)(robot->llm))->getAudioLevel() > 200;
#endif
    return false;
}

static bool speak(const String& p) {
#if defined(REALTIME_API)
    if (isOffline || robot == nullptr || robot->llm == nullptr) return false;
    ((RealtimeLLMBase*)(robot->llm))->pushUserText(p);
    return true;
#else
    return false;
#endif
}

static void load_from_spiffs() {
    if (!SPIFFS.exists(BATT_SPIFFS_PATH)) { Serial.println("[batt] no battery.json — defaults"); return; }
    File f = SPIFFS.open(BATT_SPIFFS_PATH, "r");
    if (!f) return;
    String body = f.readString(); f.close();
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, body)) { Serial.println("[batt] parse error — defaults"); return; }
    g_enabled        = doc["enabled"]        | g_enabled;
    g_lowThreshold   = doc["lowThreshold"]   | g_lowThreshold;
    g_lowReact       = doc["lowReact"]       | g_lowReact;
    g_lowIntervalMin = doc["lowIntervalMin"] | g_lowIntervalMin;
    g_chargeReact    = doc["chargeReact"]    | g_chargeReact;
    { const char* p = doc["lowPrompt"]    | ""; if (p && *p) g_lowPrompt = String(p); }
    { const char* p = doc["chargePrompt"] | ""; if (p && *p) g_chargePrompt = String(p); }
    clampCfg();
    Serial.println("[batt] loaded config from SPIFFS");
}

String battery_reaction_get_json() {
    DynamicJsonDocument doc(1024);
    doc["enabled"]        = g_enabled;
    doc["lowThreshold"]   = g_lowThreshold;
    doc["lowReact"]       = g_lowReact;
    doc["lowIntervalMin"] = g_lowIntervalMin;
    doc["chargeReact"]    = g_chargeReact;
    doc["lowPrompt"]      = g_lowPrompt;
    doc["chargePrompt"]   = g_chargePrompt;
    doc["level"]          = (int)M5.Power.getBatteryLevel();   // live (for UI)
    doc["charging"]       = M5.Power.isCharging();
    String out; serializeJson(doc, out); return out;
}

bool battery_reaction_set_json(const String& json) {
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, json)) return false;
    if (doc.containsKey("enabled"))        g_enabled        = doc["enabled"];
    if (doc.containsKey("lowThreshold"))   g_lowThreshold   = doc["lowThreshold"];
    if (doc.containsKey("lowReact"))       g_lowReact       = doc["lowReact"];
    if (doc.containsKey("lowIntervalMin")) g_lowIntervalMin = doc["lowIntervalMin"];
    if (doc.containsKey("chargeReact"))    g_chargeReact    = doc["chargeReact"];
    if (doc.containsKey("lowPrompt"))      { const char* p = doc["lowPrompt"]    | ""; g_lowPrompt = String(p); }
    if (doc.containsKey("chargePrompt"))   { const char* p = doc["chargePrompt"] | ""; g_chargePrompt = String(p); }
    clampCfg();
    File f = SPIFFS.open(BATT_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[batt] SPIFFS open(w) failed"); return false; }
    f.print(battery_reaction_get_json());
    f.close();
    Serial.println("[batt] saved config to SPIFFS");
    return true;
}

void battery_reaction_tick() {
    static uint32_t lastCheckMs = 0;
    static uint32_t lastLowMs = 0;
    static int  prevCharging = -1;   // -1 = unknown

    uint32_t now = millis();
    if (now - lastCheckMs < 30000 && lastCheckMs != 0) return;   // every 30s
    lastCheckMs = now;

    if (!g_enabled) return;
    if (camera_is_busy()) return;   // camera owns the internal I2C (AXP) during capture

    int level = (int)M5.Power.getBatteryLevel();
    bool charging = M5.Power.isCharging();

    // Charging just started (edge). 효과음은 음성반응 토글과 무관하게 발동(매핑 시).
    if (prevCharging == 0 && charging) {
        sfx_play_event("charge");
        if (g_chargeReact && !is_speaking()) {
            avatar.setExpression(Expression::Happy);
            gesture_play(Expression::Happy);
            idle_motion_hold(2500);
            idle_talk_note_activity();
            speak(g_chargePrompt);
            Serial.println("[batt] charge-start reaction");
        }
    }
    prevCharging = charging ? 1 : 0;

    // Low battery (not charging) -> 효과음 "low"(매핑 시) + (옵션)음성 "배고파". 간격 제한 공유.
    if (!charging && level > 0 && level <= g_lowThreshold) {
        if (now - lastLowMs >= (uint32_t)g_lowIntervalMin * 60000UL) {
            lastLowMs = now;
            sfx_play_event("low");
            if (g_lowReact && !is_speaking()) {
                avatar.setExpression(Expression::Sad);
                gesture_play(Expression::Sad);
                idle_motion_hold(2500);
                speak(g_lowPrompt);
                Serial.printf("[batt] low reaction (level=%d)\n", level);
            }
        }
    }
}

void battery_reaction_init() {
    load_from_spiffs();
    Serial.println("[batt] ready (polled from loop)");
}
