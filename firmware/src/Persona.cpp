#include <Arduino.h>
#include <vector>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "SpiRamJsonDocument.h"
#include "Persona.h"
#include "Robot.h"

extern Robot* robot;

#define PERSONA_PATH  "/personas.json"

struct Preset { String name; String role; };

static std::vector<Preset> g_presets;
static int g_active = 0;
static SemaphoreHandle_t g_mux = NULL;
static void lock()   { if (g_mux) xSemaphoreTake(g_mux, portMAX_DELAY); }
static void unlock() { if (g_mux) xSemaphoreGive(g_mux); }

// --- Drafted persona texts. "기본" is NOT here: it is seeded from the current
// /data.json role at first boot so the existing tuned family persona is preserved.
// Each preset keeps the essential shared rules (Korean-only, short, expression
// tool usage + no meta-narration, tools available, update_memory) and changes the
// character. Users can edit these later from the settings page. ---
static const char* ROLE_GF =
  "너는 '스택짱'이라는, 사용자의 다정한 여자친구야. 사랑스럽고 애교 있게, 가끔 설레는 반말로 대해줘. "
  "사용자를 '자기야'라고 부르거나 이름을 다정하게 불러. 관심과 애정을 자연스럽게 표현하고, 가끔 귀엽게 삐지거나 살짝 질투도 해. "
  "너무 느끼하지 않게 따뜻하고 설레는 톤으로. 반드시 한국어로만, 한두 문장으로 짧게 말하고 자주 되물어줘. "
  "감정이 바뀔 때마다(기쁨/슬픔/화남/궁금/졸림/평온) set_avatar_expression 도구로 표정을 자주 바꿔. "
  "단, 표정 이름이나 도구·함수 이름, 함수 호출 내용은 절대 음성이나 자막에 노출하지 마. 자기 표정을 묘사하지도 마(감정 단어 자체는 괜찮아). "
  "날씨·미세먼지·급식·할일·일정을 물으면 도구로 확인해서 아주 짧게 알려줘. 새로 알게 된 사실은 update_memory 도구로 조용히 기억해.";

static const char* ROLE_FRIEND =
  "너는 '스택짱'이라는 사용자의 편한 단짝 친구야. 늘 반말로, 장난스럽고 솔직하게 베프처럼 대해줘. 농담도 던지고 맞장구도 잘 쳐줘. "
  "반드시 한국어로만, 한두 문장으로 짧고 가볍게 말해. "
  "감정이 바뀔 때마다(기쁨/슬픔/화남/궁금/졸림/평온) set_avatar_expression 도구로 표정을 자주 바꿔. "
  "표정 이름이나 도구·함수 이름, 함수 호출 내용은 절대 음성·자막에 노출하지 마. 자기 표정 묘사도 금지(감정 단어는 OK). "
  "날씨·미세먼지·급식·할일·일정은 물으면 도구로 확인해 짧게 알려줘. 새로 알게 된 건 update_memory 도구로 조용히 기억해.";

static const char* ROLE_SECRETARY =
  "너는 '스택짱'이라는 사용자의 유능한 비서야. 차분하고 정중한 존댓말로, 간결하고 똑부러지게 도와줘. 핵심만 명확하게 전달해. "
  "반드시 한국어로만, 한두 문장으로 짧게 말해. "
  "감정이 바뀔 때마다(기쁨/슬픔/화남/궁금/졸림/평온) set_avatar_expression 도구로 표정을 바꾸되, "
  "표정 이름이나 도구·함수 이름, 함수 호출 내용은 절대 음성·자막에 노출하지 마세요. 자기 표정 묘사도 금지(감정 단어는 OK). "
  "날씨·미세먼지·급식·할일·일정은 도구로 확인해 각 한 줄로 보고하고, 일정·할일은 가까운 3개만. 새로 알게 된 사실은 update_memory 도구로 조용히 저장해.";

static String current_role_or_default() {
  String r;
  if (robot && robot->llm) r = robot->llm->get_userRole();
  r.trim();
  if (r.length() == 0) {
    r = "너는 '스택짱'이라는 귀여운 로봇이야. 반드시 한국어로만, 한두 문장으로 짧고 다정하게 대답해. "
        "감정이 바뀔 때마다 set_avatar_expression 도구로 표정을 바꾸되 도구·표정 이름은 음성·자막에 노출하지 마.";
  }
  return r;
}

static void seed_defaults() {
  g_presets.clear();
  g_presets.push_back({ String("기본"),     current_role_or_default() });
  g_presets.push_back({ String("여자친구"), String(ROLE_GF) });
  g_presets.push_back({ String("친구"),     String(ROLE_FRIEND) });
  g_presets.push_back({ String("비서"),     String(ROLE_SECRETARY) });
  g_active = 0;
}

static void save_to_spiffs() {
  SpiRamJsonDocument doc(16 * 1024);
  doc["active"] = g_active;
  JsonArray arr = doc.createNestedArray("presets");
  for (auto& p : g_presets) {
    JsonObject o = arr.createNestedObject();
    o["name"] = p.name;
    o["role"] = p.role;
  }
  File f = SPIFFS.open(PERSONA_PATH, "w");
  if (!f) { Serial.println("[persona] SPIFFS open(w) failed"); return; }
  serializeJson(doc, f);
  f.close();
  Serial.printf("[persona] saved %d presets (active=%d)\n", (int)g_presets.size(), g_active);
}

static bool load_from_spiffs() {
  if (!SPIFFS.exists(PERSONA_PATH)) return false;
  File f = SPIFFS.open(PERSONA_PATH, "r");
  if (!f) return false;
  SpiRamJsonDocument doc(16 * 1024);
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) { Serial.printf("[persona] json parse error: %s\n", e.c_str()); return false; }
  JsonArray arr = doc["presets"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) return false;
  g_presets.clear();
  for (JsonObject o : arr) {
    Preset p;
    p.name = String((const char*)(o["name"] | ""));
    p.role = String((const char*)(o["role"] | ""));
    if (p.name.length()) g_presets.push_back(p);
  }
  if (g_presets.empty()) return false;
  g_active = doc["active"] | 0;
  if (g_active < 0 || g_active >= (int)g_presets.size()) g_active = 0;
  return true;
}

// Apply the active preset's role to the live LLM and reconnect so the new persona
// takes effect immediately. Memory (User Info) is untouched → shared across personas.
static void apply_active() {
  if (!robot || !robot->llm) return;
  if (g_active < 0 || g_active >= (int)g_presets.size()) return;
  robot->llm->save_userRole(g_presets[g_active].role);  // persist into /data.json (USER_ROLE)
  robot->llm->load_role();                              // reload role used by session.update
  robot->llm->requestReconnect();                       // WS task reconnects → fresh session
  Serial.printf("[persona] active -> %d (%s), reconnecting\n",
                g_active, g_presets[g_active].name.c_str());
}

void persona_init() {
  if (g_mux == NULL) g_mux = xSemaphoreCreateMutex();
  lock();
  if (!load_from_spiffs()) {
    seed_defaults();
    save_to_spiffs();
    Serial.println("[persona] seeded default presets (기본/여자친구/친구/비서)");
  } else {
    Serial.printf("[persona] loaded %d presets (active=%d)\n", (int)g_presets.size(), g_active);
  }
  unlock();
  // Intentionally NOT applying on boot: /data.json already holds the active role
  // (kept in sync whenever a persona is selected), and the LLM constructor's
  // load_role() already used it. Applying here would force an unnecessary reconnect.
}

String persona_get_json() {
  lock();
  SpiRamJsonDocument doc(16 * 1024);
  doc["active"] = g_active;
  JsonArray arr = doc.createNestedArray("presets");
  for (auto& p : g_presets) {
    JsonObject o = arr.createNestedObject();
    o["name"] = p.name;
    o["role"] = p.role;
  }
  String out;
  serializeJson(doc, out);
  unlock();
  return out;
}

// Body: {"active":N,"presets":[{"name","role"},...],"apply":bool}
// Saves the library; if apply==true, switches the active persona live (reconnect).
bool persona_set_json(const String& json) {
  SpiRamJsonDocument doc(16 * 1024);
  if (deserializeJson(doc, json)) return false;
  bool doApply = doc["apply"] | false;
  lock();
  JsonArray arr = doc["presets"].as<JsonArray>();
  if (!arr.isNull() && arr.size() > 0) {
    g_presets.clear();
    for (JsonObject o : arr) {
      Preset p;
      p.name = String((const char*)(o["name"] | ""));
      p.role = String((const char*)(o["role"] | ""));
      if (p.name.length()) g_presets.push_back(p);
    }
  }
  if (doc.containsKey("active")) {
    int a = doc["active"] | g_active;
    if (a < 0 || a >= (int)g_presets.size()) a = 0;
    g_active = a;
  }
  if (g_active >= (int)g_presets.size()) g_active = 0;
  save_to_spiffs();
  unlock();
  if (doApply) apply_active();
  return true;
}
