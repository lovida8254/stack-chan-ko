#include <Arduino.h>
#include <soc/gpio_reg.h>
#include <soc/gpio_sig_map.h>
#include "SdBus.h"

// Avatar.cpp(namespace m5avatar)が定義。描画ループはこのフラグを見てループ境界で
// 描画を止める(vTaskSuspend のように描画途中で止めないのでデッドロックしない)。
namespace m5avatar { extern volatile bool g_avatar_sd_pause; extern volatile bool g_avatar_sd_paused; }
using m5avatar::g_avatar_sd_pause;
using m5avatar::g_avatar_sd_paused;

// 注意: ネスト/並行呼び出しは設計上発生しない(写真モードと効果音は同時に走らず、
// 効果音は mutexAudio で直列化、各ラッパも内部で別のラッパを呼ばない)。そのため
// 深さカウンタは使わず単純な true/false にする。カウンタの取りこぼしで pause が
// 永久に true のまま残り画面が真っ黒になる事故を防ぐため。

void sd_bus_lock() {
#if defined(ARDUINO_M5STACK_CORES3)
  g_avatar_sd_pause = true;   // LCD 描画タスクにループ境界での一時停止を要求
  // draw() がループ境界に到達して ACK を返すまで待つ(進行中の draw() を分断しない)。
  uint32_t t0 = millis();
  while (!g_avatar_sd_paused && (millis() - t0 < 300)) delay(2);

  // Panel_M5StackCoreS3::cs_control(true) と同じ操作で GPIO35 を FSPI MISO 入力に固定。
  // 描画タスクが止まっている間 GPIO35 は SD の MISO として機能し続ける。
  *(volatile uint32_t*)GPIO_FUNC35_OUT_SEL_CFG_REG = FSPIQ_OUT_IDX;
  *(volatile uint32_t*)GPIO_ENABLE1_W1TC_REG = 1u << (GPIO_NUM_35 & 31);
#endif
}

void sd_bus_unlock() {
#if defined(ARDUINO_M5STACK_CORES3)
  g_avatar_sd_pause = false;   // 描画再開。次の draw() の cs_control が GPIO35 を D/C に戻す
#endif
}
