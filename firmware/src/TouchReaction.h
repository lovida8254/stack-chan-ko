#ifndef _TOUCH_REACTION_H
#define _TOUCH_REACTION_H

#include <Arduino.h>

// Touch-screen interactions that complement the IMU pet reaction:
//   * look-toward-touch — eyes glance toward wherever you touch the screen,
//   * stroke-to-pet — rubbing the screen (accumulated drag distance, not a quick
//     flick or tap) triggers the shared happy "pet" reaction.
//
// Polled from loop(); reads M5.Touch state that M5.update() refreshed. Does NOT
// consume touches — the existing mod tap/flick handling in loop() still runs.
// Config (/touch.json) is only ever read/written from the loop task (web set
// handlers run there too), so no locking is needed.

void   touch_reaction_init();
void   touch_reaction_tick();
String touch_reaction_get_json();
bool   touch_reaction_set_json(const String& json);

#endif  // _TOUCH_REACTION_H
