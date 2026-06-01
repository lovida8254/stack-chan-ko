#ifndef _ROBOEYES_VIEW_H
#define _ROBOEYES_VIEW_H

#include <Arduino.h>

// 공유 RoboEyes 렌더 엔진 (FluxGarage RoboEyes + M5GFX 어댑터).
// RoboEyesMod(단독 모드)와 RealtimeAiMod(챗 모드)가 함께 사용.
// ⚠️ FluxGarage_RoboEyes.h(전역변수/매크로)는 RoboEyesView.cpp 한 곳에서만 include 한다.
//    이 헤더는 안전 — 아무 데서나 include 가능.

void roboeyes_view_begin();    // 스프라이트(320x240, PSRAM) + 엔진 초기화 (idempotent)
void roboeyes_view_take();     // 아바타 얼굴 렌더를 멈추고 화면 점유 (ACK 대기 → 버스 데드락 없음)
void roboeyes_view_release();  // 아바타 얼굴 렌더 재개
void roboeyes_view_render();   // 한 프레임 렌더(mod idle에서 호출). sd_pause 중엔 스킵.
                               // 매 프레임 avatar.getExpression() → setMood 매핑(변경 시).
void roboeyes_view_blink();    // 수동 깜빡임 1회
bool roboeyes_view_ready();

// 발화 중 "말하는 눈": 오디오 레벨(0.0~1.0)을 매 프레임 전달. 0=정지(립싱크 대체).
void roboeyes_view_set_talk(float level);

// ── 오버레이 (서브창 대체: 눈 스프라이트에 직접 합성) ──
void roboeyes_view_set_status(const char* utf8);  // 상단 상태 텍스트("듣는 중..." 등). ""=숨김
void roboeyes_view_set_qr(const char* url);        // QR 표시(눈 대신 전체 화면)
void roboeyes_view_clear_qr();                     // QR 끄고 눈으로 복귀
bool roboeyes_view_qr_shown();

// ── 감정별 눈 그라데이션 색 (설정 페이지에서 커스터마이즈, SPIFFS /eyecolor.json) ──
// 6감정 각각 top(밝은쪽)/bot(진한쪽) 색을 "#RRGGBB" 16진 문자열로 주고받는다.
void   roboeyes_eyecolor_init();                       // 부팅 시 SPIFFS 로드(없으면 기본값)
String roboeyes_eyecolor_get_json();                   // {neutral:{top,bot},happy:{...},...}
bool   roboeyes_eyecolor_set_json(const String& json); // 저장 + 즉시 적용

#endif  // _ROBOEYES_VIEW_H
