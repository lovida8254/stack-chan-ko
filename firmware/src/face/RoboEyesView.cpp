#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>                 // m5avatar::Expression, Avatar (getExpression)
#include "face/RoboEyesView.h"
#include "NightMode.h"              // night_mode_is_night() — 수면/밤모드 눈 감기
#include "Sfx.h"                    // 감정/취침·기상 효과음 이벤트
// ⚠️ FluxGarage_RoboEyes.h 는 전역변수(BGCOLOR/MAINCOLOR) + 매크로(DEFAULT/ON/OFF/N/E/S/W..)를
//    뿌리므로 반드시 맨 마지막에, 이 .cpp 한 곳에서만 include.
#include <FluxGarage_RoboEyes.h>

using namespace m5avatar;
extern Avatar avatar;

// ── m5stack-avatar 렌더 핸드셰이크 (Avatar.cpp 정의) ──
namespace m5avatar {
  extern volatile bool g_avatar_sd_pause;       // SD 읽기(효과음 등) 진행 중 (읽기전용)
  extern volatile bool g_avatar_render_pause;   // RoboEyes 가 화면 점유 → 아바타 얼굴 렌더 정지
  extern volatile bool g_avatar_sd_paused;      // ACK (아바타 draw 가 멈춤)
}

namespace {
// M5GFX 어댑터: RoboEyes 의 4개 호출을 M5Canvas 스프라이트(PSRAM)에 매핑.
// color(uint8_t 0/1) → RGB565 팔레트(검정/시안) 번역.
class M5RoboDisplay {
public:
  M5Canvas* cv = nullptr;
  uint16_t pal[2] = { 0x0000, 0x07FF };  // 0=배경(검정), 1=메인(시안)
  void clearDisplay() { if (cv) cv->fillScreen(pal[0]); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint8_t c) { if (cv) cv->fillRoundRect(x, y, w, h, r, pal[c & 1]); }
  void fillTriangle(int x1, int y1, int x2, int y2, int x3, int y3, uint8_t c) { if (cv) cv->fillTriangle(x1, y1, x2, y2, x3, y3, pal[c & 1]); }
  void display() { /* no-op: RoboEyesView 가 오버레이 합성 후 직접 pushSprite */ }
};

M5Canvas*               g_cv = nullptr;
M5RoboDisplay           g_disp;
RoboEyes<M5RoboDisplay> g_eyes(g_disp);
bool                    g_ready = false;
int                     g_lastExpr = -1;
uint32_t                g_lastFrame = 0;
String                  g_status;          // 상단 상태 텍스트("듣는 중..." 등), ""=숨김
bool                    g_qrOn = false;    // QR 표시 중이면 눈 대신 QR
String                  g_qrUrl;
float                   g_talkLevel = 0.0f; // 발화 중 오디오 레벨(0~1), 말하는 눈 진동용
bool                    g_sleeping = false; // 수면/밤모드 = 눈 감김
uint32_t                g_lastEmoSfxMs = 0; // 감정 효과음 쿨다운(도배 방지)

// m5avatar::Expression → sfx 이벤트 이름
static const char* emotion_event(int expr) {
  switch (expr) {
    case (int)Expression::Happy:  return "happy";
    case (int)Expression::Sad:    return "sad";
    case (int)Expression::Angry:  return "angry";
    case (int)Expression::Doubt:  return "doubt";
    case (int)Expression::Sleepy: return "sleepy";
    default:                      return "neutral";
  }
}

static const int   EYE_H    = 92;   // 기본 눈 높이(roboeyes_view_begin 과 일치)
static const float SAD_SCALE = 0.62f;
static const float DOUBT_SQUINT = 0.45f;

// m5avatar::Expression → RoboEyes 표정.
// "기본값 리셋 후 적용" — SAD/DOUBT 는 per-eye 높이/위치/idle 조합으로 구성하므로,
// 다른 표정으로 바뀔 때 반드시 기본값으로 되돌린다.
void applyMood(int expr) {
  // ── reset to defaults ──
  g_eyes.setMood(DEFAULT);
  g_eyes.setHeight(EYE_H, EYE_H);
  g_eyes.setIdleMode(ON, 2, 2);     // idle 켜짐 = 시선 다시 자유롭게 이동

  switch (expr) {
    case (int)Expression::Happy:
      g_eyes.setMood(HAPPY);                 // 아래 눈꺼풀 곡선(웃는 눈)
      break;
    case (int)Expression::Angry:
      g_eyes.setMood(ANGRY);                 // 안쪽 위 눈꺼풀 처짐
      break;
    case (int)Expression::Sleepy:
      g_eyes.setMood(TIRED);                 // 바깥 위 눈꺼풀 처짐(반쯤 감김)
      break;
    case (int)Expression::Sad:
      // 풀죽음: 졸린 처짐 + 눈 작게 + 아래를 바라봄
      g_eyes.setMood(TIRED);
      g_eyes.setHeight((byte)(EYE_H * SAD_SCALE), (byte)(EYE_H * SAD_SCALE));
      g_eyes.setIdleMode(OFF);
      g_eyes.setPosition(S);                 // 시선 아래로
      break;
    case (int)Expression::Doubt:
      // 회의적 "음?": 한쪽 눈 찡그림(비대칭) + 잠깐 좌우 흔들
      g_eyes.setHeight(EYE_H, (byte)(EYE_H * DOUBT_SQUINT));
      g_eyes.anim_confused();
      break;
    case (int)Expression::Neutral:
    default:
      break;                                 // DEFAULT 이미 적용됨
  }
}
}  // namespace

void roboeyes_view_begin() {
  if (g_ready) return;
  g_cv = new M5Canvas(&M5.Display);
  g_cv->setPsram(true);
  g_cv->setColorDepth(16);
  if (!g_cv->createSprite(320, 240)) {
    Serial.println("[roboeyes] createSprite FAILED");
    return;
  }
  g_disp.cv = g_cv;
  g_eyes.begin(320, 240, 30);          // 30fps
  g_eyes.setDisplayColors(0, 1);       // 어댑터에서 0=검정, 1=시안
  g_eyes.setWidth(92, 92);             // 320x240 용 크게 (기본값은 128x64 기준)
  g_eyes.setHeight(92, 92);
  g_eyes.setBorderradius(28, 28);
  g_eyes.setSpacebetween(44);
  g_eyes.setAutoblinker(ON, 3, 2);     // 3초(+0~2초)마다 깜빡
  g_eyes.setIdleMode(ON, 2, 2);        // 2초(+0~2초)마다 시선 이동
  g_ready = true;
  Serial.println("[roboeyes] view ready (320x240, PSRAM)");
}

void roboeyes_view_take() {
  m5avatar::g_avatar_render_pause = true;
  uint32_t t0 = millis();
  while (!m5avatar::g_avatar_sd_paused && (millis() - t0 < 300)) delay(2);  // ACK 대기
  g_lastExpr = -1;   // 다음 렌더에서 mood 재적용 강제
}

void roboeyes_view_release() {
  m5avatar::g_avatar_render_pause = false;
}

void roboeyes_view_blink() {
  if (g_ready) g_eyes.blink();
}

void roboeyes_view_render() {
  if (!g_ready) return;
  if (m5avatar::g_avatar_sd_pause) return;   // SD 읽기(효과음) 중 → LCD 건드리지 않음

  // 수면/밤모드: 눈 감기(autoblink/idle off). "잘자"·시간기반 밤 모두 적용(night_mode_is_night).
  // 백라이트 어둑하게는 NightMode 가 이미 처리(M5.Display.setBrightness).
  bool sleeping = night_mode_is_night();
  if (sleeping != g_sleeping) {
    g_sleeping = sleeping;
    if (sleeping) {
      g_eyes.setAutoblinker(OFF);
      g_eyes.setIdleMode(OFF);
      g_eyes.close();                        // 눈 감김(자는 모습)
      sfx_play_event("sleep");               // 취침 효과음(매핑 시)
    } else {
      g_eyes.setAutoblinker(ON, 3, 2);
      g_eyes.setIdleMode(ON, 2, 2);
      g_eyes.open();                         // 깨어남
      g_lastExpr = -1;                       // 표정 재적용
      sfx_play_event("wake");                // 기상 효과음(매핑 시)
    }
  }

  if (!sleeping) {                           // 자는 동안엔 표정 무시(감은 눈 유지)
    int expr = (int)avatar.getExpression();  // 단일 진실원: 모든 setExpression 호출처가 자동 반영
    if (expr != g_lastExpr) {
      applyMood(expr);
      g_lastExpr = expr;
      if (millis() - g_lastEmoSfxMs > 60000) {   // 감정 효과음(60초 쿨다운: 채팅 중 표정 잦은 변화에도 도배 안 됨, 발화 중엔 자동 스킵)
        sfx_play_event(emotion_event(expr));
        g_lastEmoSfxMs = millis();               // 시도만 해도 쿨다운 리셋 → 발화로 스킵돼도 60초간 재시도 안 함
      }
    }
  }

  if (millis() - g_lastFrame < 33) return;   // ~30fps 게이트(어댑터 display()=no-op라 직접 제어)
  g_lastFrame = millis();

  if (g_qrOn) {
    // QR 전체 화면 (눈 대신) — 설정 페이지 IP 안내
    g_cv->fillScreen(0x0000);
    g_cv->qrcode(g_qrUrl.c_str(), (320 - 200) / 2, 16, 200, 5);
    g_cv->setFont(&fonts::Font2);
    g_cv->setTextColor(0x07FF, 0x0000);
    g_cv->setTextDatum(textdatum_t::top_center);
    g_cv->drawString(g_qrUrl.c_str(), 160, 222);
  } else {
    // 말하는 눈: 발화 중 미세 상하 진동(입이 없으니 눈으로 "말하는 느낌"). 자는 중엔 정지.
    if (!g_sleeping && g_talkLevel > 0.06f) g_eyes.setVFlicker(true, 1 + (int)(g_talkLevel * 3.0f));
    else                                    g_eyes.setVFlicker(false, 0);

    g_eyes.drawEyes();                        // 눈 그리기(어댑터 display()=no-op → push 안 함)

    // 시선↔서보: 눈 목표 위치 → avatar gaze. 서보 태스크가 servo_home=false(발화 중)일 때
    // 머리로 따라감(서보 루프 2초 주기라 안 떨림). idle 중엔 servo_home=true 라 머리 정지.
    float cx = g_eyes.getScreenConstraint_X() * 0.5f;
    float cy = g_eyes.getScreenConstraint_Y() * 0.5f;
    float h = (cx > 0.5f) ? (g_eyes.eyeLxNext - cx) / cx : 0.0f;
    float v = (cy > 0.5f) ? (g_eyes.eyeLyNext - cy) / cy : 0.0f;
    if (h > 1) h = 1; else if (h < -1) h = -1;
    if (v > 1) v = 1; else if (v < -1) v = -1;
    avatar.setGaze(v, h);

    if (!g_sleeping && g_status.length()) {   // 상단 상태 텍스트 오버레이(자는 중엔 숨김)
      g_cv->setFont(&fonts::efontKR_16);
      g_cv->setTextColor(0x07FF, 0x0000);
      g_cv->setTextDatum(textdatum_t::top_center);
      g_cv->drawString(g_status.c_str(), 160, 4);
    }
  }
  g_cv->pushSprite(0, 0);
}

bool roboeyes_view_ready() { return g_ready; }

void roboeyes_view_set_status(const char* utf8) { g_status = utf8 ? utf8 : ""; }
void roboeyes_view_set_qr(const char* url)      { if (url) { g_qrUrl = url; g_qrOn = true; } }
void roboeyes_view_clear_qr()                   { g_qrOn = false; }
bool roboeyes_view_qr_shown()                   { return g_qrOn; }
void roboeyes_view_set_talk(float level)        { g_talkLevel = (level < 0) ? 0 : (level > 1 ? 1 : level); }
