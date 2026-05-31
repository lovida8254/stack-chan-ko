#include <Arduino.h>
#include <vector>
#include <SD.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <M5Unified.h>
#include "Sfx.h"
#include "Robot.h"
#include "share/Mutex.h"
#include "share/SdBus.h"   // CoreS3: SD存在確認中もアバター描画を止める
#include "driver/PlayMP3.h"
#include "DiagLog.h"       // 음성 효과음 진단 (FTP /diag.log)
#include "Motion.h"        // 사운드에 머리 모션 연동
#include "llm/ChatGPT/FunctionCall.h"   // APP_DATA_PATH
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif

extern Robot* robot;

#define SFX_DIR   APP_DATA_PATH "sfx/"
#define SFX_CFG   "/sfx.json"

struct Sound { String name; String file; String event; String motion; };  // motion = 함께 재생할 모션 이름("" = 없음)
static std::vector<Sound> g_sounds;
static bool g_enabled = true;

// 음성 play_sound 지연 재생 큐(발화 끝난 뒤 sfx_pump 가 재생)
static String   g_pendingFile;
static String   g_pendingMotion;
static bool     g_pendingMotionFired = false;
static uint32_t g_pendingMs = 0;

static void seed_defaults() {
  g_sounds.clear();
  g_sounds.push_back({ String("쓰담"),     String("pet.mp3"),      String("pet") });
  g_sounds.push_back({ String("다가옴"),   String("hello.mp3"),    String("approach") });
  g_sounds.push_back({ String("부팅"),     String("boot.mp3"),     String("boot") });
  g_sounds.push_back({ String("아침알람"), String("alarm.mp3"),    String("alarm") });
  g_sounds.push_back({ String("생일축하"), String("birthday.mp3"), String("") });
}

static void save_to_spiffs() {
  DynamicJsonDocument doc(4096);
  doc["enabled"] = g_enabled;
  JsonArray arr = doc.createNestedArray("sounds");
  for (auto& s : g_sounds) {
    JsonObject o = arr.createNestedObject();
    o["name"] = s.name; o["file"] = s.file; o["event"] = s.event; o["motion"] = s.motion;
  }
  File f = SPIFFS.open(SFX_CFG, "w");
  if (!f) { Serial.println("[sfx] SPIFFS open(w) failed"); return; }
  serializeJson(doc, f); f.close();
}

static bool load_from_spiffs() {
  if (!SPIFFS.exists(SFX_CFG)) return false;
  File f = SPIFFS.open(SFX_CFG, "r");
  if (!f) return false;
  DynamicJsonDocument doc(4096);
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) return false;
  g_enabled = doc["enabled"] | true;
  JsonArray arr = doc["sounds"].as<JsonArray>();
  if (arr.isNull()) return false;
  g_sounds.clear();
  for (JsonObject o : arr) {
    Sound s;
    s.name   = String((const char*)(o["name"]   | ""));
    s.file   = String((const char*)(o["file"]   | ""));
    s.event  = String((const char*)(o["event"]  | ""));
    s.motion = String((const char*)(o["motion"] | ""));
    if (s.name.length() || s.file.length()) g_sounds.push_back(s);
  }
  return true;
}

void sfx_init() {
  if (!load_from_spiffs()) { seed_defaults(); save_to_spiffs(); Serial.println("[sfx] seeded default sound library"); }
  else Serial.printf("[sfx] loaded %d sounds (enabled=%d)\n", (int)g_sounds.size(), g_enabled ? 1 : 0);
}

// Low-level safe playback of /app/AiStackChanEx/sfx/<file>.
bool sfx_play_file(const char* file) {
  if (!g_enabled || file == NULL || file[0] == '\0') return false;
  if (robot == nullptr || robot->llm == nullptr || mutexAudio == NULL) return false;

#if defined(REALTIME_API)
  if (robot->llm->speaking) { diag_log("[sfx] skip: speaking (file=%s)", file); return false; }   // never cut off active speech
  RealtimeLLMBase* rt = (RealtimeLLMBase*)robot->llm;
#endif

  String path = String(SFX_DIR) + file;
  sd_bus_lock();
  bool exists = SD.exists(path.c_str());
  sd_bus_unlock();
  if (!exists) { diag_log("[sfx] skip: file not found %s", path.c_str()); Serial.printf("[sfx] not found: %s\n", path.c_str()); return false; }

  if (xSemaphoreTake(mutexAudio, pdMS_TO_TICKS(40)) != pdTRUE) { Serial.println("[sfx] audio busy — skip"); return false; }   // pump 가 재시도하므로 diag 스팸 방지
  bool wasRec = false;
#if defined(REALTIME_API)
  wasRec = rt->isRealtimeRecording();
  if (wasRec) rt->stopRealtimeRecord();
#endif
  M5.Mic.end();
  diag_log("[sfx] PLAYING %s", path.c_str());
  Serial.printf("[sfx] play %s\n", path.c_str());
  playMP3SD(path.c_str());        // Speaker.begin → play (blocking) → Mic.begin
  xSemaphoreGive(mutexAudio);
#if defined(REALTIME_API)
  if (wasRec) rt->startRealtimeRecord();
#endif
  return true;
}

bool sfx_play_event(const char* event) {
  if (event == NULL || event[0] == '\0') return false;
  for (auto& s : g_sounds) if (s.event == event && s.file.length()) {
    if (s.motion.length()) motion_play(s.motion);   // 함께 재생할 머리 모션(서보, 사운드와 동시)
    return sfx_play_file(s.file.c_str());
  }
  return false;
}

// Voice command: fuzzy-match a spoken name against the library (case-insensitive
// substring either direction), then play. Used by the play_sound tool.
// 이름(음성) → g_sounds 인덱스 퍼지 매칭. 못 찾으면 -1.
static int sfx_match_index(const char* name) {
  if (name == NULL || name[0] == '\0') return -1;
  String q = String(name); q.toLowerCase();
  for (int i = 0; i < (int)g_sounds.size(); i++) {   // 1) 이름 부분일치
    if (!g_sounds[i].file.length()) continue;
    String n = g_sounds[i].name; n.toLowerCase();
    if (n.length() && (n.indexOf(q) >= 0 || q.indexOf(n) >= 0)) return i;
  }
  for (int i = 0; i < (int)g_sounds.size(); i++) {   // 2) 파일명 부분일치
    if (!g_sounds[i].file.length()) continue;
    String fn = g_sounds[i].file; fn.toLowerCase();
    if (fn.indexOf(q) >= 0) return i;
  }
  return -1;
}

bool sfx_play_name(const char* name) {
  int i = sfx_match_index(name);
  if (i < 0) { Serial.printf("[sfx] no sound matches '%s'\n", name ? name : ""); return false; }
  if (g_sounds[i].motion.length()) motion_play(g_sounds[i].motion);   // 함께 머리 모션
  return sfx_play_file(g_sounds[i].file.c_str());
}

// 음성 play_sound: 매칭만 하고 큐에 적재(즉시 재생 X). 발화가 끝나면 sfx_pump 가 재생.
bool sfx_request_name(const char* name) {
  int i = sfx_match_index(name);
  if (i < 0) { Serial.printf("[sfx] no sound matches '%s'\n", name ? name : ""); return false; }
  g_pendingFile        = g_sounds[i].file;
  g_pendingMotion      = g_sounds[i].motion;
  g_pendingMotionFired = false;
  g_pendingMs          = millis();
  Serial.printf("[sfx] queued '%s' (발화 후 재생)\n", name);
  return true;
}

// loop 에서 매 틱 호출. 발화 중이 아니면 대기 중 사운드를 재생(없으면 즉시 반환).
void sfx_pump() {
  if (!g_pendingFile.length()) return;
  if (millis() - g_pendingMs > 10000) { diag_log("[sfx] pump giveup(10s) %s", g_pendingFile.c_str()); g_pendingFile = String(); g_pendingMotion = String(); g_pendingMotionFired = false; return; }
#if defined(REALTIME_API)
  if (robot && robot->llm && robot->llm->speaking) return;   // 발화 끝날 때까지 대기
#endif
  // 함께 재생할 모션은 한 번만 발사(사운드 재시도에 중복 발사 안 되게).
  if (!g_pendingMotionFired && g_pendingMotion.length()) { motion_play(g_pendingMotion); g_pendingMotionFired = true; }
  // 오디오 뮤텍스 busy(발화 꼬리/녹음)면 포기 말고 매 틱 재시도, 성공할 때만 큐 비움.
  if (sfx_play_file(g_pendingFile.c_str())) { g_pendingFile = String(); g_pendingMotion = String(); g_pendingMotionFired = false; }
}

String sfx_get_json() {
  DynamicJsonDocument doc(8192);
  doc["enabled"] = g_enabled;
  JsonArray arr = doc.createNestedArray("sounds");
  for (auto& s : g_sounds) {
    JsonObject o = arr.createNestedObject();
    o["name"] = s.name; o["file"] = s.file; o["event"] = s.event; o["motion"] = s.motion;
  }
  // list actual *.mp3 files present in the SD sfx dir (UI hint).
  // NOTE: open WITHOUT trailing slash — SD.open("/dir/") can fail to enumerate.
  JsonArray files = doc.createNestedArray("files");
  String sfxDirNoSlash = String(APP_DATA_PATH) + "sfx";
  File dir = SD.open(sfxDirNoSlash.c_str());
  if (dir) {
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      if (!e.isDirectory()) {
        String n = String(e.name());
        int sl = n.lastIndexOf('/'); if (sl >= 0) n = n.substring(sl + 1);
        if (n.endsWith(".mp3") || n.endsWith(".MP3")) files.add(n);
      }
    }
    dir.close();
  }
  String out; serializeJson(doc, out);
  return out;
}

bool sfx_set_json(const String& json) {
  DynamicJsonDocument doc(8192);
  if (deserializeJson(doc, json)) return false;
  if (doc.containsKey("enabled")) g_enabled = doc["enabled"];
  JsonArray arr = doc["sounds"].as<JsonArray>();
  if (!arr.isNull()) {
    g_sounds.clear();
    for (JsonObject o : arr) {
      Sound s;
      s.name   = String((const char*)(o["name"]   | ""));
      s.file   = String((const char*)(o["file"]   | ""));
      s.event  = String((const char*)(o["event"]  | ""));
      s.motion = String((const char*)(o["motion"] | ""));
      if (s.name.length() || s.file.length()) g_sounds.push_back(s);
    }
  }
  save_to_spiffs();
  Serial.printf("[sfx] saved %d sounds (enabled=%d)\n", (int)g_sounds.size(), g_enabled ? 1 : 0);
  return true;
}
