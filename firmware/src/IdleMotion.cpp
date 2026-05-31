#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <Avatar.h>
#include "IdleMotion.h"
#include "Gesture.h"
#include "NightMode.h"
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

using namespace m5avatar;

extern Avatar avatar;
extern volatile uint32_t gesture_suppress_until;   // from Gesture.cpp

#define IDLE_SPIFFS_PATH  "/idlemotion.json"

// Expression order matches expressions_table[] in main.cpp:
//   0 Neutral, 1 Happy, 2 Sleepy, 3 Doubt, 4 Sad, 5 Angry
static const Expression IDLE_EXPR[] = {
    Expression::Neutral, Expression::Happy, Expression::Sleepy,
    Expression::Doubt,   Expression::Sad,   Expression::Angry,
};
#define IDLE_EXPR_COUNT 6

struct IdleConfig {
    bool enabled        = true;
    int  minIntervalSec = 4;     // min seconds between idle actions
    int  maxIntervalSec = 12;    // max seconds between idle actions
    int  quietAfterSec  = 3;     // stay calm this long after speech ends
    int  energy         = 5;     // 1..10, higher = more frequent (scales intervals)
    bool gestureEnabled = true;  // play matching head motion with the expression
    bool gazeWander     = true;  // random on-screen eye gaze drift
    bool blinkEnabled   = true;  // occasional blink
    int  blinkMinSec    = 3;
    int  blinkMaxSec    = 9;
    // weights for [Neutral, Happy, Sleepy, Doubt, Sad, Angry]
    int  exprWeights[IDLE_EXPR_COUNT] = { 5, 6, 2, 3, 1, 1 };
};

static IdleConfig g_cfg;
static portMUX_TYPE g_cfgMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t g_holdUntil = 0;

void idle_motion_hold(uint32_t ms) {
    uint32_t until = millis() + ms;
    if (until > g_holdUntil) g_holdUntil = until;
}

static void clampConfig(IdleConfig& c) {
    if (c.minIntervalSec < 1)  c.minIntervalSec = 1;
    if (c.maxIntervalSec < c.minIntervalSec) c.maxIntervalSec = c.minIntervalSec;
    if (c.maxIntervalSec > 600) c.maxIntervalSec = 600;
    if (c.quietAfterSec < 0)   c.quietAfterSec = 0;
    if (c.energy < 1)  c.energy = 1;
    if (c.energy > 10) c.energy = 10;
    if (c.blinkMinSec < 1) c.blinkMinSec = 1;
    if (c.blinkMaxSec < c.blinkMinSec) c.blinkMaxSec = c.blinkMinSec;
    int wsum = 0;
    for (int i = 0; i < IDLE_EXPR_COUNT; i++) { if (c.exprWeights[i] < 0) c.exprWeights[i] = 0; wsum += c.exprWeights[i]; }
    if (wsum == 0) c.exprWeights[0] = 1;  // never all-zero
}

static void load_from_spiffs() {
    if (!SPIFFS.exists(IDLE_SPIFFS_PATH)) {
        Serial.println("[idle] no idlemotion.json — using defaults");
        return;
    }
    File f = SPIFFS.open(IDLE_SPIFFS_PATH, "r");
    if (!f) return;
    String body = f.readString();
    f.close();
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, body)) {
        Serial.println("[idle] idlemotion.json parse error — using defaults");
        return;
    }
    IdleConfig c;
    c.enabled        = doc["enabled"]        | c.enabled;
    c.minIntervalSec = doc["minIntervalSec"] | c.minIntervalSec;
    c.maxIntervalSec = doc["maxIntervalSec"] | c.maxIntervalSec;
    c.quietAfterSec  = doc["quietAfterSec"]  | c.quietAfterSec;
    c.energy         = doc["energy"]         | c.energy;
    c.gestureEnabled = doc["gestureEnabled"] | c.gestureEnabled;
    c.gazeWander     = doc["gazeWander"]     | c.gazeWander;
    c.blinkEnabled   = doc["blinkEnabled"]   | c.blinkEnabled;
    c.blinkMinSec    = doc["blinkMinSec"]    | c.blinkMinSec;
    c.blinkMaxSec    = doc["blinkMaxSec"]    | c.blinkMaxSec;
    JsonArray w = doc["exprWeights"].as<JsonArray>();
    if (!w.isNull()) {
        for (int i = 0; i < IDLE_EXPR_COUNT && i < (int)w.size(); i++) c.exprWeights[i] = w[i];
    }
    clampConfig(c);
    portENTER_CRITICAL(&g_cfgMux);
    g_cfg = c;
    portEXIT_CRITICAL(&g_cfgMux);
    Serial.println("[idle] loaded config from SPIFFS");
}

static void buildJson(const IdleConfig& c, String& out) {
    DynamicJsonDocument doc(2048);
    doc["enabled"]        = c.enabled;
    doc["minIntervalSec"] = c.minIntervalSec;
    doc["maxIntervalSec"] = c.maxIntervalSec;
    doc["quietAfterSec"]  = c.quietAfterSec;
    doc["energy"]         = c.energy;
    doc["gestureEnabled"] = c.gestureEnabled;
    doc["gazeWander"]     = c.gazeWander;
    doc["blinkEnabled"]   = c.blinkEnabled;
    doc["blinkMinSec"]    = c.blinkMinSec;
    doc["blinkMaxSec"]    = c.blinkMaxSec;
    JsonArray w = doc.createNestedArray("exprWeights");
    for (int i = 0; i < IDLE_EXPR_COUNT; i++) w.add(c.exprWeights[i]);
    serializeJson(doc, out);
}

String idle_motion_get_json() {
    IdleConfig c;
    portENTER_CRITICAL(&g_cfgMux);
    c = g_cfg;
    portEXIT_CRITICAL(&g_cfgMux);
    String out;
    buildJson(c, out);
    return out;
}

bool idle_motion_set_json(const String& json) {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, json)) return false;
    IdleConfig c;
    portENTER_CRITICAL(&g_cfgMux);
    c = g_cfg;
    portEXIT_CRITICAL(&g_cfgMux);
    // apply provided keys over current
    if (doc.containsKey("enabled"))        c.enabled        = doc["enabled"];
    if (doc.containsKey("minIntervalSec")) c.minIntervalSec = doc["minIntervalSec"];
    if (doc.containsKey("maxIntervalSec")) c.maxIntervalSec = doc["maxIntervalSec"];
    if (doc.containsKey("quietAfterSec"))  c.quietAfterSec  = doc["quietAfterSec"];
    if (doc.containsKey("energy"))         c.energy         = doc["energy"];
    if (doc.containsKey("gestureEnabled")) c.gestureEnabled = doc["gestureEnabled"];
    if (doc.containsKey("gazeWander"))     c.gazeWander     = doc["gazeWander"];
    if (doc.containsKey("blinkEnabled"))   c.blinkEnabled   = doc["blinkEnabled"];
    if (doc.containsKey("blinkMinSec"))    c.blinkMinSec    = doc["blinkMinSec"];
    if (doc.containsKey("blinkMaxSec"))    c.blinkMaxSec    = doc["blinkMaxSec"];
    JsonArray w = doc["exprWeights"].as<JsonArray>();
    if (!w.isNull()) {
        for (int i = 0; i < IDLE_EXPR_COUNT && i < (int)w.size(); i++) c.exprWeights[i] = w[i];
    }
    clampConfig(c);
    portENTER_CRITICAL(&g_cfgMux);
    g_cfg = c;
    portEXIT_CRITICAL(&g_cfgMux);

    String out;
    buildJson(c, out);
    File f = SPIFFS.open(IDLE_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[idle] SPIFFS open(w) failed"); return false; }
    f.print(out);
    f.close();
    Serial.println("[idle] saved config to SPIFFS");
    return true;
}

// Pick a random expression index by weight.
static int pickExpr(const IdleConfig& c) {
    int wsum = 0;
    for (int i = 0; i < IDLE_EXPR_COUNT; i++) wsum += c.exprWeights[i];
    if (wsum <= 0) return 0;
    int r = (int)random(wsum);
    for (int i = 0; i < IDLE_EXPR_COUNT; i++) {
        r -= c.exprWeights[i];
        if (r < 0) return i;
    }
    return 0;
}

// Is the robot currently speaking (realtime audio playing)?
static bool is_speaking() {
#if defined(REALTIME_API)
    if (robot && robot->llm) {
        int level = ((RealtimeLLMBase*)(robot->llm))->getAudioLevel();
        return level > 200;
    }
#endif
    return false;
}

static void idle_task(void* arg) {
    uint32_t nextActionAt = millis() + 2000;
    uint32_t nextBlinkAt  = millis() + 3000;
    uint32_t lastSpeakMs  = 0;

    for (;;) {
        delay(250);

        IdleConfig c;
        portENTER_CRITICAL(&g_cfgMux);
        c = g_cfg;
        portEXIT_CRITICAL(&g_cfgMux);

        if (!c.enabled) { lastSpeakMs = millis(); continue; }
        if (robot == nullptr) continue;

        uint32_t now = millis();

        // Yield while speaking / held / a gesture is running.
        if (is_speaking()) { lastSpeakMs = now; continue; }
        if (now < g_holdUntil) continue;
        if (now < gesture_suppress_until) continue;
        if (now - lastSpeakMs < (uint32_t)c.quietAfterSec * 1000UL) continue;

        // energy 1..10 scales interval: energy 10 ≈ ×0.5, energy 1 ≈ ×1.5
        float scale = 1.5f - (c.energy - 1) * (1.0f / 9.0f);
        if (scale < 0.2f) scale = 0.2f;

        // --- Blink (cheap, frequent) ---
        if (c.blinkEnabled && now >= nextBlinkAt) {
            avatar.setEyeOpenRatio(0.0f);
            delay(120);
            avatar.setEyeOpenRatio(1.0f);
            int span = c.blinkMaxSec - c.blinkMinSec;
            nextBlinkAt = now + (uint32_t)((c.blinkMinSec + (span > 0 ? (int)random(span) : 0)) * 1000UL);
        }

        // --- Main idle action ---
        if (now >= nextActionAt) {
            bool night = night_mode_is_night();
            int idx = pickExpr(c);
            // At night, mostly look sleepy and move less.
            if (night && random(100) < 70) idx = 2;  // Sleepy
            avatar.setExpression(IDLE_EXPR[idx]);

            if (c.gazeWander) {
                float gx = (random(201) - 100) / 100.0f;  // -1.0 .. 1.0
                float gy = (random(201) - 100) / 100.0f;
                avatar.setGaze(gy, gx);
            }
            if (c.gestureEnabled) {
                gesture_play(IDLE_EXPR[idx]);
            }

            int span = c.maxIntervalSec - c.minIntervalSec;
            uint32_t base = c.minIntervalSec + (span > 0 ? (uint32_t)random(span) : 0);
            uint32_t interval = (uint32_t)(base * 1000UL * scale);
            if (night) interval *= 2;     // calmer at night
            if (interval < 800) interval = 800;
            nextActionAt = now + interval;
        }
    }
}

void idle_motion_init() {
    load_from_spiffs();
    xTaskCreatePinnedToCore(idle_task, "idle_motion", 4096, NULL, 1, NULL, APP_CPU_NUM);
    Serial.println("[idle] task started");
}
