#ifndef _PROXIMITY_H
#define _PROXIMITY_H

#include <Arduino.h>

// Proximity reaction using the M5Stack CoreS3 built-in LTR-553ALS sensor
// (internal I2C, address 0x23 — no extra hardware required).
//
// When something approaches:
//   * the eyes grow proportionally to closeness (setEyeRadiusScale),
//   * crossing the "very near" threshold triggers a one-shot surprised reaction
//     (Doubt expression + quick gesture).
//
// Thresholds are tunable from the web UI (/prox_get, /prox_set) and persisted to
// SPIFFS (/proximity.json). /prox_get also reports the live raw sensor value and
// whether the sensor was detected, so thresholds can be tuned by watching the
// number on the device's actual desk distance.
//
// If the sensor is not detected at init (wrong part id / I2C silent), the feature
// disables itself gracefully and eyes stay at normal size.

void   proximity_init();                 // probe sensor (call after avatar ready)
void   proximity_tick();                 // poll from loop() — serialized with M5.update()
String proximity_get_json();             // config + live raw value (served by /prox_get)
bool   proximity_set_json(const String& json);  // apply + persist (served by /prox_set)

#endif  // _PROXIMITY_H
