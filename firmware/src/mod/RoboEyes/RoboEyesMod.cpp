#include <Arduino.h>
#include "RoboEyesMod.h"
#include "face/RoboEyesView.h"   // 공유 RoboEyes 엔진 (FluxGarage include 는 RoboEyesView.cpp 한 곳)

// 단독 RoboEyes 표정 모드(플릭으로 진입). 챗 모드도 같은 RoboEyesView 엔진을 공유한다.
RoboEyesMod::RoboEyesMod(bool _isOffline) : isOffline{_isOffline} {}

void RoboEyesMod::init(void) {
  roboeyes_view_begin();   // 엔진/스프라이트 준비(최초 1회)
  roboeyes_view_take();    // 아바타 얼굴 멈추고 화면 점유
}

void RoboEyesMod::pause(void) {
  roboeyes_view_release(); // 아바타 얼굴 렌더 재개
}

void RoboEyesMod::idle(void) {
  roboeyes_view_render();  // 30fps 렌더 (sd_pause 중엔 스킵)
}

void RoboEyesMod::display_touched(int16_t x, int16_t y) {
  roboeyes_view_blink();   // 탭 = 수동 깜빡임 (입력 확인)
}
