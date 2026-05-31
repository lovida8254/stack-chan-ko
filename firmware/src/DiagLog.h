#ifndef _DIAG_LOG_H
#define _DIAG_LOG_H

#include <Arduino.h>

// Persistent diagnostic log on SPIFFS (/diag.log), downloadable over FTP
// (user "stackchan" / pass "stackchan"). Captures the reset reason on each boot
// (panic / task-watchdog / brownout / power / sw-reset) and a periodic heartbeat
// (uptime, free heap, WS connected) so a silent hang or crash can be diagnosed
// after the fact: the last heartbeat shows the state just before death, and the
// next boot's reset reason shows how it died.
//
// Writes only happen on a dedicated low-priority task + at boot — never in the
// audio/WebSocket hot paths (which must not block on flash I/O).

void diag_log_init();                       // log boot reason + start heartbeat task
void diag_log(const char* fmt, ...);        // append one line (safe: opens/appends/closes)

// Set by the realtime WS connect/disconnect handlers so the heartbeat can record it.
extern volatile bool g_ws_connected;

#endif  // _DIAG_LOG_H
