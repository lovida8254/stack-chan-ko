#ifndef _IDLE_MOTION_H
#define _IDLE_MOTION_H

#include <Arduino.h>

// Idle "alive & cute" behaviour engine.
//
// When the robot is idle (not speaking, no gesture in progress, no recent
// conversation audio), this periodically picks a random expression + matching
// head gesture + on-screen gaze wander + blinks, so Stack-chan looks lively to
// watch even when nobody is interacting. After a conversation, this also breaks
// the "last expression stays frozen" behaviour by drifting back to life.
//
// Everything is tunable from the web UI (/idle_get, /idle_set) and persisted to
// SPIFFS (/idlemotion.json). Reads config live on each action, so edits apply
// immediately (no reboot needed).

void   idle_motion_init();              // load config + start task (call after avatar/robot ready)
String idle_motion_get_json();          // current config as JSON (served by /idle_get)
bool   idle_motion_set_json(const String& json);  // apply + persist (served by /idle_set)

// Lets conversation / proximity code temporarily yield idle motion (e.g. while
// speaking or reacting). millis() timestamp until which idle motion stays quiet.
void   idle_motion_hold(uint32_t ms);

#endif  // _IDLE_MOTION_H
