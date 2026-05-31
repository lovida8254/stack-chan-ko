#ifndef _IDLE_TALK_H
#define _IDLE_TALK_H

#include <Arduino.h>

// Proactive speech — Stack-chan talks first when it's been quiet for a while,
// mixing "심심해서 말 걸기" and "가끔 엉뚱한 농담" from one editable prompt pool.
// Also greets when someone approaches (called by the proximity sensor) after a
// period of quiet.
//
// Speaks via the same RealtimeLLMBase::pushUserText path the scheduler uses, so
// it only works online in realtime mode. Tunable + prompt-editable from the web
// UI (/talk_get, /talk_set), persisted to SPIFFS (/idletalk.json).

void   idle_talk_init();
void   idle_talk_tick();   // poll from loop() (keeps WS writes on the main task)
String idle_talk_get_json();
bool   idle_talk_set_json(const String& json);

// Reset the "quiet" timer because the user just interacted (touch / pet /
// conversation). Prevents proactive talk right after activity.
void   idle_talk_note_activity();

// Greet on approach (called from proximity). Speaks an approach prompt only if
// it's already been quiet for the configured time + a cooldown.
void   idle_talk_on_approach();

#endif  // _IDLE_TALK_H
