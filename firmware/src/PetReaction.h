#ifndef _PET_REACTION_H
#define _PET_REACTION_H

#include <Arduino.h>

// "쓰다듬으면 반응" — uses the CoreS3 built-in IMU (BMI270) to detect being
// picked up / patted / stroked (movement energy), and reacts like a happy pet:
// Happy expression + wiggle gesture, and optionally a short delighted remark.
//
// Polled from loop() so IMU I2C reads stay serialized with M5.update(). Tunable
// from the web UI (/pet_get, /pet_set), persisted to SPIFFS (/petreaction.json).
// Disables itself gracefully if no IMU is present.

void   pet_reaction_init();
void   pet_reaction_tick();   // poll from loop() (IMU lift/stroke)
bool   pet_reaction_fire();   // run the happy reaction now (shared by IMU + touch stroke);
                              // respects enabled + cooldown. Returns true if it fired.
String pet_reaction_get_json();
bool   pet_reaction_set_json(const String& json);

#endif  // _PET_REACTION_H
