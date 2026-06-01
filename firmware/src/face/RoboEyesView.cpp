#include <Arduino.h>
#include <M5Unified.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
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
// 눈 세로 그라데이션 색(RGB565). 위(밝은쪽)→아래(진한쪽)로 보간해 입체감.
// 감정별로 (top,bot) 두 색을 갖는다. applyMood 에서 표정 바뀔 때 현재색 갱신.
static uint16_t g_eyeTop = 0xFF0C;   // 현재 적용 중인 위쪽 색(기본: 밝은 노랑빛)
static uint16_t g_eyeBot = 0xFB60;   // 현재 적용 중인 아래쪽 색(기본: 진한 주황)

// m5avatar::Expression 6종별 그라데이션 색 (top=밝은쪽, bot=진한쪽).
// (2단계 고정 프리셋. 3단계에서 설정 페이지로 사용자가 바꿀 수 있게 확장)
struct EyeGrad { uint16_t top, bot; };
static EyeGrad g_eyeGrad[6] = {
  /* Neutral */ { 0xFF0C, 0xFB60 },   // 주황 (밝은노랑 → 진한주황) 기본·따뜻
  /* Happy   */ { 0xFFE0, 0xFD20 },   // 노랑 → 주황 (밝고 경쾌)
  /* Sleepy  */ { 0x8C1F, 0x400F },   // 연보라 → 남보라 (어둑·졸림)
  /* Doubt   */ { 0x9FFF, 0x05FF },   // 밝은 시안 → 파랑 (호기심)
  /* Sad     */ { 0x6E7F, 0x021F },   // 하늘 → 진한 파랑 (차분·슬픔)
  /* Angry   */ { 0xFE0C, 0xF800 },   // 주황빛 → 빨강 (강렬·분노)
};
// expr(Expression 값) → g_eyeGrad 인덱스
static int eyegrad_index(int expr) {
  switch (expr) {
    case (int)Expression::Happy:  return 1;
    case (int)Expression::Sleepy: return 2;
    case (int)Expression::Doubt:  return 3;
    case (int)Expression::Sad:    return 4;
    case (int)Expression::Angry:  return 5;
    default:                      return 0;   // Neutral
  }
}

// RGB565 두 색을 t(0..256)로 선형 보간.
static inline uint16_t lerp565(uint16_t a, uint16_t b, int t) {
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (((br - ar) * t) >> 8);
  int g = ag + (((bg - ag) * t) >> 8);
  int bl = ab + (((bb - ab) * t) >> 8);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// M5GFX 어댑터: RoboEyes 의 4개 호출을 M5Canvas 스프라이트(PSRAM)에 매핑.
// color(uint8_t 0/1): 0=배경(검정), 1=눈(세로 그라데이션).
class M5RoboDisplay {
public:
  M5Canvas* cv = nullptr;
  uint16_t pal[2] = { 0x0000, 0x07FF };  // 0=배경(검정), 1=(미사용, 그라데이션이 대체)
  void clearDisplay() { if (cv) cv->fillScreen(pal[0]); }
  void fillRoundRect(int x, int y, int w, int h, int r, uint8_t c) {
    if (!cv) return;
    if ((c & 1) == 0) { cv->fillRoundRect(x, y, w, h, r, pal[0]); return; }  // 배경/눈꺼풀(검정)은 단색
    if (h <= 0 || w <= 0) return;
    // 눈: y줄마다 위→아래 색을 보간하고, 둥근 모서리 행에서는 좌우 인셋을 계산해
    // 라운드렉트 실제 모양대로 가로선을 그린다(세로 그라데이션 + 둥근 모서리).
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    for (int j = 0; j < h; j++) {
      int inset = 0;
      if (j < r) {                       // 위쪽 모서리 행
        int dy = r - 1 - j;
        inset = r - (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
      } else if (j >= h - r) {           // 아래쪽 모서리 행
        int dy = j - (h - r);
        inset = r - (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
      }
      int lw = w - 2 * inset;
      if (lw <= 0) continue;
      uint16_t col = lerp565(g_eyeTop, g_eyeBot, (j * 256) / (h > 1 ? h - 1 : 1));
      cv->drawFastHLine(x + inset, y + j, lw, col);
    }
  }
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

  // 감정별 눈 그라데이션 색 적용(현재색 갱신 → 다음 drawEyes 부터 반영)
  EyeGrad eg = g_eyeGrad[eyegrad_index(expr)];
  g_eyeTop = eg.top;
  g_eyeBot = eg.bot;

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
      // 풀죽음: 졸린 처짐 + 눈 작게 + 살짝 아래를 바라봄(맨 아래까지 X).
      g_eyes.setMood(TIRED);
      g_eyes.setHeight((byte)(EYE_H * SAD_SCALE), (byte)(EYE_H * SAD_SCALE));
      g_eyes.setIdleMode(OFF);
      // setPosition(S)는 화면 최하단(getScreenConstraint_Y)으로 내려가 하단 텍스트와 겹침.
      // 대신 사용 가능 세로범위의 약 55% 지점으로만 살짝 내린다.
      g_eyes.eyeLyNext = (g_eyes.getScreenConstraint_Y() * 55) / 100;
      g_eyes.eyeLxNext = g_eyes.getScreenConstraint_X() / 2;   // 가로 중앙
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
  g_eyes.setIdleVRange(67);            // 시선 상하 이동을 화면 상단 2/3로 제한(아랫부분 미사용)
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

    if (!g_sleeping && g_status.length()) {   // 하단 상태 텍스트 오버레이(자는 중엔 숨김)
      g_cv->setFont(&fonts::efontKR_16);
      g_cv->setTextColor(0xFFFF, 0x0000);     // 흰색
      g_cv->setTextDatum(textdatum_t::bottom_center);
      g_cv->drawString(g_status.c_str(), 160, 236);   // 화면 하단(240 높이, 약간 여백)
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

// ── 감정별 눈 그라데이션 색: SPIFFS 저장/로드 + 웹 JSON ──────────────────────
#define EYECOLOR_CFG "/eyecolor.json"
// g_eyeGrad 인덱스(0..5) ↔ JSON 키. eyegrad_index 의 매핑과 일치.
static const char* kEyeKeys[6] = { "neutral", "happy", "sleepy", "doubt", "sad", "angry" };

static uint16_t hex_to_565(const char* s) {
  if (!s || s[0] != '#') return 0;
  long v = strtol(s + 1, nullptr, 16);
  int r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
static String c565_to_hex(uint16_t c) {
  int r = ((c >> 11) & 0x1F) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
  char buf[8]; snprintf(buf, sizeof(buf), "#%02X%02X%02X", r, g, b);
  return String(buf);
}

// 현재 표정의 색을 즉시 다시 반영(설정 저장 직후 화면에 바로 보이게).
static void reapply_current_eyecolor() {
  int idx = eyegrad_index((int)avatar.getExpression());
  g_eyeTop = g_eyeGrad[idx].top;
  g_eyeBot = g_eyeGrad[idx].bot;
}

void roboeyes_eyecolor_init() {
  if (!SPIFFS.exists(EYECOLOR_CFG)) { Serial.println("[eyecolor] no config — using defaults"); return; }
  File f = SPIFFS.open(EYECOLOR_CFG, "r");
  if (!f) return;
  DynamicJsonDocument doc(1024);
  if (!deserializeJson(doc, f)) {
    for (int i = 0; i < 6; i++) {
      JsonObject o = doc[kEyeKeys[i]];
      if (o.isNull()) continue;
      const char* t = o["top"] | ""; const char* b = o["bot"] | "";
      if (t && *t) g_eyeGrad[i].top = hex_to_565(t);
      if (b && *b) g_eyeGrad[i].bot = hex_to_565(b);
    }
    Serial.println("[eyecolor] loaded from SPIFFS");
  }
  f.close();
  reapply_current_eyecolor();
}

String roboeyes_eyecolor_get_json() {
  DynamicJsonDocument doc(1024);
  for (int i = 0; i < 6; i++) {
    JsonObject o = doc.createNestedObject(kEyeKeys[i]);
    o["top"] = c565_to_hex(g_eyeGrad[i].top);
    o["bot"] = c565_to_hex(g_eyeGrad[i].bot);
  }
  String out; serializeJson(doc, out);
  return out;
}

bool roboeyes_eyecolor_set_json(const String& json) {
  DynamicJsonDocument doc(1024);
  if (deserializeJson(doc, json)) return false;
  for (int i = 0; i < 6; i++) {
    JsonObject o = doc[kEyeKeys[i]];
    if (o.isNull()) continue;
    const char* t = o["top"] | ""; const char* b = o["bot"] | "";
    if (t && *t) g_eyeGrad[i].top = hex_to_565(t);
    if (b && *b) g_eyeGrad[i].bot = hex_to_565(b);
  }
  File f = SPIFFS.open(EYECOLOR_CFG, "w");
  if (!f) { Serial.println("[eyecolor] SPIFFS open(w) failed"); return false; }
  String out = roboeyes_eyecolor_get_json();
  f.print(out); f.close();
  reapply_current_eyecolor();   // 즉시 반영
  Serial.println("[eyecolor] saved");
  return true;
}
