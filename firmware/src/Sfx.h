#ifndef _SFX_H
#define _SFX_H

#include <Arduino.h>

// Sound-effect library. Each sound = { name, file, event }.
//   file  : MP3 on SD at /app/AiStackChanEx/sfx/<file>
//   event : optional auto-trigger binding — "pet" | "approach" | "boot" | "alarm" | ""(voice-only)
// Played three ways: auto (event triggers), voice (play_sound tool → by name), test (by file).
//
// Plays through the shared speaker I2S, coordinated with realtime audio:
// never interrupts active speech, try-locks the audio mutex (skip if busy → no
// deadlock), pauses mic recording for the (short) playback then restores it.

void sfx_init();
bool sfx_play_event(const char* event);   // auto-trigger: play the sound bound to this event
bool sfx_play_name(const char* name);      // voice: fuzzy match by name, then play (즉시)
bool sfx_play_file(const char* file);      // low-level: play a specific file (test / resolved)

// 음성 play_sound 용: 이름 매칭만 하고 큐에 적재(매치 성공 시 true). 실제 재생은
// AI 발화가 끝난 뒤 sfx_pump() 가 함. (발화 중 sfx_play_file 의 "speaking 스킵" 가드로
// 음성 효과음이 무시되던 문제 해결.)
bool sfx_request_name(const char* name);
void sfx_pump();                           // loop 에서 호출: 발화 끝나면 대기 중 사운드 재생

String sfx_get_json();                     // {enabled, sounds:[{name,file,event}], files:[..sd..]}
bool   sfx_set_json(const String& json);

#endif  // _SFX_H
