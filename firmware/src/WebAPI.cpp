#include <ESP32WebServer.h>
#include <nvs.h>
#include "WebAPI.h"
#include "Avatar.h"
#include "llm/ChatGPT/ChatGPT.h"
#include "llm/ChatGPT/FunctionCall.h"
#include "Robot.h"
#include "MySchedule.h"
#include "Volume.h"
#include "IdleMotion.h"
#include "IdleTalk.h"
#include "PetReaction.h"
#include "TouchReaction.h"
#include "BatteryReaction.h"
#include "NightMode.h"
#include "Proximity.h"
#include "WifiConfig.h"
#include "CameraVision.h"
#include "Persona.h"
#include "Sfx.h"
#include "mod/PhotoFrame/PhotoFrameMod.h"

using namespace m5avatar;
extern Avatar avatar;
extern uint8_t m5spk_virtual_channel;
extern String STT_API_KEY;
// 웹 조이스틱 머리 수동 제어 (main.cpp 정의)
extern volatile uint32_t g_servoManualUntil;
extern volatile int g_servoManualX;
extern volatile int g_servoManualY;
#include "ServoTrim.h"   // g_servoTrimX/Y, servo_trim_save — 홈 보정
#include "Motion.h"      // 머리 움직임 녹화/저장/재생

ESP32WebServer server(80);

// C++11 multiline string constants are neato...
static const char HEAD[] PROGMEM = R"KEWL(
<!DOCTYPE html>
<html lang="ja">
<head>
  <meta charset="UTF-8">
  <title>AIｽﾀｯｸﾁｬﾝ</title>
</head>)KEWL";

static const char APIKEY_HTML[] PROGMEM = R"KEWL(
<!DOCTYPE html>
<html>
  <head>
    <meta charset="UTF-8">
    <title>APIキー設定</title>
  </head>
  <body>
    <h1>APIキー設定</h1>
    <form>
      <label for="role1">OpenAI API Key</label>
      <input type="text" id="openai" name="openai" oninput="adjustSize(this)"><br>
      <label for="role2">VoiceVox API Key</label>
      <input type="text" id="voicevox" name="voicevox" oninput="adjustSize(this)"><br>
      <label for="role3">Speech to Text API Key</label>
      <input type="text" id="sttapikey" name="sttapikey" oninput="adjustSize(this)"><br>
      <button type="button" onclick="sendData()">送信する</button>
    </form>
    <script>
      function adjustSize(input) {
        input.style.width = ((input.value.length + 1) * 8) + 'px';
      }
      function sendData() {
        // FormDataオブジェクトを作成
        const formData = new FormData();

        // 各ロールの値をFormDataオブジェクトに追加
        const openaiValue = document.getElementById("openai").value;
        if (openaiValue !== "") formData.append("openai", openaiValue);

        const voicevoxValue = document.getElementById("voicevox").value;
        if (voicevoxValue !== "") formData.append("voicevox", voicevoxValue);

        const sttapikeyValue = document.getElementById("sttapikey").value;
        if (sttapikeyValue !== "") formData.append("sttapikey", sttapikeyValue);

	    // POSTリクエストを送信
	    const xhr = new XMLHttpRequest();
	    xhr.open("POST", "/apikey_set");
	    xhr.onload = function() {
	      if (xhr.status === 200) {
	        alert("データを送信しました！");
	      } else {
	        alert("送信に失敗しました。");
	      }
	    };
	    xhr.send(formData);
	  }
	</script>
  </body>
</html>)KEWL";

#if 0
static const char ROLE_HTML[] PROGMEM = R"KEWL(
<!DOCTYPE html>
<html>
<head>
	<title>ロール設定</title>
	<meta charset="UTF-8">
	<meta name="viewport" content="width=device-width, initial-scale=1.0">
	<style>
		textarea {
			width: 80%;
			height: 200px;
			resize: both;
		}
	</style>
</head>
<body>
	<h1>ロール設定</h1>
	<form onsubmit="postData(event)">
		<label for="textarea">ここにロールを記述してください。:</label><br>
		<textarea id="textarea" name="textarea"></textarea><br><br>
		<input type="submit" value="Submit">
	</form>
	<script>
		function postData(event) {
			event.preventDefault();
			const textAreaContent = document.getElementById("textarea").value.trim();
//			if (textAreaContent.length > 0) {
				const xhr = new XMLHttpRequest();
				xhr.open("POST", "/role_set", true);
				xhr.setRequestHeader("Content-Type", "text/plain;charset=UTF-8");
			// xhr.onload = () => {
			// 	location.reload(); // 送信後にページをリロード
			// };
			xhr.onload = () => {
				document.open();
				document.write(xhr.responseText);
				document.close();
			};
				xhr.send(textAreaContent);
//        document.getElementById("textarea").value = "";
				alert("Data sent successfully!");
//			} else {
//				alert("Please enter some text before submitting.");
//			}
		}
	</script>
</body>
</html>)KEWL";
#endif

#define IMPORT_FILE(section, filename, symbol) \
static constexpr const char* filename_##symbol = filename; \
extern const uint8_t symbol[], sizeof_##symbol[]; \
asm(\
  ".section " #section "\n"\
  ".balign 4\n"\
  ".global " #symbol "\n"\
  #symbol ":\n"\
  ".incbin \"incbin/" filename "\"\n"\
  ".global sizeof_" #symbol "\n"\
  ".set sizeof_" #symbol ", . - " #symbol "\n"\
  ".balign 4\n"\
  ".section \".text\"\n")

//IMPORT_FILE(.rodata, "index.html", index_html);
IMPORT_FILE(.rodata, "personalize.html", personalize_html);
IMPORT_FILE(.rodata, "personalize.js", personalize_js);
IMPORT_FILE(.rodata, "schedules.html", schedules_html);
IMPORT_FILE(.rodata, "schedules.js", schedules_js);
IMPORT_FILE(.rodata, "settings.html", settings_html);
IMPORT_FILE(.rodata, "settings.js", settings_js);
// incbin re-embed touch: settings.html/js 수정 시 이 파일을 재컴파일해야 반영됨(v12)


// Settings page changes every firmware build; without this the browser may serve a
// stale cached settings.html/js, so edits appear to "not save". Force revalidation.
static void sendNoCacheHeaders() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
}

void handle_settings_html() {
  sendNoCacheHeaders();
  server.send_P(200, "text/html", (const char*)settings_html, (size_t)sizeof_settings_html);
}
void handle_settings_js() {
  sendNoCacheHeaders();
  server.send_P(200, "application/javascript", (const char*)settings_js, (size_t)sizeof_settings_js);
}

void handleRoot() {
  // Unified settings page is now the landing page.
  handle_settings_html();
}

void handle_personalize_html() {
  server.send_P(200, "text/html", (const char*)personalize_html, (size_t)sizeof_personalize_html);
}

void handle_personalize_js() {
  server.send_P(200, "application/javascript", (const char*)personalize_js, (size_t)sizeof_personalize_js);
}

void handle_schedules_html() {
  server.send_P(200, "text/html", (const char*)schedules_html, (size_t)sizeof_schedules_html);
}
void handle_schedules_js() {
  server.send_P(200, "application/javascript", (const char*)schedules_js, (size_t)sizeof_schedules_js);
}
void handle_schedules_get() {
  server.send(200, "application/json", get_schedules_json());
}
void handle_schedules_set() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST only");
    return;
  }
  String json = server.arg("plain");
  if (set_schedules_json(json)) {
    server.send(200, "text/plain", "prompts live now; time/weekday changes need reboot");
  } else {
    server.send(400, "text/plain", "invalid JSON or write failed");
  }
}
void handle_speak_now() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST only");
    return;
  }
  String text = server.arg("plain");
  if (text.length() == 0) {
    server.send(400, "text/plain", "empty text");
    return;
  }
  speak_now(text);
  server.send(200, "text/plain", "sent");
}

void handle_volume_get() {
  server.send(200, "text/plain", String(volume_get()));
}
void handle_volume_set() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST only");
    return;
  }
  String body = server.arg("plain");
  body.trim();
  int v = body.toInt();
  if (v < 0 || v > 255 || (v == 0 && body != "0")) {
    server.send(400, "text/plain", "expected integer 0..255");
    return;
  }
  if (volume_set(v)) {
    server.send(200, "text/plain", String(v));
  } else {
    server.send(500, "text/plain", "SPIFFS write failed");
  }
}

// ---- Idle motion (random expression/behaviour) -----------------------------
void handle_idle_get() {
  server.send(200, "application/json", idle_motion_get_json());
}
void handle_idle_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (idle_motion_set_json(server.arg("plain")))
    server.send(200, "text/plain", "ok");
  else
    server.send(400, "text/plain", "invalid JSON or write failed");
}

// ---- Proximity sensor (eyes grow) ------------------------------------------
void handle_prox_get() {
  server.send(200, "application/json", proximity_get_json());
}
void handle_prox_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (proximity_set_json(server.arg("plain")))
    server.send(200, "text/plain", "ok");
  else
    server.send(400, "text/plain", "invalid JSON or write failed");
}

// ---- Idle talk (proactive speech / jokes / greet on approach) --------------
void handle_talk_get() {
  server.send(200, "application/json", idle_talk_get_json());
}
void handle_talk_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (idle_talk_set_json(server.arg("plain")))
    server.send(200, "text/plain", "ok");
  else
    server.send(400, "text/plain", "invalid JSON or write failed");
}

// ---- Pet reaction (IMU stroke/lift) ----------------------------------------
void handle_pet_get() {
  server.send(200, "application/json", pet_reaction_get_json());
}
void handle_pet_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (pet_reaction_set_json(server.arg("plain")))
    server.send(200, "text/plain", "ok");
  else
    server.send(400, "text/plain", "invalid JSON or write failed");
}

// ---- Touch reaction (look-toward-touch / stroke) ---------------------------
void handle_touch_get() {
  server.send(200, "application/json", touch_reaction_get_json());
}
void handle_touch_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (touch_reaction_set_json(server.arg("plain")))
    server.send(200, "text/plain", "ok");
  else
    server.send(400, "text/plain", "invalid JSON or write failed");
}

// ---- Battery / charging reaction -------------------------------------------
void handle_batt_get() {
  server.send(200, "application/json", battery_reaction_get_json());
}
void handle_batt_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (battery_reaction_set_json(server.arg("plain")))
    server.send(200, "text/plain", "ok");
  else
    server.send(400, "text/plain", "invalid JSON or write failed");
}

// ---- Night mode ------------------------------------------------------------
void handle_night_get() {
  server.send(200, "application/json", night_mode_get_json());
}
void handle_night_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (night_mode_set_json(server.arg("plain")))
    server.send(200, "text/plain", "ok");
  else
    server.send(400, "text/plain", "invalid JSON or write failed");
}

// ---- WiFi config (apply on reboot) -----------------------------------------
void handle_wifi_get() {
  server.send(200, "application/json", wifi_get_json());
}
void handle_wifi_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (wifi_set_json(server.arg("plain")))
    server.send(200, "text/plain", "saved; reboot to apply");
  else
    server.send(400, "text/plain", "invalid JSON or write failed");
}
void handle_wifi_scan() {
  server.send(200, "application/json", wifi_scan_json());
}
void handle_reboot() {
  server.send(200, "text/plain", "rebooting");
  delay(400);
  ESP.restart();
}

// ---- Camera vision (GPT-4o "look around") ----------------------------------
void handle_look() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  String hint = server.arg("plain");   // optional custom prompt
  if (camera_vision_look(hint))
    server.send(200, "text/plain", "looked");
  else
    server.send(503, "text/plain", "camera/vision unavailable (camera build + online required)");
}

// ---- 웹 조이스틱 머리 수동 제어 (/servo_move, body "x,y" 각 -1..1) ----
#define SERVO_MANUAL_MAX_PAN   40
#define SERVO_MANUAL_MAX_TILT  30
void handle_servo_move() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  String body = server.arg("plain"); body.trim();
  int comma = body.indexOf(',');
  if (comma < 0) { server.send(400, "text/plain", "expected x,y"); return; }
  float x = body.substring(0, comma).toFloat();
  float y = body.substring(comma + 1).toFloat();
  if (x < -1) x = -1; else if (x > 1) x = 1;
  if (y < -1) y = -1; else if (y > 1) y = 1;
  g_servoManualX = (int)(x * SERVO_MANUAL_MAX_PAN);
  g_servoManualY = (int)(y * SERVO_MANUAL_MAX_TILT);
  g_servoManualUntil = millis() + 2000;   // 2초간 갱신 없으면 자동 해제(평소 동작 복귀)
  server.send(200, "text/plain", "ok");
}

// 현재 수동 위치를 홈(중앙)으로 저장 — 비뚤어진 홈 보정
void handle_servo_home_save() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  int nx = g_servoTrimX + g_servoManualX;   // 현재 수동 오프셋을 트림에 흡수
  int ny = g_servoTrimY + g_servoManualY;
  if (servo_trim_save(nx, ny)) {
    g_servoManualX = 0; g_servoManualY = 0;        // 흡수 후 매뉴얼 0 → 새 중앙(점프 없음)
    g_servoManualUntil = millis() + 1500;
    server.send(200, "text/plain", String("ok trim x=") + nx + " y=" + ny);
  } else server.send(500, "text/plain", "save failed");
}

void handle_servo_home_reset() {
  servo_trim_save(0, 0);
  g_servoManualX = 0; g_servoManualY = 0;
  g_servoManualUntil = millis() + 1500;
  server.send(200, "text/plain", "reset");
}

// ---- 머리 움직임 녹화/재생 ----
void handle_motion_save() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (motion_save_json(server.arg("plain"))) server.send(200, "text/plain", "ok");
  else server.send(400, "text/plain", "save failed");
}
void handle_motions_get() { server.send(200, "application/json", motions_list_json()); }
void handle_motion_delete() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  String name = server.arg("plain"); name.trim();
  bool ok = motion_delete(name);
  server.send(ok ? 200 : 404, "text/plain", ok ? "ok" : "not found");
}
void handle_motion_play() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  String name = server.arg("plain"); name.trim();
  if (motion_play(name)) server.send(200, "text/plain", "ok");
  else server.send(409, "text/plain", "busy/manual/not found");
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
//  server.send(404, "text/plain", message);
  server.send(404, "text/html", String(HEAD) + String("<body>") + message + String("</body>"));
}

void handle_speech() {
  String message = server.arg("say");
  String speaker = server.arg("voice");
  //if(speaker != "") {
  //  TTS_PARMS = TTS_SPEAKER + speaker;
  //}
  Serial.println(message);
  ////////////////////////////////////////
  // 音声の発声
  ////////////////////////////////////////
  //avatar.setExpression(Expression::Happy);
  robot->speech(message);
  server.send(200, "text/plain", String("OK"));
}

void handle_chat() {
  static String response = "";
  // tts_parms_no = 1;
  String text = server.arg("text");
  String speaker = server.arg("voice");
  //if(speaker != "") {
  //  TTS_PARMS = TTS_SPEAKER + speaker;
  //}

  robot->chat(text);

  server.send(200, "text/html", String(HEAD)+String("<body>")+response+String("</body>"));
}

void handle_apikey() {
  // ファイルを読み込み、クライアントに送信する
  server.send(200, "text/html", APIKEY_HTML);
}

#if 0
void handle_apikey_set() {
  // POST以外は拒否
  if (server.method() != HTTP_POST) {
    return;
  }
  // openai
  String openai = server.arg("openai");
  // voicetxt
  String voicevox = server.arg("voicevox");
  // voicetxt
  String sttapikey = server.arg("sttapikey");
 
  OPENAI_API_KEY = openai;
  VOICEVOX_API_KEY = voicevox;
  STT_API_KEY = sttapikey;
  Serial.println(openai);
  Serial.println(voicevox);
  Serial.println(sttapikey);

  uint32_t nvs_handle;
  if (ESP_OK == nvs_open("apikey", NVS_READWRITE, &nvs_handle)) {
    nvs_set_str(nvs_handle, "openai", openai.c_str());
    nvs_set_str(nvs_handle, "voicevox", voicevox.c_str());
    nvs_set_str(nvs_handle, "sttapikey", sttapikey.c_str());
    nvs_close(nvs_handle);
  }
  server.send(200, "text/plain", String("OK"));
}
#endif

void handle_role_set() {
  String html = "";

  // POST以外は拒否
  if (server.method() != HTTP_POST) {
    return;
  }
  String role = server.arg("plain");

  // JSONデータをSPIFFSに保存
  if(robot->llm->save_userRole(role)){
#if 0
    // 整形したJSONデータを出力するHTMLデータを作成する
    serializeJsonPretty(robot->llm->get_chat_doc(), html);
    html = "<html><body><pre>" + html + "</pre></body></html>";
    //Serial.println(html);
#endif
    server.send(200, "text/plain", String("Role set successful"));
  }
  else{
    //html = "Failed to save role to SPIFFS.";
    server.send(500, "text/plain", String("Role set failed"));
  }

  // HTMLデータをシリアルに出力する
  //server.send(200, "text/html", html);
};

void handle_role_get() {
#if 0
  String html = "";
  serializeJsonPretty(robot->llm->get_chat_doc(), html);
  html = "<html><body><pre>" + html + "</pre></body></html>";

  // HTMLデータをシリアルに出力する
  //Serial.println(html);
  server.send(200, "text/html", String(HEAD) + html);
#endif
  Serial.println("http request: handle_role_get");
  Serial.println(robot->llm->get_userRole());
  server.send(200, "text/plain", robot->llm->get_userRole());
};

void handle_sfx_get() {
  sendNoCacheHeaders();
  server.send(200, "application/json", sfx_get_json());
}
void handle_sfx_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (sfx_set_json(server.arg("plain"))) server.send(200, "text/plain", "ok");
  else                                   server.send(400, "text/plain", "invalid JSON");
}
void handle_sfx_test() {
  // POST body = MP3 filename (e.g. "pet.mp3"); plays /app/AiStackChanEx/sfx/<file>
  String file = server.arg("plain"); file.trim();
  bool ok = sfx_play_file(file.c_str());
  server.send(200, "text/plain", ok ? "played" : "skipped (busy/missing/disabled)");
}

void handle_photo_get() {
  sendNoCacheHeaders();
  server.send(200, "application/json", photoframe_get_json());
}
void handle_photo_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  if (photoframe_set_json(server.arg("plain"))) server.send(200, "text/plain", "ok");
  else                                          server.send(400, "text/plain", "invalid JSON");
}

void handle_persona_get() {
  sendNoCacheHeaders();
  server.send(200, "application/json", persona_get_json());
}
void handle_persona_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  String body = server.arg("plain");
  if (persona_set_json(body)) server.send(200, "text/plain", "ok");
  else                        server.send(400, "text/plain", "invalid JSON");
}

void handle_memory_get() {
  Serial.println("http request: handle_memory_get");
  Serial.println(robot->llm->get_userInfo());
  server.send(200, "text/plain", robot->llm->get_userInfo());
};

void handle_memory_set() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "POST only"); return; }
  String info = server.arg("plain");
  // save_userInfo prepends "User Info: " itself; strip it if the client kept it.
  if (info.startsWith("User Info:")) info = info.substring(String("User Info:").length());
  info.trim();
  if (robot->llm->save_userInfo(info)) server.send(200, "text/plain", "ok");
  else                                 server.send(500, "text/plain", "save failed");
}

void handle_memory_clear() {
  Serial.println("http request: handle_memory_clear");
  bool result = robot->llm->clear_userInfo();
  if(result){
    server.send(200, "text/plain", String("Memory clear successful"));
  }else{
    server.send(500, "text/plain", String("Memory clear failed"));
  }
};

void handle_face() {
  String expression = server.arg("expression");
  expression = expression + "\n";
  Serial.println(expression);
  switch (expression.toInt())
  {
    case 0: avatar.setExpression(Expression::Neutral); break;
    case 1: avatar.setExpression(Expression::Happy); break;
    case 2: avatar.setExpression(Expression::Sleepy); break;
    case 3: avatar.setExpression(Expression::Doubt); break;
    case 4: avatar.setExpression(Expression::Sad); break;
    case 5: avatar.setExpression(Expression::Angry); break;  
  } 
  server.send(200, "text/plain", String("OK"));
}

#if 0
void handle_setting() {
  String value = server.arg("volume");
  String led = server.arg("led");
  String speaker = server.arg("speaker");
//  volume = volume + "\n";
  Serial.println(speaker);
  Serial.println(value);
  size_t speaker_no;

  if(speaker != ""){
    speaker_no = speaker.toInt();
    if(speaker_no > 60) {
      speaker_no = 60;
    }
    TTS_SPEAKER_NO = String(speaker_no);
    TTS_PARMS = TTS_SPEAKER + TTS_SPEAKER_NO;
  }

  if(value == "") value = "180";
  size_t volume = value.toInt();
  uint8_t led_onoff = 0;
  uint32_t nvs_handle;
  if (ESP_OK == nvs_open("setting", NVS_READWRITE, &nvs_handle)) {
    if(volume > 255) volume = 255;
    nvs_set_u32(nvs_handle, "volume", volume);
    if(led != "") {
      if(led == "on") led_onoff = 1;
      else  led_onoff = 0;
      nvs_set_u8(nvs_handle, "led", led_onoff);
    }
    nvs_set_u8(nvs_handle, "speaker", speaker_no);

    nvs_close(nvs_handle);
  }
  M5.Speaker.setVolume(volume);
  M5.Speaker.setChannelVolume(m5spk_virtual_channel, volume);
  server.send(200, "text/plain", String("OK"));
}
#endif


void init_web_server(void)
{
  // Files
  //
  server.on("/", handleRoot);
  // Unified settings page (persona + schedule + volume + idle motion + proximity + wifi)
  server.on("/settings.html", handle_settings_html);
  server.on("/settings.js", handle_settings_js);
  // Legacy URLs now redirect to the unified page so old bookmarks still work.
  server.on("/personalize.html", handle_settings_html);
  server.on("/personalize.js", handle_personalize_js);
  server.on("/schedules.html", handle_settings_html);
  server.on("/schedules.js", handle_schedules_js);
  server.on("/schedules_get", handle_schedules_get);
  server.on("/schedules_set", HTTP_POST, handle_schedules_set);
  server.on("/speak_now", HTTP_POST, handle_speak_now);
  server.on("/volume_get", handle_volume_get);
  server.on("/volume_set", HTTP_POST, handle_volume_set);
  // Idle motion
  server.on("/idle_get", handle_idle_get);
  server.on("/idle_set", HTTP_POST, handle_idle_set);
  // Proximity sensor
  server.on("/prox_get", handle_prox_get);
  server.on("/prox_set", HTTP_POST, handle_prox_set);
  // Idle talk / pet reaction / night mode
  server.on("/talk_get", handle_talk_get);
  server.on("/talk_set", HTTP_POST, handle_talk_set);
  server.on("/pet_get", handle_pet_get);
  server.on("/pet_set", HTTP_POST, handle_pet_set);
  server.on("/touch_get", handle_touch_get);
  server.on("/touch_set", HTTP_POST, handle_touch_set);
  server.on("/batt_get", handle_batt_get);
  server.on("/batt_set", HTTP_POST, handle_batt_set);
  server.on("/night_get", handle_night_get);
  server.on("/night_set", HTTP_POST, handle_night_set);
  // WiFi config (apply on reboot)
  server.on("/wifi_get", handle_wifi_get);
  server.on("/wifi_set", HTTP_POST, handle_wifi_set);
  server.on("/wifi_scan", handle_wifi_scan);
  server.on("/reboot", HTTP_POST, handle_reboot);
  // Camera vision
  server.on("/look", HTTP_POST, handle_look);
  server.on("/servo_move", HTTP_POST, handle_servo_move);   // 웹 조이스틱 머리 제어
  server.on("/servo_home_save", HTTP_POST, handle_servo_home_save);    // 현재 위치를 홈으로
  server.on("/servo_home_reset", HTTP_POST, handle_servo_home_reset);  // 홈 초기화
  server.on("/motion_save", HTTP_POST, handle_motion_save);    // 머리 움직임 저장
  server.on("/motions_get", handle_motions_get);               // 저장 목록
  server.on("/motion_delete", HTTP_POST, handle_motion_delete);
  server.on("/motion_play", HTTP_POST, handle_motion_play);     // 재생


  // APIs
  //
  server.on("/speech", handle_speech);
  server.on("/face", handle_face);
  server.on("/chat", handle_chat);
  server.on("/apikey", handle_apikey);
  //server.on("/setting", handle_setting);
  //server.on("/apikey_set", HTTP_POST, handle_apikey_set);
  server.on("/role_set", HTTP_POST, handle_role_set);
  server.on("/role_get", handle_role_get);
  server.on("/persona_get", handle_persona_get);
  server.on("/persona_set", HTTP_POST, handle_persona_set);
  server.on("/sfx_get", handle_sfx_get);
  server.on("/sfx_set", HTTP_POST, handle_sfx_set);
  server.on("/sfx_test", HTTP_POST, handle_sfx_test);
  server.on("/photo_get", handle_photo_get);
  server.on("/photo_set", HTTP_POST, handle_photo_set);
  server.on("/memory_get", handle_memory_get);
  server.on("/memory_set", HTTP_POST, handle_memory_set);
  server.on("/memory_clear", handle_memory_clear);

  // Other
  //
  server.onNotFound(handleNotFound);
  server.on("/inline", [](){
    server.send(200, "text/plain", "this works as well");
  });

  server.begin();
  Serial.println("HTTP server started");
  M5.Lcd.println("HTTP server started");  
}

void web_server_handle_client(void)
{
  server.handleClient();
}
