#include <Arduino.h>
#include <math.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "PetReaction.h"
#include "Gesture.h"
#include "IdleMotion.h"
#include "IdleTalk.h"
#include "CameraVision.h"
#include "Robot.h"
#include "Sfx.h"

extern volatile uint32_t gesture_suppress_until;  // Gesture.cpp — servo is moving the head
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

using namespace m5avatar;
extern Avatar avatar;
extern bool isOffline;

#define PET_SPIFFS_PATH  "/petreaction.json"

struct PetConfig {
    bool enabled    = true;
    int  sensitivity = 5;   // 1..10 (higher = easier to trigger)
    bool speakOnPet = true;
    std::vector<String> prompts;
};

static PetConfig g_cfg;
static SemaphoreHandle_t g_mux = NULL;
static volatile bool g_detected = false;

static void lock()   { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); }
static void unlock() { if (g_mux) xSemaphoreGive(g_mux); }

static void set_defaults(PetConfig& c) {
    c.prompts.clear();
    c.prompts.push_back("쓰다듬어줘서 기분 좋아! '헤헤' 하고 행복하게 반응해줘. 아주 짧게.");
    c.prompts.push_back("간지러워~ 하면서 좋아하는 한마디 해줘. 짧게.");
    c.prompts.push_back("관심 받아서 신난 강아지처럼 짧게 좋아해줘.");
}

static void clampConfig(PetConfig& c) {
    if (c.sensitivity < 1) c.sensitivity = 1;
    if (c.sensitivity > 10) c.sensitivity = 10;
}

static bool is_speaking() {
#if defined(REALTIME_API)
    if (robot && robot->llm) return ((RealtimeLLMBase*)(robot->llm))->getAudioLevel() > 200;
#endif
    return false;
}

static void load_from_spiffs() {
    PetConfig c;
    set_defaults(c);
    if (SPIFFS.exists(PET_SPIFFS_PATH)) {
        File f = SPIFFS.open(PET_SPIFFS_PATH, "r");
        if (f) {
            String body = f.readString();
            f.close();
            DynamicJsonDocument doc(2048);
            if (!deserializeJson(doc, body)) {
                c.enabled     = doc["enabled"]     | c.enabled;
                c.sensitivity = doc["sensitivity"] | c.sensitivity;
                c.speakOnPet  = doc["speakOnPet"]  | c.speakOnPet;
                JsonArray pa = doc["prompts"].as<JsonArray>();
                if (!pa.isNull()) {
                    c.prompts.clear();
                    for (JsonVariant v : pa) { String s = v.as<String>(); s.trim(); if (s.length()) c.prompts.push_back(s); }
                }
            }
        }
    } else {
        Serial.println("[pet] no petreaction.json — using defaults");
    }
    clampConfig(c);
    lock(); g_cfg = c; unlock();
}

static void buildJson(const PetConfig& c, String& out) {
    DynamicJsonDocument doc(2048);
    doc["enabled"]     = c.enabled;
    doc["sensitivity"] = c.sensitivity;
    doc["speakOnPet"]  = c.speakOnPet;
    doc["detected"]    = g_detected;
    JsonArray pa = doc.createNestedArray("prompts");
    for (auto& s : c.prompts) pa.add(s);
    serializeJson(doc, out);
}

String pet_reaction_get_json() {
    String out;
    lock(); buildJson(g_cfg, out); unlock();
    return out;
}

bool pet_reaction_set_json(const String& json) {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, json)) return false;
    PetConfig c;
    lock(); c = g_cfg; unlock();
    if (doc.containsKey("enabled"))     c.enabled     = doc["enabled"];
    if (doc.containsKey("sensitivity")) c.sensitivity = doc["sensitivity"];
    if (doc.containsKey("speakOnPet"))  c.speakOnPet  = doc["speakOnPet"];
    JsonArray pa = doc["prompts"].as<JsonArray>();
    if (!pa.isNull()) {
        c.prompts.clear();
        for (JsonVariant v : pa) { String s = v.as<String>(); s.trim(); if (s.length()) c.prompts.push_back(s); }
    }
    clampConfig(c);
    String out;
    buildJson(c, out);
    File f = SPIFFS.open(PET_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[pet] SPIFFS open(w) failed"); return false; }
    f.print(out);
    f.close();
    lock(); g_cfg = c; unlock();
    Serial.println("[pet] saved config to SPIFFS");
    return true;
}

// Shared cooldown across IMU and touch-stroke triggers.
static uint32_t g_lastPetMs = 0;
static uint32_t g_lastSpeakMs = 0;

// Run the happy "pet me" reaction now. Respects enabled + a 3s cooldown.
// Called from pet_reaction_tick() (IMU) and from the touch-stroke handler.
extern volatile bool g_inAiMod;   // defined in RealtimeAiMod.cpp

// Speech (if any) MUST be issued from the main loop context (both callers are).
bool pet_reaction_fire() {
    if (!g_inAiMod) return false;   // 쓰담 반응은 AI 대화 모드에서만 (포토프레임 등에선 끼어들지 않게)
    bool enabled, speakOnPet; String prompt;
    lock();
    enabled = g_cfg.enabled;
    speakOnPet = g_cfg.speakOnPet;
    if (speakOnPet && !g_cfg.prompts.empty()) prompt = g_cfg.prompts[random(g_cfg.prompts.size())];
    unlock();
    if (!enabled) return false;

    uint32_t now = millis();
    if (now - g_lastPetMs < 3000) return false;   // cooldown
    if (is_speaking()) return false;
    g_lastPetMs = now;

    Serial.println("[pet] reaction fired");
    avatar.setExpression(Expression::Happy);
    gesture_play(Expression::Happy);
    sfx_play_event("pet");       // optional SD sound effect bound to the "pet" event; skipped if missing/busy
    idle_motion_hold(2500);
    idle_talk_note_activity();   // count as interaction

    if (speakOnPet && !isOffline && prompt.length() && (now - g_lastSpeakMs > 15000)) {
#if defined(REALTIME_API)
        if (robot && robot->llm) {
            ((RealtimeLLMBase*)(robot->llm))->pushUserText(prompt);
            g_lastSpeakMs = now;
        }
#endif
    }
    return true;
}

void pet_reaction_tick() {
    if (!g_detected) return;
    if (camera_is_busy()) return;   // camera owns the internal I2C during capture

    static uint32_t lastReadMs = 0;
    static float energy = 0.0f;

    uint32_t now = millis();
    if (now - lastReadMs < 40) return;   // ~25 Hz
    lastReadMs = now;

    bool enabled; int sensitivity;
    lock();
    enabled = g_cfg.enabled;
    sensitivity = g_cfg.sensitivity;
    unlock();
    if (!enabled) return;

    // While a gesture is running the servo physically moves the head, which the
    // IMU registers as motion — ignore it so we don't react to our own gestures.
    if (now < gesture_suppress_until) { energy *= 0.7f; return; }

    // While Stack-chan is speaking, the speaker physically vibrates the case and
    // the IMU reads it as motion → self-triggered "petting" → it talks to itself.
    // Ignore IMU energy during (and just after) speech.
    if (is_speaking()) { energy *= 0.7f; g_lastPetMs = now; return; }

    float gx, gy, gz;
    if (!M5.Imu.getGyro(&gx, &gy, &gz)) return;
    float motion = sqrtf(gx * gx + gy * gy + gz * gz);   // deg/s

    // Low-pass the motion into a decaying "handling energy" score.
    energy = energy * 0.80f + motion * 0.20f;

    // sensitivity 1..10 -> threshold ~260 (hard) .. 35 (easy)
    float threshold = 285.0f - sensitivity * 25.0f;

    if (energy > threshold) {
        if (pet_reaction_fire()) {
            Serial.printf("[pet] (imu) energy=%.0f thr=%.0f\n", energy, threshold);
            energy = 0.0f;   // reset so we don't immediately retrigger
        }
    }
}

void pet_reaction_init() {
    if (g_mux == NULL) g_mux = xSemaphoreCreateMutex();
    load_from_spiffs();
    if (!M5.Imu.isEnabled()) {
        g_detected = false;
        Serial.println("[pet] no IMU detected — pet reaction disabled");
        return;
    }
    g_detected = true;
    Serial.printf("[pet] IMU ready (type=%d, polled from loop)\n", (int)M5.Imu.getType());
}
