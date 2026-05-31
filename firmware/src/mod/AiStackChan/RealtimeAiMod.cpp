#if defined(REALTIME_API)

#include <Arduino.h>
#include <deque>
#include <SD.h>
#include <SPIFFS.h>
#include "mod/ModManager.h"
#include "RealtimeAiMod.h"
#include <Avatar.h>
#include "Robot.h"
#include "llm/ChatGPT/FunctionCall.h"
#include <WiFiClientSecure.h>
#include "Scheduler.h"
#include "MySchedule.h"
#include "Sfx.h"
#include "share/SDUtil.h"
#include "Gesture.h"
#include "face/RoboEyesView.h"   // 챗 모드 얼굴 = RoboEyes 눈
#include "NightMode.h"           // 탭하면 깨어나기(자는 중 판정/해제)

using namespace m5avatar;


/// 外部参照 ///
extern Avatar avatar;
extern bool servo_home;
extern void sw_tone();
extern void alarm_tone();
///////////////



RealtimeAiMod::RealtimeAiMod(bool _isOffline)
  : isOffline{_isOffline}
{
  box_servo.setupBox(80, 120, 80, 80);
  box_stt.setupBox(0, 0, M5.Display.width(), 60);
  box_BtnA.setupBox(0, 100, 40, 60);
  box_BtnC.setupBox(280, 100, 40, 60);

  pRtLLM = (RealtimeLLMBase*)robot->llm;
  pRtLLM->invokeWebSocketLoopTask();

  //servo_home = false;

  if(!isOffline){
    //スケジューラ設定 (midnight bedtime + weekday 07:00 wake + 08:10 howl briefing)
    init_schedule();
  }
}


// True only while the AI conversation mod is the active mod. Touch "쓰담"/gaze
// follow and proactive idle-talk are gated on this so swipes used to navigate
// other modes (photo frame) don't get misread as petting → spurious speech.
volatile bool g_inAiMod = false;

void RealtimeAiMod::init(void)
{
  //avatar.setSpeechText("Realtime AI");
  avatar.set_isSubWindowEnable(true);
  pRtLLM->resumeWebSocketLoopTask();
  g_inAiMod = true;
  // 챗 모드 얼굴을 RoboEyes 눈으로 — 아바타 얼굴 렌더를 멈추고 눈을 띄운다.
  // 표정은 avatar.getExpression()(set_avatar_expression 등) 을 RoboEyesView 가 매 프레임 반영.
  roboeyes_view_begin();
  roboeyes_view_take();
}

void RealtimeAiMod::pause(void)
{
  avatar.set_isSubWindowEnable(false);
  pRtLLM->suspendWebSocketLoopTask();
  g_inAiMod = false;
  roboeyes_view_release();   // 화면 반납 → 아바타 얼굴 렌더 재개(다른 모드용)
}


void RealtimeAiMod::update(int page_no)
{

}

void RealtimeAiMod::btnA_pressed(void)
{
#if defined(ARDUINO_M5STACK_ATOMS3R)
  Serial.println("Btn A pressed");
  sw_tone();
  toggleRealtimeRecord();
#endif
}

void RealtimeAiMod::btnB_longPressed(void)
{

}

void RealtimeAiMod::btnC_pressed(void)
{
  // QR(설정 페이지 IP 안내)을 눈 화면에 합성 — 서브창 대신 RoboEyesView 사용.
  if(!roboeyes_view_qr_shown()){
    String url = String("http://") + WiFi.localIP().toString();
    roboeyes_view_set_qr(url.c_str());
  }else{
    roboeyes_view_clear_qr();
  }
}

void RealtimeAiMod::display_touched(int16_t x, int16_t y)
{
  // 자는 중("잘자"/밤모드)이면 아무 곳이나 탭하면 깨어남(대화 토글은 다음 탭부터).
  if (night_mode_is_night())
  {
    night_mode_force_sleep(false);   // 깨우기(스케줄 복귀 + 화면 밝게)
    sw_tone();
    return;
  }
  if (box_stt.contain(x, y))
  {
    sw_tone();
    toggleRealtimeRecord();
  }
#ifdef USE_SERVO
  if (box_servo.contain(x, y))
  {
    sw_tone();
    servo_home = !servo_home;
    // Cancel any in-flight gesture suppression so the user's explicit toggle
    // takes effect immediately (otherwise post-dialog suppress windows can
    // swallow the first few seconds of intended idle gaze).
    gesture_suppress_until = 0;
  }
#endif
  if (box_BtnA.contain(x, y))
  {
    //sw_tone();
  }
  if (box_BtnC.contain(x, y))
  {
    btnC_pressed();
  }

}

void RealtimeAiMod::doubleTapped(float ax, float ay, float az)
{
  Serial.printf("Mod double tapped. ax=%.3f ay=%.3f az=%.3f\n", ax, ay, az);
#if defined(ARDUINO_M5STACK_ATOMS3R)
  sw_tone();
  toggleRealtimeRecord();
#endif
}


void RealtimeAiMod::idle(void)
{
#ifdef REALTIME_API_WITH_TTS

  if(robot->asyncPlaying || (pRtLLM->getOutputTextQueueSize() != 0)){
    // 発話中
    pRtLLM->setSpeaking(true);
    servo_home = false;
    avatar.setExpression(Expression::Happy);
  }
  else{
    // 発話停止中かつキューにテキストがない場合はLLM機能に発話終了を通知
    pRtLLM->setSpeaking(false);
    servo_home = true;
    avatar.setExpression(Expression::Neutral);
  }

#endif  //REALTIME_API_WITH_TTS

  // Alarm (Function Calling)
  alarmEventHandler();

  //スケジューラ処理
  if(!isOffline){
    run_schedule();
  }

  sfx_pump();   // 음성 play_sound 로 큐된 효과음을 발화 끝난 뒤 재생

  // 챗 모드 얼굴 = RoboEyes 눈. 상태표시(서브창 대체)는 녹음/발화 상태에서 도출.
  bool speaking = pRtLLM->isSpeaking();
  if (!roboeyes_view_qr_shown()) {
    if (speaking)                            roboeyes_view_set_status("");           // 발화 중엔 눈으로 표현
    else if (pRtLLM->isRealtimeRecording())  roboeyes_view_set_status("듣는 중...");
    else                                     roboeyes_view_set_status("터치하면 시작");
  }
  // 말하는 눈: 발화 중 오디오 레벨(0~15000)을 0~1로 정규화해 전달(입 대체 미세 진동)
  roboeyes_view_set_talk(speaking ? (pRtLLM->getAudioLevel() / 12000.0f) : 0.0f);

  roboeyes_view_render();   // 30fps, sd_pause 중엔 스킵
}

void RealtimeAiMod::alarmEventHandler()
{
  if(xAlarmTimer != NULL){
    TickType_t xRemainingTime;

    /* Query the period of the timer that expires. */
    xRemainingTime = xTimerGetExpiryTime( xAlarmTimer ) - xTaskGetTickCount();
    avatarText = "Alarm countdown: " + String(xRemainingTime / 1000);
    avatar.set_isSubWindowEnable(true);
    avatar.updateSubWindowTxt(avatarText, 0, 0, 200, 50);
  }

  if (alarmTimerCallbacked) {
    alarmTimerCallbacked = false;
    avatar.set_isSubWindowEnable(false);
    if (!sfx_play_event("alarm")) alarm_tone();   // "alarm" 사운드가 등록돼 있으면 MP3, 없으면 기본 톤
  }

  if (alarmTimerCanceled) {
    alarmTimerCanceled = false;
    avatar.set_isSubWindowEnable(false);
  }

}

bool RealtimeAiMod::isBusy(void)
{
  if(pRtLLM->isRealtimeRecording() || pRtLLM->isSpeaking()){
    return true;
  }else{
    return false;
  }
}

void RealtimeAiMod::toggleRealtimeRecord(void)
{
  if(pRtLLM->isRealtimeRecording()){
    pRtLLM->stopRealtimeRecord();
  }else{
    pRtLLM->startRealtimeRecord();
  }
}

#endif //REALTIME_API