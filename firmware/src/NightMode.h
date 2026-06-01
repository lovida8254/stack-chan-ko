#ifndef _NIGHT_MODE_H
#define _NIGHT_MODE_H

#include <Arduino.h>

// "잘 자라고 인사" / night mode — during the configured night window the screen
// dims, idle motion is biased toward a sleepy look, and a one-shot bedtime
// greeting is spoken when night begins.
//
// Polled from loop() (setBrightness writes the backlight over internal I2C, kept
// serialized with M5.update()). Tunable from the web UI (/night_get, /night_set),
// persisted to SPIFFS (/nightmode.json).

void   night_mode_init();
void   night_mode_tick();          // poll from loop()
bool   night_mode_is_night();      // read by IdleMotion to bias toward Sleepy (sleepyBias 의존)
bool   night_mode_is_sleeping();   // 순수 취침 상태(밤시간 OR 강제취침). sleepyBias 와 무관 — 듣기 중지 판정용
String night_mode_get_json();
bool   night_mode_set_json(const String& json);
void   night_mode_force_sleep(bool sleep);   // voice "잘자"→true(취침), "일어나"→false(스케줄 복귀)

#endif  // _NIGHT_MODE_H
