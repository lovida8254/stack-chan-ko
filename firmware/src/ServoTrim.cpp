#include <Arduino.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "ServoTrim.h"

int g_servoTrimX = 0;
int g_servoTrimY = 0;

#define TRIM_PATH "/servotrim.json"

void servo_trim_load() {
  if (!SPIFFS.exists(TRIM_PATH)) { Serial.println("[servotrim] none — center=0,0"); return; }
  File f = SPIFFS.open(TRIM_PATH, "r");
  if (!f) return;
  DynamicJsonDocument d(256);
  if (!deserializeJson(d, f)) {
    g_servoTrimX = d["x"] | 0;
    g_servoTrimY = d["y"] | 0;
  }
  f.close();
  Serial.printf("[servotrim] loaded x=%d y=%d\n", g_servoTrimX, g_servoTrimY);
}

bool servo_trim_save(int x, int y) {
  if (x < -40) x = -40; else if (x > 40) x = 40;   // 안전 한계
  if (y < -40) y = -40; else if (y > 40) y = 40;
  g_servoTrimX = x;
  g_servoTrimY = y;
  File f = SPIFFS.open(TRIM_PATH, "w");
  if (!f) { Serial.println("[servotrim] save open failed"); return false; }
  DynamicJsonDocument d(256);
  d["x"] = x; d["y"] = y;
  serializeJson(d, f);
  f.close();
  Serial.printf("[servotrim] saved x=%d y=%d\n", x, y);
  return true;
}
