#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "Proximity.h"
#include "Gesture.h"
#include "IdleMotion.h"
#include "IdleTalk.h"
#include "CameraVision.h"
#include "Sfx.h"          // 깜짝 효과음 이벤트

using namespace m5avatar;

extern Avatar avatar;

#define PROX_SPIFFS_PATH  "/proximity.json"

// --- LTR-553ALS register map (proximity portion) ----------------------------
#define LTR553_ADDR        0x23
#define LTR553_PS_CONTR    0x81
#define LTR553_PS_LED      0x82
#define LTR553_PS_N_PULSES 0x83
#define LTR553_PS_MEAS     0x84
#define LTR553_PART_ID     0x86   // expect 0x92 (part 0x9, rev 0x2)
#define LTR553_PS_DATA_0   0x8D
#define LTR553_PS_DATA_1   0x8E   // low 3 bits = PS_DATA[10:8]
#define LTR553_I2C_FREQ    100000

struct ProxConfig {
    bool  enabled           = true;
    int   nearThreshold     = 150;   // PS value above which eyes start to grow
    int   veryNearThreshold = 600;   // PS value above which surprise reaction fires
    float maxEyeScale       = 1.6f;  // eye radius multiplier at very near
};

static ProxConfig g_cfg;
static portMUX_TYPE g_cfgMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool g_detected = false;
static volatile int  g_rawPs    = 0;

static void clampConfig(ProxConfig& c) {
    if (c.nearThreshold < 0)    c.nearThreshold = 0;
    if (c.nearThreshold > 2047) c.nearThreshold = 2047;
    if (c.veryNearThreshold <= c.nearThreshold) c.veryNearThreshold = c.nearThreshold + 50;
    if (c.veryNearThreshold > 2047) c.veryNearThreshold = 2047;
    if (c.maxEyeScale < 1.0f) c.maxEyeScale = 1.0f;
    if (c.maxEyeScale > 3.0f) c.maxEyeScale = 3.0f;
}

static void load_from_spiffs() {
    if (!SPIFFS.exists(PROX_SPIFFS_PATH)) {
        Serial.println("[prox] no proximity.json — using defaults");
        return;
    }
    File f = SPIFFS.open(PROX_SPIFFS_PATH, "r");
    if (!f) return;
    String body = f.readString();
    f.close();
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, body)) {
        Serial.println("[prox] proximity.json parse error — using defaults");
        return;
    }
    ProxConfig c;
    c.enabled           = doc["enabled"]           | c.enabled;
    c.nearThreshold     = doc["nearThreshold"]     | c.nearThreshold;
    c.veryNearThreshold = doc["veryNearThreshold"] | c.veryNearThreshold;
    c.maxEyeScale       = doc["maxEyeScale"]       | c.maxEyeScale;
    clampConfig(c);
    portENTER_CRITICAL(&g_cfgMux);
    g_cfg = c;
    portEXIT_CRITICAL(&g_cfgMux);
    Serial.println("[prox] loaded config from SPIFFS");
}

static void buildJson(const ProxConfig& c, String& out) {
    DynamicJsonDocument doc(512);
    doc["enabled"]           = c.enabled;
    doc["nearThreshold"]     = c.nearThreshold;
    doc["veryNearThreshold"] = c.veryNearThreshold;
    doc["maxEyeScale"]       = c.maxEyeScale;
    doc["detected"]          = g_detected;   // live: sensor present?
    doc["raw"]               = g_rawPs;       // live: current PS reading (for tuning)
    serializeJson(doc, out);
}

String proximity_get_json() {
    ProxConfig c;
    portENTER_CRITICAL(&g_cfgMux);
    c = g_cfg;
    portEXIT_CRITICAL(&g_cfgMux);
    String out;
    buildJson(c, out);
    return out;
}

bool proximity_set_json(const String& json) {
    DynamicJsonDocument doc(512);
    if (deserializeJson(doc, json)) return false;
    ProxConfig c;
    portENTER_CRITICAL(&g_cfgMux);
    c = g_cfg;
    portEXIT_CRITICAL(&g_cfgMux);
    if (doc.containsKey("enabled"))           c.enabled           = doc["enabled"];
    if (doc.containsKey("nearThreshold"))     c.nearThreshold     = doc["nearThreshold"];
    if (doc.containsKey("veryNearThreshold")) c.veryNearThreshold = doc["veryNearThreshold"];
    if (doc.containsKey("maxEyeScale"))       c.maxEyeScale       = doc["maxEyeScale"];
    clampConfig(c);
    portENTER_CRITICAL(&g_cfgMux);
    g_cfg = c;
    portEXIT_CRITICAL(&g_cfgMux);

    String out;
    buildJson(c, out);
    File f = SPIFFS.open(PROX_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[prox] SPIFFS open(w) failed"); return false; }
    f.print(out);
    f.close();
    Serial.println("[prox] saved config to SPIFFS");
    return true;
}

static bool ltr553_begin() {
    // Probe part id on the internal I2C bus.
    uint8_t part = M5.In_I2C.readRegister8(LTR553_ADDR, LTR553_PART_ID, LTR553_I2C_FREQ);
    Serial.printf("[prox] LTR-553 part id = 0x%02X (expect 0x92)\n", part);
    if (part != 0x92) {
        // Some revisions differ; also accept "present on bus" as a fallback.
        if (!M5.In_I2C.scanID(LTR553_ADDR, LTR553_I2C_FREQ)) {
            Serial.println("[prox] LTR-553 not found on internal I2C — proximity disabled");
            return false;
        }
        Serial.println("[prox] part id mismatch but device answers at 0x23 — continuing");
    }
    // PS LED: 60kHz, current 100% (0x7F default); 8 pulses; 50ms measurement rate.
    M5.In_I2C.writeRegister8(LTR553_ADDR, LTR553_PS_LED,      0x7F, LTR553_I2C_FREQ);
    M5.In_I2C.writeRegister8(LTR553_ADDR, LTR553_PS_N_PULSES, 0x08, LTR553_I2C_FREQ);
    M5.In_I2C.writeRegister8(LTR553_ADDR, LTR553_PS_MEAS,     0x02, LTR553_I2C_FREQ);
    // PS_CONTR = active mode.
    M5.In_I2C.writeRegister8(LTR553_ADDR, LTR553_PS_CONTR,    0x03, LTR553_I2C_FREQ);
    delay(15);  // wakeup time
    return true;
}

static int ltr553_read_ps() {
    uint8_t lo = M5.In_I2C.readRegister8(LTR553_ADDR, LTR553_PS_DATA_0, LTR553_I2C_FREQ);
    uint8_t hi = M5.In_I2C.readRegister8(LTR553_ADDR, LTR553_PS_DATA_1, LTR553_I2C_FREQ) & 0x07;
    return ((int)hi << 8) | lo;
}

// Polled from loop() (NOT a task) so all internal-I2C access stays serialized
// with M5.update() on the main thread — avoids bus contention with touch/IMU.
void proximity_tick() {
    if (!g_detected) return;
    if (camera_is_busy()) return;   // camera owns the internal I2C during capture

    static uint32_t lastReadMs = 0;
    static float curScale = 1.0f;
    static uint32_t lastSurpriseMs = 0;
    static bool wasVeryNear = false;

    uint32_t now = millis();
    if (now - lastReadMs < 120) return;   // ~8 Hz
    lastReadMs = now;

    ProxConfig c;
    portENTER_CRITICAL(&g_cfgMux);
    c = g_cfg;
    portEXIT_CRITICAL(&g_cfgMux);

    if (!c.enabled) {
        if (curScale != 1.0f) { curScale = 1.0f; setEyeRadiusScale(1.0f); }
        wasVeryNear = false;
        return;
    }

    int ps = ltr553_read_ps();
    g_rawPs = ps;

    // Map PS -> target eye scale.
    float target = 1.0f;
    if (ps > c.nearThreshold) {
        int range = c.veryNearThreshold - c.nearThreshold;
        float t = range > 0 ? (float)(ps - c.nearThreshold) / (float)range : 1.0f;
        if (t > 1.0f) t = 1.0f;
        target = 1.0f + t * (c.maxEyeScale - 1.0f);
    }

    // Smooth toward target to avoid jitter (asymmetric: grow fast, shrink slow).
    float k = (target > curScale) ? 0.45f : 0.15f;
    curScale += (target - curScale) * k;
    if (fabsf(curScale - target) < 0.01f) curScale = target;
    setEyeRadiusScale(curScale);

    // Surprise reaction on rising edge into "very near".
    bool veryNear = ps >= c.veryNearThreshold;
    if (veryNear && !wasVeryNear && (now - lastSurpriseMs > 4000)) {
        lastSurpriseMs = now;
        avatar.setExpression(Expression::Doubt);
        gesture_play(Expression::Doubt);
        idle_motion_hold(2500);     // let the surprise show without idle overriding
        idle_talk_on_approach();    // greet if it's been quiet for a while (online only)
        sfx_play_event("surprise"); // 깜짝 효과음(매핑 시)
        Serial.printf("[prox] surprise! ps=%d\n", ps);
    }
    wasVeryNear = veryNear;
}

void proximity_init() {
    load_from_spiffs();
    if (!ltr553_begin()) {
        g_detected = false;
        setEyeRadiusScale(1.0f);
        return;   // sensor absent — proximity_tick() will no-op
    }
    g_detected = true;
    Serial.println("[prox] sensor ready (polled from loop)");
}
