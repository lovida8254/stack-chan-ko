#include "Volume.h"
#include <Arduino.h>
#include <M5Unified.h>
#include <SPIFFS.h>

#define VOL_PATH  "/volume.txt"

static int g_volume = 120;  // matches DEFAULT_SPEAKER_VOLUME

int volume_get() {
  return g_volume;
}

bool volume_set(int v) {
  if (v < 0)   v = 0;
  if (v > 255) v = 255;
  g_volume = v;
  M5.Speaker.setVolume((uint8_t)v);
  File f = SPIFFS.open(VOL_PATH, "w");
  if (!f) {
    Serial.println("[vol] SPIFFS open(w) failed");
    return false;
  }
  f.printf("%d", v);
  f.close();
  Serial.printf("[vol] saved %d\n", v);
  return true;
}

void volume_init() {
  if (!SPIFFS.exists(VOL_PATH)) {
    Serial.println("[vol] no SPIFFS /volume.txt — keeping current");
    return;
  }
  File f = SPIFFS.open(VOL_PATH, "r");
  if (!f) return;
  String s = f.readString();
  f.close();
  s.trim();
  int v = s.toInt();
  if (v <= 0 || v > 255) {
    Serial.printf("[vol] bad value '%s' in SPIFFS, ignoring\n", s.c_str());
    return;
  }
  g_volume = v;
  M5.Speaker.setVolume((uint8_t)v);
  Serial.printf("[vol] loaded %d from SPIFFS\n", v);
}
