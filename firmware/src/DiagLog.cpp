#include <Arduino.h>
#include <stdarg.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <SPIFFS.h>
#include <esp_system.h>
#include "DiagLog.h"

#define DIAG_PATH     "/diag.log"
#define DIAG_MAX_SIZE (128 * 1024)   // rotate (truncate) above this to bound flash use

volatile bool g_ws_connected = false;

static SemaphoreHandle_t g_mux = NULL;
static void lock()   { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); }
static void unlock() { if (g_mux) xSemaphoreGive(g_mux); }

static const char* reset_reason_str(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";      // 정상 전원 인가
    case ESP_RST_SW:        return "SW";           // esp_restart() (재플래시 등)
    case ESP_RST_PANIC:     return "PANIC";        // 예외/abort (크래시)
    case ESP_RST_INT_WDT:   return "INT_WDT";      // 인터럽트 워치독
    case ESP_RST_TASK_WDT:  return "TASK_WDT";     // 태스크 워치독 (행)
    case ESP_RST_WDT:       return "OTHER_WDT";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";     // 전원 부족 (USB 노이즈 등)
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_EXT:       return "EXT";          // 외부 리셋 핀 (RTS 등)
    default:                return "UNKNOWN";
  }
}

// Append one line with a millis() timestamp. Rotates by truncating when too big.
void diag_log(const char* fmt, ...) {
  if (g_mux == NULL) return;
  char buf[256];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  lock();
  File f = SPIFFS.open(DIAG_PATH, FILE_APPEND);
  if (f) {
    if (f.size() > DIAG_MAX_SIZE) { f.close(); SPIFFS.remove(DIAG_PATH); f = SPIFFS.open(DIAG_PATH, FILE_APPEND); }
    if (f) { f.printf("[%lu] %s\n", (unsigned long)(millis() / 1000), buf); f.close(); }
  }
  unlock();
}

static void heartbeat_task(void* arg) {
  for (;;) {
    diag_log("hb heapI=%u heapS=%u ws=%d",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             g_ws_connected ? 1 : 0);
    vTaskDelay(20000 / portTICK_PERIOD_MS);   // every 20s
  }
}

void diag_log_init() {
  if (g_mux == NULL) g_mux = xSemaphoreCreateMutex();
  if (!SPIFFS.begin(true)) { Serial.println("[diag] SPIFFS mount failed"); return; }
  esp_reset_reason_t r = esp_reset_reason();
  diag_log("==== BOOT  reset=%s  heapI=%u heapS=%u ====",
           reset_reason_str(r),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  Serial.printf("[diag] boot logged (reset=%s). Heartbeat every 20s → %s (FTP)\n",
                reset_reason_str(r), DIAG_PATH);
  xTaskCreate(heartbeat_task, "diag_hb", 3072, NULL, 1, NULL);
}
