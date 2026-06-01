#if defined(REALTIME_API)

#ifndef _REALTIME_AI_MOD_H
#define _REALTIME_AI_MOD_H

#include <Arduino.h>
#include "mod/ModBase.h"
#include "llm/RealtimeLLMBase.h"

class RealtimeAiMod: public ModBase{
private:
    box_t box_servo;
    box_t box_stt;
    box_t box_BtnA;
    box_t box_BtnC;

    String avatarText;
    bool isOffline;

    RealtimeLLMBase* pRtLLM;

    // for TTS
    String ttsText;

    // for alarm (Function Calling)
    void alarmEventHandler();

    // 취침 모드 듣기 중지: 자는 동안 마이크 녹음을 멈춰 주변 소리에 반응 안 하게.
    bool sleepHandled    = false;   // 현재 취침으로 인해 녹음을 멈춘 상태인지
    bool sleepWasRecording = false; // 취침 진입 직전 녹음 중이었는지(깨어날 때 복구용)
    void nightListenGuard();        // idle 에서 매 틱 호출: 취침 전이 감지 → 녹음 stop/복구

public:
    RealtimeAiMod(bool _isOffline);

    void init(void);
    void pause(void);
    void update(int page_no);
    void btnA_pressed(void);
    void btnB_longPressed(void);
    void btnC_pressed(void);
    void display_touched(int16_t x, int16_t y);
    void doubleTapped(float ax, float ay, float az);   // 加速度センサによるダブルタップ検出のコールバック。platformio.iniで-DENABLE_TAP_DETECTを有効にしてください
    void idle(void);
    bool isBusy(void);

    void toggleRealtimeRecord(void);
};


#endif  //_REALTIME_AI_MOD_H

#endif  //REALTIME_API