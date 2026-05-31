#ifndef _MY_SCHEDULER_H
#define _MY_SCHEDULER_H

#include <Arduino.h>

#define SCHEDULE_SLOT_COUNT  5   // 0~2 기본(취침/기상/아침 인사), 3~4 자유(알람 등)

extern void init_schedule(void);

// Web UI integration --------------------------------------------------------
// Inject a one-shot user-role message; the model responds immediately. Used by
// the /speak_now endpoint to let the user type a prompt in the browser.
extern void speak_now(const String& text);

// Return the in-memory schedule slots as a JSON string. Served by /schedules_get.
extern String get_schedules_json();

// Replace the in-memory schedule slots from a JSON body and persist to SPIFFS.
// Prompt edits take effect on the NEXT scheduled fire (live). Time / weekday-only
// edits require a reboot to re-register the ScheduleEveryDay objects.
// Returns true on success, false on parse error / SPIFFS write failure.
extern bool set_schedules_json(const String& json);

#endif
