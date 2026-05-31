#ifndef _MOTION_H
#define _MOTION_H

#include <Arduino.h>

// 머리 움직임 녹화/저장/재생.
// steps = 평탄 배열 [x%, y%, ms, x%, y%, ms, ...]  (x/y = -100..100, 서보 범위 대비 퍼센트)
// SPIFFS 슬롯 /mot0.json ~ /mot3.json 에 모션 1개씩 {name, steps:[...]} 저장.
bool   motion_save_json(const String& json);   // {"name":"..","steps":[..]}  성공=true
String motions_list_json();                      // {"motions":[{"name":"..","steps":N}]}
bool   motion_delete(const String& name);
bool   motion_play(const String& name);          // 비동기 재생(태스크). 수동조작 중/이미재생중이면 false.

#endif  // _MOTION_H
