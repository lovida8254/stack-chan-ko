#if defined(REALTIME_API)

#include <Arduino.h>
#include <M5Unified.h>
#include <Avatar.h>
#include "share/Mutex.h"
//#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "rootCA/rootCACertificate.h"
#include <ArduinoJson.h>
#include "SpiRamJsonDocument.h"
#include "RealtimeChatGPT.h"
#include "FunctionCall.h"
#include "MCPClient.h"
#include "Robot.h"
#include "DiagLog.h"

#include <base64.h>
#include "libb64/cdecode.h"
#include <WebSocketsClient.h>

using namespace m5avatar;
extern Avatar avatar;

static const char session_update[] =
      "{"
        "\"type\": \"session.update\","
        "\"session\": {"
          "\"type\": \"realtime\","
          "\"model\": \"gpt-realtime\","
#ifdef REALTIME_API_WITH_TTS
          "\"output_modalities\": [\"text\"],"
#else
          "\"output_modalities\": [\"audio\"],"
#endif
          "\"audio\": {"
            "\"input\": {"
              "\"format\": {"
                "\"type\": \"audio/pcm\","
                "\"rate\": 24000"
              "},"
              "\"turn_detection\": {"
                "\"type\": \"semantic_vad\""
              "}"
            "},"
            "\"output\": {"
              "\"format\": {"
                "\"type\": \"audio/pcm\","
                "\"rate\": 24000"
              "},"
              //"\"voice\": \"sage\""
              "\"voice\": \"marin\""
            "}"
          "},"
          "\"instructions\": \"You are a friendly robot named 스택짱. Respond only in Korean.\","
          "\"max_output_tokens\": 2500,"
          "\"tools\":[]"
        "}"
      "}";


static const char input_audio_append[] =
        "{"
          "\"type\": \"input_audio_buffer.append\","
          "\"audio\": \"REPLACE_TO_AUDIO_BASE64\""
        "}";

// for function calling
//
static const char conversation_item_create[] =
        "{"
            "\"type\": \"conversation.item.create\","
            "\"item\": {"
                "\"type\": \"function_call_output\","
                "\"call_id\": \"REPLACE_TO_CALL_ID\","
                "\"output\": \"{\\\"result\\\":\\\"REPLACE_TO_OUTPUT\\\"}\""
            "}"
        "}";

static const char response_create[] =
        "{"
            "\"type\": \"response.create\""
        "}";

// WebSocketのコールバック関数としてクラスメソッドを渡せないので、コールバック関数を
// 通常の関数にして静的変数を経由してクラスのthisポインタを渡す。
static RealtimeChatGPT* p_this;

// Watchdog: heartbeat catches dead TCP/PING but the server occasionally accepts
// user audio, sends back PING/PONG fine, yet never produces response.done
// (more frequent on gpt-realtime full model once context grows past ~3k tokens).
// If we recorded a user commit but never got response.done within the timeout,
// force-disconnect → setReconnectInterval kicks in → fresh session.
// NOTE: the actual timeout check + disconnect now runs inside onWebSocketTick()
// on the WebSocket task — it must NOT be done from a separate task, since calling
// webSocket.disconnect() concurrently with webSocket.loop() corrupts the TLS
// stream ("SSL MAC verification failed" → null-PC crash in the ws_wdt task).
static volatile uint32_t last_commit_time = 0;
static const uint32_t RESPONSE_TIMEOUT_MS = 35000;

static void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
    String msgType, delta;
    DeserializationError error;

	switch(type) {
		case WStype_DISCONNECTED:
			Serial.printf("[WSc] Disconnected!\n");
			g_ws_connected = false;
			diag_log("WS disconnected (speaking=%d)", p_this->speaking ? 1 : 0);
			// If we disconnect MID-OUTPUT (speaking==true) the mutexAudio + speaker
			// were claimed by the committed/proactive handoff and would normally be
			// released at response.done. A disconnect skips that → mutexAudio (a
			// non-recursive mutex) stays locked → the next turn's enterMutexAudio()
			// on this same task deadlocks = silent hang ("듣는 중" frozen, no panic).
			// Clean the audio state here so every reconnect starts fresh.
			if (p_this->speaking) {
				p_this->speaking = false;
#ifndef REALTIME_API_WITH_TTS
				M5.Speaker.end();
				M5.Mic.begin();
				exitMutexAudio();
#endif
				Serial.println("[WSc] disconnect mid-speech — audio state reset (mutex released)");
			}
			last_commit_time = 0;
			break;
		case WStype_CONNECTED:
			Serial.printf("[WSc] Connected to url: %s\n", payload);
			g_ws_connected = true;
			diag_log("WS connected");

            /*
             * session.updateでAPIの振る舞いをカスタマイズする
             */
            {
                SpiRamJsonDocument sessionUpdateDoc(1024*10);
                DeserializationError error = deserializeJson(sessionUpdateDoc, session_update);
                if (error) {
                    Serial.println("webSocketEvent: JSON deserialization error (session_update)");
                }

                // instructionsにロール、前回会話の要約を設定
                //
                Serial.printf("role: %s\n", p_this->role.c_str());
                Serial.printf("sysRole: %s\n", p_this->systemRole.c_str());
                Serial.printf("userInfo: %s\n", p_this->userInfo.c_str());
                sessionUpdateDoc["session"]["instructions"] = p_this->role + " "
                                                              + p_this->systemRole + " "
                                                              + p_this->userInfo;

                // MCP tools listをfunctionとして挿入
                //
                for(int s=0; s < p_this->param.llm_conf.nMcpServers; s++){
                    if(true == p_this->param.llm_conf.mcpServer[s].disabled){
                        continue;
                    }
                    if(!p_this->mcpClient[s]->isConnected()){
                        continue;
                    }

                    for(int t=0; t < p_this->mcpClient[s]->nTools; t++){
                        sessionUpdateDoc["session"]["tools"].add(p_this->mcpClient[s]->toolsListDoc["result"]["tools"][t]);
                        sessionUpdateDoc["session"]["tools"][t]["type"] = "function";
                    }
                }

                // FunctionCall.cppで定義したfunctionをsession.updateに挿入
                //
                SpiRamJsonDocument functionsDoc(1024*10);
                error = deserializeJson(functionsDoc, json_Functions.c_str());
                if (error) {
                    Serial.println("FunctionCall: JSON deserialization error");
                }

                int nFuncs = functionsDoc.size();
                int nMcpFuncs = sessionUpdateDoc["session"]["tools"].size();
                for(int i=0; i<nFuncs; i++){
                    sessionUpdateDoc["session"]["tools"].add(functionsDoc[i]);
                    sessionUpdateDoc["session"]["tools"][nMcpFuncs + i]["type"] = "function";
                }

                String sessionUpdateStr;
                serializeJson(sessionUpdateDoc, sessionUpdateStr);
                String jsonPretty;
                serializeJsonPretty(sessionUpdateDoc, jsonPretty);
                Serial.printf("[WSc] session update json: %s\n", jsonPretty.c_str());
                p_this->webSocket.sendTXT(sessionUpdateStr.c_str());
            }
			break;
		case WStype_TEXT:
			// Per-message logging removed: these fired on EVERY audio.delta (~25-50/sec)
			// and blocked the WebSocket task on USB-CDC writes → choppy audio.
			//Serial.printf("[WSc] get text: %s\n", payload);
			//Serial.printf("[WSc] text size: %d\n", strlen((char*)payload));

            error = deserializeJson(p_this->msgDoc, payload);
            if (error) {
                Serial.printf("WebSocket Event: JSON deserialization error %d\n", error.code());
            }

            msgType = p_this->msgDoc["type"].as<String>();
            //Serial.printf("[WSc] text type: %s\n", msgType.c_str());

            if(msgType.equals("session.updated")){
                Serial.printf("[WSc] payload: %s\n", payload);
                avatar.setSpeechText("터치해서 시작");
            }
            else if(msgType.equals("input_audio_buffer.speech_started")){
                p_this->resetRealtimeRecordStartTime();
            }
            else if(msgType.equals("input_audio_buffer.committed")){
                Serial.printf("[WSc] input audio committed\n");
                last_commit_time = millis();
                p_this->stopRealtimeRecord();
#ifndef REALTIME_API_WITH_TTS
                enterMutexAudio();
                M5.Mic.end();
                M5.Speaker.begin();
                p_this->speaking = true;
#else
                p_this->speaking = true;
#endif
            }
#ifndef REALTIME_API_WITH_TTS            
            else if(msgType.equals("response.output_audio_transcript.delta")){
                delta = p_this->msgDoc["delta"].as<String>();
                // transcript delta log silenced (was per-chunk → blocked task during long speech)
                //Serial.printf("[WSc] delta: %s\n", delta.c_str());
            }
            else if(msgType.equals("response.output_audio.delta")){
                delta = p_this->msgDoc["delta"].as<String>();
                p_this->streamAudioDelta(delta);
            }
#else
            else if(msgType.equals("response.output_text.delta")){
                p_this->outputText += p_this->msgDoc["delta"].as<String>();

                // 区切り文字を検出したらテキストをキューに追加
                int idx = p_this->search_delimiter(p_this->outputText);
                if(idx > 0){
                    String inputText = p_this->outputText.substring(0, idx);
                    Serial.printf("[WSc] Push text: %s\n", inputText.c_str());
                    p_this->outputTextQueue.push_back(inputText);
                    p_this->outputText = p_this->outputText.substring(idx + strlen("。"), p_this->outputText.length());
                }
            }
#endif
            else if(msgType.equals("response.done")){
                last_commit_time = 0;
                Serial.printf("[WSc] response.done payload: %s\n", payload);
                Serial.printf("[heap] DMA=%u SPIRAM_free=%u SPIRAM_largest=%u INTERNAL_free=%u\n",
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                    (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM),
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
                int outputNum = p_this->msgDoc["response"]["output"].size();
                Serial.printf("output num: %d\n", outputNum);
                bool isFuncCall = false;
                for(int i = 0; i < outputNum; i++){
                    String outputType = p_this->msgDoc["response"]["output"][i]["type"].as<String>();
                    if(outputType.equals("function_call")){
                        //Serial.printf("[WSc] function call payload: %s\n", payload);
                        isFuncCall = true;
                        const char* name = p_this->msgDoc["response"]["output"][i]["name"];
                        const char* args = p_this->msgDoc["response"]["output"][i]["arguments"];
                        const char* call_id = p_this->msgDoc["response"]["output"][i]["call_id"];
                        Serial.printf("name: %s, args: %s\n", name, args);

                        //avatar.setSpeechFont(&fonts::efontJA_12);
                        //avatar.setSpeechText(name);
                        String response = p_this->fnCall->exec_calledFunc(name, args);
                        response.replace("\"", "\\\"");     //JSON内の文字列を囲む"にエスケープ(\)を付ける

                        String json(conversation_item_create);
                        json.replace("REPLACE_TO_CALL_ID", call_id);
                        json.replace("REPLACE_TO_OUTPUT", response.c_str());
                        Serial.printf("[WSc] function output: %s\n", json.c_str());
                        p_this->webSocket.sendTXT(json);
                        p_this->webSocket.sendTXT(response_create);
                    }
                }

                if(!isFuncCall){
#ifndef REALTIME_API_WITH_TTS
                    while (M5.Speaker.isPlaying()) { vTaskDelay(1); }
                    M5.Speaker.end();
                    M5.Mic.begin();
                    exitMutexAudio();
                    p_this->startRealtimeRecord();

                    for(int i=0; i<2; i++){
                        memset(p_this->audioBuf[i], 0, 100 * 1024);
                    }
                    p_this->speaking = false;
#else
                    p_this->response_done = true;
#endif
                }
            }
            else if(msgType.equals("rate_limits.updated")){
                //Serial.printf("[WSc] payload: %s\n", payload);
            }
            else if(msgType.equals("error")){
                Serial.printf("[WSc] payload: %s\n", payload);
            }

			break;
		case WStype_BIN:
			Serial.printf("[WSc] get binary length: %u\n", length);
			p_this->hexdump(payload, length);
			break;
		case WStype_ERROR:
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
 			Serial.printf("[WSc] payload: %s\n", payload);		
            break;
        default:
			Serial.printf("[WSc] Unknown event\n");
            //Serial.printf("[WSc] payload: %s\n", payload);
            break;
	}

}


RealtimeChatGPT::RealtimeChatGPT(llm_param_t param)
  : RealtimeLLMBase(param),
    role(""),
    userInfo("User Info: "),
    systemRole("")
{
  p_this = this;    //コールバック関数に静的変数経由でthisポインタを渡す
  msgDoc = SpiRamJsonDocument(1024*150);
  
  initMcpClientList(mcpClient, param.llm_conf.mcpServer, param.llm_conf.nMcpServers);
  fnCall = new FunctionCall(param, this, mcpClient);
  //fnCall->init_func_call_settings(robot->m_config);

  if(proactiveMux == NULL) proactiveMux = xSemaphoreCreateMutex();

  enableMemory(param.llm_conf.enableMemory);
  if(enableMemory()){
    Serial.println("Memory is enabled");
    M5.Lcd.println("Memory is enabled");
  }
  load_role();


  // WebSocket connect
  //
  avatar.setSpeechText("연결 중...");
  webSocket.beginSslWithCA("api.openai.com", 443, "/v1/realtime?model=gpt-realtime", root_ca_openai);

  // event handler
  p_this = this;    //コールバック関数に静的変数経由でthisポインタを渡す
  webSocket.onEvent(webSocketEvent);
  String auth = "Bearer " + param.api_key;
  webSocket.setAuthorization(auth.c_str());

  // Keep-alive + auto-reconnect: gpt-realtime full model occasionally goes
  // idle/silent; without heartbeat the firmware doesn't notice the dead
  // socket and the touch handler hangs. Ping every 15s, expect pong within
  // 5s, disconnect after 2 misses, then auto-reconnect after 5s.
  webSocket.enableHeartbeat(15000, 5000, 2);
  webSocket.setReconnectInterval(5000);

  // Response-timeout watchdog now runs inside onWebSocketTick() on the WebSocket
  // task (no separate task touching webSocket — see note above last_commit_time).

  // try ever 5000 again if connection has failed
  webSocket.setReconnectInterval(5000);

}


void RealtimeChatGPT::load_role(){
  Serial.println("Load role from SPIFFS.");
  if(enableMemory()){
    systemRole = systemRole_memory;
  }else{
    systemRole = systemRole_noMemory;
  }
  systemRole += " " + systemRole_realtimeAvatarExpression;

  if(load_system_prompt_from_spiffs()){
    role = String((const char*)systemPrompt["messages"][SYSTEM_PROMPT_INDEX_USER_ROLE]["content"]);
    //Serial.printf("role length: %d\n", role.length());
    if (role == "") {
      Serial.println("SPIFFS user role is empty. set default role.");
      role = defaultRole;
    }

    userInfo = String((const char*)systemPrompt["messages"][SYSTEM_PROMPT_INDEX_USER_INFO]["content"]);
    //Serial.println(userInfo);
    int idx = userInfo.indexOf("User Info");
    if(idx < 0 || !enableMemory()){
      userInfo = "User Info: ";
    }
  }else{
    // load_system_prompt_from_spiffs()内でSPIFFSからの取得失敗かつ
    // デフォルトのシステムプロンプト設定に失敗した場合（通常起こり得ない）。
    role = defaultRole;
    userInfo = "User Info: ";
  }
}

String& RealtimeChatGPT::buildInputAudioJson(String& jsonBuf, String& base64)
{
    jsonBuf.concat(input_audio_append);
    jsonBuf.replace("REPLACE_TO_AUDIO_BASE64", base64);
    //Serial.println(jsonBuf);
    return jsonBuf;
}


// Proactive speech: queue a user-role message; onWebSocketTick() (WS task) sends it
// and triggers a response. Used by scheduled greetings, idle talk, pet/touch/battery/
// proximity reactions and camera vision — all of which run on the loop/HTTP/scheduler
// task, NOT the WebSocket task. We therefore only ENQUEUE here: calling sendTXT from a
// different task than the one running webSocket.loop() corrupts the TLS stream.
void RealtimeChatGPT::pushUserText(const String& text) {
  if (proactiveMux == NULL) return;   // not constructed yet
  xSemaphoreTake(proactiveMux, portMAX_DELAY);
  if (hasPendingProactive) {
    // A proactive utterance is already queued and not yet sent — coalesce by
    // dropping the newer one rather than growing an unbounded backlog.
    Serial.printf("[realtime] proactive busy, dropping: %s\n", text.c_str());
  } else {
    pendingProactiveText = text;
    hasPendingProactive = true;
  }
  xSemaphoreGive(proactiveMux);
}

// Request a session reconnect (handled on the WS task in onWebSocketTick).
void RealtimeChatGPT::requestReconnect() {
  reconnectRequest = true;
}

// Runs on the WebSocket task each iteration (after webSocket.loop()). This is the only
// place outside the event callback that may touch webSocket, so both the response
// watchdog and the proactive-send live here — never on another task.
void RealtimeChatGPT::onWebSocketTick() {
  // (0) Persona switch requested: drop the session so it reconnects with the new
  // role (load_role() already updated p_this->role before this flag was set).
  if (reconnectRequest) {
    reconnectRequest = false;
    last_commit_time = 0;
    Serial.println("[persona] reconnecting to apply new persona");
    diag_log("persona reconnect");
    webSocket.disconnect();
    return;
  }

  // (1) Response-timeout watchdog. If a turn/proactive request never produced a
  // response.done within the timeout, force a reconnect for a fresh session.
  uint32_t t = last_commit_time;
  if (t != 0 && (millis() - t) > RESPONSE_TIMEOUT_MS) {
    Serial.printf("[wdt] response timeout (%ums) — reconnect\n", (unsigned)(millis() - t));
    diag_log("wdt response timeout %ums — reconnect", (unsigned)(millis() - t));
    last_commit_time = 0;
    webSocket.disconnect();
    return;
  }

  // (2) Drain a queued proactive utterance.
  if (!hasPendingProactive) return;

  String text;
  if (proactiveMux) xSemaphoreTake(proactiveMux, portMAX_DELAY);
  text = pendingProactiveText;
  pendingProactiveText = "";
  hasPendingProactive = false;
  if (proactiveMux) xSemaphoreGive(proactiveMux);

  // Only send once the session is live; otherwise the mic→speaker handoff below
  // would never be reversed (no response → mic stuck off → robot goes deaf).
  if (!webSocket.isConnected()) {
    Serial.printf("[realtime] proactive dropped (WS not connected): %s\n", text.c_str());
    return;
  }

  String escaped = text;
  escaped.replace("\\", "\\\\");
  escaped.replace("\"", "\\\"");
  escaped.replace("\n", "\\n");
  String json = "{\"type\":\"conversation.item.create\","
                "\"item\":{\"type\":\"message\",\"role\":\"user\","
                "\"content\":[{\"type\":\"input_text\",\"text\":\"";
  json += escaped;
  json += "\"}]}}";
  webSocket.sendTXT(json);

  // Proactive speech produces NO input_audio_buffer.committed event, so the
  // mic→speaker handoff that normally happens there is skipped. Without it the
  // response audio calls Speaker.playRaw() while the mic still owns the shared I2S
  // bus → "register I2S object to platform failed" → mic_task i2s_stop() null-deref
  // crash. Mirror the committed-side handoff; the non-funccall response.done reverses it.
  if (!speaking) {
#ifndef REALTIME_API_WITH_TTS
    if (realtime_recording) stopRealtimeRecord();
    enterMutexAudio();
    M5.Mic.end();
    M5.Speaker.begin();
#endif
    speaking = true;
  }

  last_commit_time = millis();   // arm the timeout watchdog for this response
  webSocket.sendTXT(response_create);
  Serial.printf("[realtime] proactive sent: %s\n", text.c_str());
}


#endif  //REALTIME_API
