#include <Arduino.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include "NightMode.h"
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

extern bool isOffline;

#define NIGHT_SPIFFS_PATH  "/nightmode.json"

struct NightConfig {
    bool enabled        = true;
    int  startHour      = 22;    // night begins (0..23)
    int  endHour        = 7;     // night ends   (0..23); wraps past midnight
    int  dayBrightness  = 180;   // 0..255 (bright but not max)
    int  nightBrightness = 25;
    bool greetEnabled   = true;
    bool sleepyBias     = true;  // bias idle motion toward Sleepy at night
    String greetPrompt  = "이제 밤이 깊었어. 가족에게 졸린 목소리로 '이제 잘 시간이야, 잘 자~' 하고 다정하게 인사해줘.";
};

static NightConfig g_cfg;
static SemaphoreHandle_t g_mux = NULL;
static volatile bool g_isNight = false;
// Manual override (voice "잘자" → sleep now / "일어나" → back to schedule).
static volatile bool g_forceActive = false;   // true = ignore clock, use g_forceSleep
static volatile bool g_forceSleep  = false;

static void lock()   { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); }
static void unlock() { if (g_mux) xSemaphoreGive(g_mux); }

// Force sleep (dim + sleepy) now, or release back to the time schedule.
void night_mode_force_sleep(bool sleep) {
    g_forceActive = sleep;          // sleeping → override on; waking → back to auto schedule
    g_forceSleep  = sleep;
    NightConfig c; lock(); c = g_cfg; unlock();
    if (sleep) {
        g_isNight = true;
        M5.Display.setBrightness(c.nightBrightness);   // apply immediately (don't wait for 10s tick)
        Serial.println("[night] forced sleep (voice)");
    } else {
        Serial.println("[night] force released → schedule");
    }
}

static void clampConfig(NightConfig& c) {
    if (c.startHour < 0) c.startHour = 0; if (c.startHour > 23) c.startHour = 23;
    if (c.endHour   < 0) c.endHour   = 0; if (c.endHour   > 23) c.endHour   = 23;
    if (c.dayBrightness   < 1)   c.dayBrightness = 1;   if (c.dayBrightness   > 255) c.dayBrightness = 255;
    if (c.nightBrightness < 0)   c.nightBrightness = 0; if (c.nightBrightness > 255) c.nightBrightness = 255;
}

static bool hour_in_night(int h, int start, int end) {
    if (start == end) return false;
    if (start < end)  return (h >= start && h < end);
    return (h >= start || h < end);   // wraps past midnight
}

bool night_mode_is_night() {
    bool sleepy;
    lock(); sleepy = g_cfg.sleepyBias; unlock();
    return g_isNight && sleepy;
}

static void load_from_spiffs() {
    NightConfig c;
    if (SPIFFS.exists(NIGHT_SPIFFS_PATH)) {
        File f = SPIFFS.open(NIGHT_SPIFFS_PATH, "r");
        if (f) {
            String body = f.readString();
            f.close();
            DynamicJsonDocument doc(1024);
            if (!deserializeJson(doc, body)) {
                c.enabled         = doc["enabled"]         | c.enabled;
                c.startHour       = doc["startHour"]       | c.startHour;
                c.endHour         = doc["endHour"]         | c.endHour;
                c.dayBrightness   = doc["dayBrightness"]   | c.dayBrightness;
                c.nightBrightness = doc["nightBrightness"] | c.nightBrightness;
                c.greetEnabled    = doc["greetEnabled"]    | c.greetEnabled;
                c.sleepyBias      = doc["sleepyBias"]      | c.sleepyBias;
                const char* gp = doc["greetPrompt"] | "";
                if (gp && *gp) c.greetPrompt = String(gp);
            }
        }
    } else {
        Serial.println("[night] no nightmode.json — using defaults");
    }
    clampConfig(c);
    lock(); g_cfg = c; unlock();
}

static void buildJson(const NightConfig& c, String& out) {
    DynamicJsonDocument doc(1024);
    doc["enabled"]         = c.enabled;
    doc["startHour"]       = c.startHour;
    doc["endHour"]         = c.endHour;
    doc["dayBrightness"]   = c.dayBrightness;
    doc["nightBrightness"] = c.nightBrightness;
    doc["greetEnabled"]    = c.greetEnabled;
    doc["sleepyBias"]      = c.sleepyBias;
    doc["greetPrompt"]     = c.greetPrompt;
    serializeJson(doc, out);
}

String night_mode_get_json() {
    String out;
    lock(); buildJson(g_cfg, out); unlock();
    return out;
}

bool night_mode_set_json(const String& json) {
    DynamicJsonDocument doc(1024);
    if (deserializeJson(doc, json)) return false;
    NightConfig c;
    lock(); c = g_cfg; unlock();
    if (doc.containsKey("enabled"))         c.enabled         = doc["enabled"];
    if (doc.containsKey("startHour"))       c.startHour       = doc["startHour"];
    if (doc.containsKey("endHour"))         c.endHour         = doc["endHour"];
    if (doc.containsKey("dayBrightness"))   c.dayBrightness   = doc["dayBrightness"];
    if (doc.containsKey("nightBrightness")) c.nightBrightness = doc["nightBrightness"];
    if (doc.containsKey("greetEnabled"))    c.greetEnabled    = doc["greetEnabled"];
    if (doc.containsKey("sleepyBias"))      c.sleepyBias      = doc["sleepyBias"];
    if (doc.containsKey("greetPrompt"))     { const char* gp = doc["greetPrompt"] | ""; c.greetPrompt = String(gp); }
    clampConfig(c);
    String out;
    buildJson(c, out);
    File f = SPIFFS.open(NIGHT_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[night] SPIFFS open(w) failed"); return false; }
    f.print(out);
    f.close();
    lock(); g_cfg = c; unlock();
    Serial.println("[night] saved config to SPIFFS");
    return true;
}

void night_mode_tick() {
    static uint32_t lastCheckMs = 0;
    static int  appliedBrightness = -1;
    static bool prevNight = false;
    static bool firstValidDone = false;   // have we seen at least one valid clock reading?

    uint32_t now = millis();
    if (now - lastCheckMs < 10000 && lastCheckMs != 0) return;   // every 10s (first call runs immediately)
    lastCheckMs = now;

    NightConfig c;
    lock(); c = g_cfg; unlock();

    // 음성 "잘자"/"일어나" 강제 모드는 스케줄(enabled)과 무관하게 항상 우선.
    // (예전엔 enabled=false 시 아래에서 g_isNight=false로 덮어써 force_sleep이 10초 내 풀렸음)
    if (g_forceActive) {
        g_isNight = g_forceSleep;
        int wantB = g_forceSleep ? c.nightBrightness : c.dayBrightness;
        if (wantB != appliedBrightness) { M5.Display.setBrightness(wantB); appliedBrightness = wantB; }
        return;
    }

    if (!c.enabled) {
        if (appliedBrightness != c.dayBrightness) { M5.Display.setBrightness(c.dayBrightness); appliedBrightness = c.dayBrightness; }
        g_isNight = false;
        return;
    }

    bool night;
    if (g_forceActive) {
        night = g_forceSleep;            // manual override (voice "잘자"/"일어나") — ignore clock
    } else {
        struct tm tnow;
        if (!getLocalTime(&tnow)) return;   // no NTP yet — wait, don't touch state
        night = hour_in_night(tnow.tm_hour, c.startHour, c.endHour);
    }
    g_isNight = night;

    int wantBrightness = night ? c.nightBrightness : c.dayBrightness;
    if (wantBrightness != appliedBrightness) {
        M5.Display.setBrightness(wantBrightness);
        appliedBrightness = wantBrightness;
        Serial.printf("[night] brightness -> %d (night=%d)\n", wantBrightness, (int)night);
    }

    // One-shot bedtime greeting on a real day->night transition. Skip the very
    // first valid reading so booting up during the night doesn't greet on boot.
    if (firstValidDone && night && !prevNight && c.greetEnabled && !isOffline && !g_forceActive) {
#if defined(REALTIME_API)
        if (robot && robot->llm && c.greetPrompt.length()) {
            ((RealtimeLLMBase*)(robot->llm))->pushUserText(c.greetPrompt);
            Serial.println("[night] bedtime greeting sent");
        }
#endif
    }
    prevNight = night;
    firstValidDone = true;
}

void night_mode_init() {
    if (g_mux == NULL) g_mux = xSemaphoreCreateMutex();
    load_from_spiffs();
    Serial.println("[night] ready (polled from loop)");
}
