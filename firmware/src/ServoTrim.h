#ifndef _SERVO_TRIM_H
#define _SERVO_TRIM_H

#include <Arduino.h>

// 서보 홈(중앙) 보정 오프셋(deg). 조립체마다 서보 혼(horn) 장착 오차로 홈이 비뚤어지는 걸
// 보정. 모든 서보 이동(origin/gaze/gesture/manual)에 더해져 기준점이 이동한다.
// /servotrim.json 에 영속. 설정 페이지의 "이 위치를 홈으로 저장"으로 조이스틱 보정.
extern int g_servoTrimX;
extern int g_servoTrimY;

void servo_trim_load();              // 부팅 시 SPIFFS 에서 로드
bool servo_trim_save(int x, int y);  // 저장 + 즉시 적용 (±40deg 클램프)

#endif  // _SERVO_TRIM_H
