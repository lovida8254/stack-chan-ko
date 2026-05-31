#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "Motion.h"
#include "Robot.h"

extern Robot* robot;
extern volatile uint32_t gesture_suppress_until;   // Gesture.cpp — idle/시선 억제 창
extern volatile uint32_t g_servoManualUntil;       // main.cpp — 웹 수동 조작 중

#define MOTION_SLOTS      4
#define MOTION_MAX_STEPS  120     // step = 3 ints (x%,y%,ms). 80ms 간격이면 ~9.6초
#define MOT_MAX_PAN       40      // 조이스틱(SERVO_MANUAL_MAX_*)과 동일해야 함
#define MOT_MAX_TILT      30

static String slotPath(int i) { return String("/mot") + i + ".json"; }

bool motion_save_json(const String& json) {
  DynamicJsonDocument in(8192);
  if (deserializeJson(in, json)) return false;
  String name = String((const char*)(in["name"] | ""));
  name.trim();
  if (!name.length()) return false;
  JsonArray steps = in["steps"].as<JsonArray>();
  if (steps.isNull() || steps.size() < 3) return false;
  int n = steps.size();
  if (n > MOTION_MAX_STEPS * 3) n = MOTION_MAX_STEPS * 3;
  n -= n % 3;

  // 슬롯 선택: 같은 이름이면 덮어쓰기, 없으면 빈 슬롯, 다 차면 0번.
  int slot = -1, empty = -1;
  for (int i = 0; i < MOTION_SLOTS; i++) {
    if (!SPIFFS.exists(slotPath(i))) { if (empty < 0) empty = i; continue; }
    File f = SPIFFS.open(slotPath(i), "r"); if (!f) continue;
    DynamicJsonDocument d(8192); DeserializationError e = deserializeJson(d, f); f.close();
    if (!e && name == String((const char*)(d["name"] | ""))) { slot = i; break; }
  }
  if (slot < 0) slot = (empty >= 0) ? empty : 0;

  DynamicJsonDocument out(8192);
  out["name"] = name;
  JsonArray st = out.createNestedArray("steps");
  for (int i = 0; i < n; i++) st.add((int)steps[i]);
  File f = SPIFFS.open(slotPath(slot), "w"); if (!f) return false;
  serializeJson(out, f); f.close();
  Serial.printf("[motion] saved '%s' (%d steps) to slot %d\n", name.c_str(), n / 3, slot);
  return true;
}

String motions_list_json() {
  DynamicJsonDocument out(2048);
  JsonArray arr = out.createNestedArray("motions");
  for (int i = 0; i < MOTION_SLOTS; i++) {
    if (!SPIFFS.exists(slotPath(i))) continue;
    File f = SPIFFS.open(slotPath(i), "r"); if (!f) continue;
    DynamicJsonDocument d(8192); DeserializationError e = deserializeJson(d, f); f.close();
    if (e) continue;
    JsonObject o = arr.createNestedObject();
    o["name"]  = String((const char*)(d["name"] | ""));   // 복사(로컬 doc 소멸 대비)
    o["steps"] = (int)(d["steps"].as<JsonArray>().size() / 3);
  }
  String s; serializeJson(out, s); return s;
}

bool motion_delete(const String& name) {
  for (int i = 0; i < MOTION_SLOTS; i++) {
    if (!SPIFFS.exists(slotPath(i))) continue;
    File f = SPIFFS.open(slotPath(i), "r"); if (!f) continue;
    DynamicJsonDocument d(8192); DeserializationError e = deserializeJson(d, f); f.close();
    if (!e && name == String((const char*)(d["name"] | ""))) { SPIFFS.remove(slotPath(i)); return true; }
  }
  return false;
}

// ---- 재생 (제스처식 비동기 태스크) ----
static volatile bool g_motionPlaying = false;
struct PlayCtx { int* steps; int n; };

static void motion_play_task(void* arg) {
  PlayCtx* c = (PlayCtx*)arg;
  for (int i = 0; i + 2 < c->n; i += 3) {
    int degX = c->steps[i]     * MOT_MAX_PAN  / 100;
    int degY = c->steps[i + 1] * MOT_MAX_TILT / 100;
    uint32_t ms = (uint32_t)c->steps[i + 2]; if (ms < 20) ms = 20;
    gesture_suppress_until = millis() + ms + 1500;   // 재생 내내 idle/시선 억제
    if (robot && robot->servo) robot->servo->moveTo(degX, degY, ms);
    delay(ms);
  }
  if (robot && robot->servo) robot->servo->moveTo(0, 0, 400);   // 끝나면 홈으로
  free(c->steps); free(c);
  g_motionPlaying = false;
  vTaskDelete(NULL);
}

bool motion_play(const String& name) {
  if (g_motionPlaying) return false;
  g_servoManualUntil = 0;   // 재생 요청 시 수동 모드 해제(조이스틱 패널 keepalive 무시하고 재생)
  for (int i = 0; i < MOTION_SLOTS; i++) {
    if (!SPIFFS.exists(slotPath(i))) continue;
    File f = SPIFFS.open(slotPath(i), "r"); if (!f) continue;
    DynamicJsonDocument d(8192); DeserializationError e = deserializeJson(d, f); f.close();
    if (e || name != String((const char*)(d["name"] | ""))) continue;
    JsonArray steps = d["steps"].as<JsonArray>();
    int n = steps.size(); if (n < 3) return false;
    int* buf = (int*)malloc(n * sizeof(int)); if (!buf) return false;
    for (int k = 0; k < n; k++) buf[k] = steps[k];
    PlayCtx* c = (PlayCtx*)malloc(sizeof(PlayCtx)); c->steps = buf; c->n = n;
    g_motionPlaying = true;
    if (xTaskCreate(motion_play_task, "motplay", 3072, c, 1, NULL) != pdPASS) {
      free(buf); free(c); g_motionPlaying = false; return false;
    }
    Serial.printf("[motion] play '%s' (%d steps)\n", name.c_str(), n / 3);
    return true;
  }
  return false;
}
