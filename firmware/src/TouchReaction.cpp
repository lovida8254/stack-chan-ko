#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "TouchReaction.h"
#include "IdleMotion.h"
#include "IdleTalk.h"
#include "PetReaction.h"
#include "CameraVision.h"

using namespace m5avatar;
extern Avatar avatar;

#define TOUCH_SPIFFS_PATH  "/touch.json"

// Config is only touched from the loop task — no mutex needed.
static bool g_lookEnabled    = true;
static bool g_strokeEnabled  = true;
static int  g_strokeThreshold = 120;   // accumulated drag pixels to count as a stroke

static void load_from_spiffs() {
    if (!SPIFFS.exists(TOUCH_SPIFFS_PATH)) {
        Serial.println("[touch] no touch.json — using defaults");
        return;
    }
    File f = SPIFFS.open(TOUCH_SPIFFS_PATH, "r");
    if (!f) return;
    String body = f.readString();
    f.close();
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body)) { Serial.println("[touch] parse error — defaults"); return; }
    g_lookEnabled     = doc["lookEnabled"]     | g_lookEnabled;
    g_strokeEnabled   = doc["strokeEnabled"]   | g_strokeEnabled;
    g_strokeThreshold = doc["strokeThreshold"] | g_strokeThreshold;
    if (g_strokeThreshold < 20) g_strokeThreshold = 20;
    Serial.println("[touch] loaded config from SPIFFS");
}

String touch_reaction_get_json() {
    DynamicJsonDocument doc(512);
    doc["lookEnabled"]     = g_lookEnabled;
    doc["strokeEnabled"]   = g_strokeEnabled;
    doc["strokeThreshold"] = g_strokeThreshold;
    String out; serializeJson(doc, out); return out;
}

bool touch_reaction_set_json(const String& json) {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, json)) return false;
    if (doc.containsKey("lookEnabled"))     g_lookEnabled     = doc["lookEnabled"];
    if (doc.containsKey("strokeEnabled"))   g_strokeEnabled   = doc["strokeEnabled"];
    if (doc.containsKey("strokeThreshold")) g_strokeThreshold = doc["strokeThreshold"];
    if (g_strokeThreshold < 20) g_strokeThreshold = 20;
    File f = SPIFFS.open(TOUCH_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[touch] SPIFFS open(w) failed"); return false; }
    f.print(touch_reaction_get_json());
    f.close();
    Serial.println("[touch] saved config to SPIFFS");
    return true;
}

extern volatile bool g_inAiMod;     // defined in RealtimeAiMod.cpp

void touch_reaction_tick() {
#if defined(ARDUINO_M5STACK_Core2) || defined(ARDUINO_M5STACK_CORES3)
    if (!g_inAiMod) return;         // 쓰담/시선은 AI 대화 모드에서만 — 포토프레임 등 스와이프 오인 방지
    if (camera_is_busy()) return;   // camera owns the internal I2C during capture
    static int strokeAccum = 0;

    auto count = M5.Touch.getCount();
    if (!count) return;
    auto t = M5.Touch.getDetail();
    int W = M5.Display.width();
    int H = M5.Display.height();
    if (W <= 0 || H <= 0) return;

    if (t.wasPressed()) strokeAccum = 0;

    if (t.isPressed()) {
        if (g_lookEnabled) {
            float h = ((float)t.x / (W * 0.5f)) - 1.0f;   // -1 (left) .. 1 (right)
            float v = ((float)t.y / (H * 0.5f)) - 1.0f;   // -1 (top)  .. 1 (bottom)
            if (h < -1) h = -1; if (h > 1) h = 1;
            if (v < -1) v = -1; if (v > 1) v = 1;
            avatar.setGaze(v, h);
            idle_motion_hold(1000);     // don't let idle yank the gaze away mid-touch
        }
        if (g_strokeEnabled) strokeAccum += abs(t.deltaX()) + abs(t.deltaY());
    }

    if (t.wasReleased()) {
        // A real stroke = enough accumulated drag, and NOT a quick flick (which
        // the mod uses to switch screens) — so we don't fight that gesture.
        if (g_strokeEnabled && !t.wasFlicked() && strokeAccum > g_strokeThreshold) {
            Serial.printf("[touch] stroke (%dpx) -> pet\n", strokeAccum);
            idle_talk_note_activity();
            pet_reaction_fire();
        }
        strokeAccum = 0;
    }
#endif
}

void touch_reaction_init() {
    load_from_spiffs();
    Serial.println("[touch] ready (polled from loop)");
}
