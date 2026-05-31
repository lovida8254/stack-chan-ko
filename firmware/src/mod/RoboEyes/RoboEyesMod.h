#ifndef _ROBOEYES_MOD_H
#define _ROBOEYES_MOD_H

#include <Arduino.h>
#include "mod/ModBase.h"

// RoboEyes 표정 모드 (FluxGarage RoboEyes, M5GFX 어댑터).
// 1단계 MVP: 도형 눈이 320x240 화면에 떠서 깜빡/시선이동.
// NOTE: FluxGarage_RoboEyes.h 는 전역변수(BGCOLOR/MAINCOLOR) + 매크로(DEFAULT/ON/N/E..)를
//       정의하므로 RoboEyesMod.cpp 한 곳에서만(맨 마지막에) include 한다. 헤더엔 노출 금지.
class RoboEyesMod : public ModBase {
private:
    bool isOffline;
public:
    RoboEyesMod(bool _isOffline = false);
    void init(void);
    void pause(void);
    void idle(void);
    void display_touched(int16_t x, int16_t y);
};

#endif  // _ROBOEYES_MOD_H
