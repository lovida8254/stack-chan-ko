#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "Gesture.h"
#include "Robot.h"

using namespace m5avatar;

extern volatile uint32_t g_servoManualUntil;   // main.cpp — 웹 조이스틱 수동 머리 제어 중

volatile uint32_t gesture_suppress_until = 0;
static QueueHandle_t gestureQueue = NULL;

struct GestureStep {
  int degX;
  int degY;
  uint32_t ms;
};

static void play_sequence(const GestureStep* steps, int n, uint32_t tail_pad_ms) {
  if(robot == nullptr || robot->servo == nullptr){
    Serial.println("[gesture] ABORT: robot/servo null");
    return;
  }
  if(millis() < g_servoManualUntil){   // 웹 조이스틱 수동 제어 중엔 제스처로 머리 안 움직임
    Serial.println("[gesture] skip (manual head control)");
    return;
  }

  uint32_t total = 0;
  for(int i = 0; i < n; i++) total += steps[i].ms;
  gesture_suppress_until = millis() + total + tail_pad_ms + 2000;
  Serial.printf("[gesture] sequence n=%d total=%ums suppress=%ums\n", n, total, total + tail_pad_ms + 2000);

  for(int i = 0; i < n; i++){
    Serial.printf("[gesture]   step %d: x=%d y=%d ms=%u\n", i, steps[i].degX, steps[i].degY, steps[i].ms);
    robot->servo->moveTo(steps[i].degX, steps[i].degY, steps[i].ms);
    delay(steps[i].ms);
  }
}

static void gesture_task(void* arg) {
  Expression e;
  for(;;){
    if(xQueueReceive(gestureQueue, &e, portMAX_DELAY) != pdTRUE) continue;

    Serial.printf("[gesture] dequeued expression=%d\n", (int)e);
    switch(e){
      case Expression::Happy: {
        // 긍정/기쁨 = 상하로 빠르게 끄덕끄덕 (degY 음수 = 고개 숙임, 0 = 중앙). 좌우 흔들기에서 변경.
        // 범위 살짝 축소(-22→-15) + 속도 향상(160/140→100/85ms).
        const GestureStep s[] = {{0, -15, 100}, {0, 0, 85}, {0, -15, 100}, {0, 0, 85}};
        play_sequence(s, 4, 0);
        break;
      }
      case Expression::Sad: {
        const GestureStep s[] = {{0, -40, 600}};
        play_sequence(s, 1, 1000);
        break;
      }
      case Expression::Angry: {
        const GestureStep s[] = {{30, 0, 150}, {-30, 0, 150}, {0, 0, 200}};
        play_sequence(s, 3, 0);
        break;
      }
      case Expression::Doubt: {
        const GestureStep s[] = {{15, 0, 400}};
        play_sequence(s, 1, 1000);
        break;
      }
      case Expression::Sleepy: {
        const GestureStep s[] = {{0, -30, 1500}};
        play_sequence(s, 1, 1000);
        break;
      }
      case Expression::Neutral:
      default: {
        const GestureStep s[] = {{0, 0, 800}};
        play_sequence(s, 1, 0);
        break;
      }
    }
  }
}

void gesture_init() {
  if(gestureQueue != NULL) return;
  gestureQueue = xQueueCreate(1, sizeof(Expression));
  xTaskCreatePinnedToCore(gesture_task, "gesture", 4096, NULL, 1, NULL, APP_CPU_NUM);
  Serial.println("[gesture] task started");
}

void gesture_play(Expression e) {
  if(gestureQueue == NULL){
    Serial.println("[gesture] play called but queue is NULL");
    return;
  }
  Serial.printf("[gesture] play enqueue expression=%d\n", (int)e);
  xQueueOverwrite(gestureQueue, &e);
}
