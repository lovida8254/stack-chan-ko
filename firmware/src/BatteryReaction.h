#ifndef _BATTERY_REACTION_H
#define _BATTERY_REACTION_H

#include <Arduino.h>

// Battery / charging reactions for personality:
//   * low battery (not charging)  -> looks sad and occasionally says "배고파",
//   * just plugged in to charge    -> happy "냠냠" reaction (once per plug-in).
//
// Polled from loop() (M5.Power reads + speech kept on the main task). Tunable
// from the web UI (/batt_get, /batt_set), persisted to SPIFFS (/battery.json).
// Config is only read/written from the loop task, so no locking is needed.

void   battery_reaction_init();
void   battery_reaction_tick();
String battery_reaction_get_json();
bool   battery_reaction_set_json(const String& json);

#endif  // _BATTERY_REACTION_H
