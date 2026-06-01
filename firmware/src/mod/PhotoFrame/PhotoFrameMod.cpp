#include <Arduino.h>
#include <deque>
#include <SD.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include "share/SDUtil.h"
#include "mod/ModManager.h"
#include "PhotoFrameMod.h"
#include <Avatar.h>
#include "Robot.h"
//#include "driver/WakeWord.h"
#include "driver/PlayMP3.h"
#include <WiFiClientSecure.h>
#include "llm/ChatGPT/FunctionCall.h"  //APP_DATA_PATHのため
#include "share/SdBus.h"   // CoreS3: SD読込中はアバター描画を止めてGPIO35をMISO入力に固定

using namespace m5avatar;


/// 外部参照 ///
extern Avatar avatar;
extern void sw_tone();

///////////////

std::deque<String> photoList;
static bool photoFrameTimerCallbacked = false;

void photoFrameTimerCallback(TimerHandle_t _xTimer){
  photoFrameTimerCallbacked = true;
}

// ---- Photo frame settings (SPIFFS /photoframe.json), editable from /settings.html ----
#define PF_CFG  "/photoframe.json"
static int    g_slideSec  = 60;    // slideshow interval (seconds)
static String g_folder    = "";    // subfolder under photo/ ("" = the photo/ root)
static bool   g_autoSlide = true;  // auto-advance on entering the mode
static size_t g_photoIdx  = 0;     // current photo index (stable list, index-based nav)

// Full SD directory the photos are read from.
static String pf_base_dir() {
  String d = String(APP_DATA_PATH) + "photo";
  if (g_folder.length()) d += "/" + g_folder;
  return d;
}

void photoframe_config_load() {
  if (!SPIFFS.exists(PF_CFG)) return;
  File f = SPIFFS.open(PF_CFG, "r");
  if (!f) return;
  DynamicJsonDocument doc(512);
  if (!deserializeJson(doc, f)) {
    g_slideSec  = doc["slideSec"]  | g_slideSec;
    g_folder    = String((const char*)(doc["folder"] | ""));
    g_autoSlide = doc["autoSlide"] | g_autoSlide;
  }
  f.close();
}

String photoframe_get_json() {
  DynamicJsonDocument doc(2048);
  doc["slideSec"]  = g_slideSec;
  doc["folder"]    = g_folder;
  doc["autoSlide"] = g_autoSlide;
  // list subfolders under photo/ (albums) so the UI can offer a dropdown
  JsonArray fl = doc.createNestedArray("folders");
  String base = String(APP_DATA_PATH) + "photo";
  File dir = SD.open(base.c_str());
  if (dir) {
    for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
      if (e.isDirectory()) {
        String n = String(e.name());
        int sl = n.lastIndexOf('/'); if (sl >= 0) n = n.substring(sl + 1);
        if (n.length()) fl.add(n);
      }
    }
    dir.close();
  }
  String out; serializeJson(doc, out);
  return out;
}

bool photoframe_set_json(const String& json) {
  DynamicJsonDocument doc(512);
  if (deserializeJson(doc, json)) return false;
  if (doc.containsKey("slideSec"))  { int v = doc["slideSec"]; if (v < 2) v = 2; if (v > 3600) v = 3600; g_slideSec = v; }
  if (doc.containsKey("folder"))    g_folder = String((const char*)(doc["folder"] | ""));
  if (doc.containsKey("autoSlide")) g_autoSlide = doc["autoSlide"];
  File f = SPIFFS.open(PF_CFG, "w");
  if (!f) return false;
  DynamicJsonDocument o(512);
  o["slideSec"] = g_slideSec; o["folder"] = g_folder; o["autoSlide"] = g_autoSlide;
  serializeJson(o, f); f.close();
  Serial.printf("[photo] saved slideSec=%d folder='%s' auto=%d\n", g_slideSec, g_folder.c_str(), g_autoSlide?1:0);
  return true;
}

PhotoFrameMod::PhotoFrameMod(bool _isOffline)
  : isOffline{_isOffline}
{
  box_servo.setupBox(0, 0, 0, 0);   // unused in photo frame
  box_stt.setupBox(0, 0, M5.Display.width(), 50);
  // 왼쪽 절반 = 이전, 오른쪽 절반 = 다음 (좁은 끝 버튼 대신 화면 절반씩, 탭하기 쉽게)
  box_BtnA.setupBox(0, 50, 160, 190);
  box_BtnC.setupBox(160, 50, 160, 190);

}


void PhotoFrameMod::init(void)
{
  avatar.setSpeechText("포토프레임");
  delay(1000);

  photoframe_config_load();
  g_photoIdx = 0;
  String dir = pf_base_dir();
  sd_bus_lock();                       // CoreS3: ディレクトリ走査中も描画を止めてSDを確実に読む
  SD.begin(GPIO_NUM_4, SPI, 25000000); // 他機能(メモ保存等)が SD.end() した後でも確実に再マウント
  photoRoot = SD.open(dir.c_str());
  createPhotoList(photoRoot);
  if (photoRoot) photoRoot.close();
  sd_bus_unlock();
  updatePhoto();

  // Auto-start the slideshow only if enabled in settings (자동 슬라이드 on/off). idle() re-arms.
  xTimer = NULL;
  if (g_autoSlide && photoList.size() > 1) {
    xTimer = xTimerCreate("Timer", g_slideSec * 1000, pdFALSE, 0, photoFrameTimerCallback);
    if (xTimer) xTimerStart(xTimer, 0);
    Serial.printf("[photo] slideshow auto-started (%ds, %d photos)\n", g_slideSec, (int)photoList.size());
  }
}

void PhotoFrameMod::pause(void)
{
  if(xTimer != NULL){
    xTimerStop(xTimer, 0);
    xTimerDelete(xTimer, 0);
    xTimer = NULL;
  }
  photoList.clear();
  avatar.set_isSubWindowEnable(false);
  avatar.setSpeechText("");

}


void PhotoFrameMod::btnA_pressed(void)   // 왼쪽 탭 = 이전 사진
{
  if (!photoList.empty()) g_photoIdx = (g_photoIdx + photoList.size() - 1) % photoList.size();
  updatePhoto();
}


void PhotoFrameMod::btnB_pressed(void)
{

}

void PhotoFrameMod::btnC_pressed(void)   // 오른쪽 탭 = 다음 사진
{
  if (!photoList.empty()) g_photoIdx = (g_photoIdx + 1) % photoList.size();
  updatePhoto();
}

void PhotoFrameMod::display_touched(int16_t x, int16_t y)
{
  if (box_stt.contain(x, y))
  {
    sw_tone();
  }

  if (box_servo.contain(x, y))
  {
    //servo_home = !servo_home;
    sw_tone();
  }

  if (box_BtnA.contain(x, y))
  {
    btnA_pressed();
  }

  if (box_BtnC.contain(x, y))
  {
    btnC_pressed();
  }

}

void PhotoFrameMod::idle(void)
{

  if (photoFrameTimerCallbacked) {
    photoFrameTimerCallbacked = false;
    if (!photoList.empty()) g_photoIdx = (g_photoIdx + 1) % photoList.size();   // auto-advance
    updatePhoto();
    if (xTimer) xTimerStart(xTimer, 0);   // re-arm one-shot timer
  }
}


String PhotoFrameMod::getNextPhoto(){
  if (photoList.empty()) return String();   // no photos — caller must guard (deque[0] on empty = UB → crash)
  String fname = photoList[0];
  photoList.pop_front();
  photoList.push_back(fname);
  return fname;
}

void PhotoFrameMod::createPhotoList(File dir) {
  Serial.println("Creating photo list");
  if (!dir) {   // SD folder /app/AiStackChanEx/photo missing → invalid File, don't iterate
    Serial.println("[photo] /app/AiStackChanEx/photo not found on SD");
    return;
  }
  while(true) {
    File entry = dir.openNextFile();
    if (! entry) {
      Serial.println("no more files");
      return;
    }

    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println(" (this is a directory)");
    } else {
      // Store the BASENAME only. ESP32 SD File::name() may return the full path
      // ("/app/.../pri/img.jpg") depending on core version; appending that to
      // pf_base_dir() again gives a broken path → "불러오기 실패". Strip to basename.
      // Only JPEG (PNG/other crash the JPG decoder); skip macOS/hidden ._ files.
      String orig = String(entry.name());
      int sl = orig.lastIndexOf('/');
      String base = (sl >= 0) ? orig.substring(sl + 1) : orig;
      String low = base; low.toLowerCase();
      bool isJpg = low.endsWith(".jpg") || low.endsWith(".jpeg");
      bool isHidden = base.startsWith(".") || base.startsWith("_");
      if (isJpg && !isHidden) {
        photoList.push_back(base);
        Serial.printf(" (added: %s)\n", base.c_str());
      } else {
        Serial.println(" (skipped: not .jpg)");
      }
    }
  }
}

void PhotoFrameMod::updatePhoto(){
  if (photoList.empty()) {   // nothing to show — show a notice instead of crashing
    avatar.set_isSubWindowEnable(false);
    avatar.setSpeechText((String("사진 없음 (SD ") + pf_base_dir() + ")").c_str());
    return;
  }
  if (g_photoIdx >= photoList.size()) g_photoIdx = 0;
  String fname = pf_base_dir() + "/" + photoList[g_photoIdx];   // index-based (prev/next navigation)
  Serial.printf("Next photo : %s\n", fname.c_str());

  // CoreS3: SDとLCDがSPIバス(GPIO35=MISO/DC)を共有するため、SD読込中は
  // アバターの描画タスクを止めてGPIO35をMISO入力に固定する(でないと描画と衝突して
  // "does not exist"/ff_sd_status で読み込み失敗する)。
  // SAFETY: a 320x240 JPEG is ~10-30KB. A full-size photo (MB) blows up the JPEG
  // decoder's internal-heap allocation → OOM. Skip oversized files (>200KB).
  bool notFound = false, oversized = false, ok = false;

  sd_bus_lock();
  {
    SD.begin(GPIO_NUM_4, SPI, 25000000);   // 念のため再マウント(他機能の SD.end() で外れていても自己復旧)
    File pf = SD.open(fname.c_str());
    if (!pf) {
      notFound = true;
    } else {
      size_t fsz = pf.size(); pf.close();
      if (fsz > 200 * 1024) oversized = true;
    }
  }
  if (!notFound && !oversized) {
    ok = avatar.updateSubWindowJpg(fname);   // copySDFileToRAM でSDから読み込む(描画は再開後)
  }
  sd_bus_unlock();

  if (notFound) {
    Serial.printf("[photo] NOT FOUND: %s\n", fname.c_str());
    avatar.set_isSubWindowEnable(false);
    avatar.setSpeechText((String("파일없음:") + photoList[g_photoIdx]).c_str());
  } else if (oversized) {
    Serial.printf("[photo] skip oversized %s\n", fname.c_str());
    avatar.set_isSubWindowEnable(false);
    avatar.setSpeechText("사진 너무 큼(320x240)");
  } else if (!ok) {
    avatar.set_isSubWindowEnable(false);
    avatar.setSpeechText("디코드 실패(표준 JPEG?)");   // 파일은 있는데 JPEG 디코드 실패(progressive/CMYK 등)
  } else {
    avatar.setSpeechText("");
    avatar.set_isSubWindowEnable(true);
  }
}