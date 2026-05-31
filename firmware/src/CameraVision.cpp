#include <Arduino.h>
#include "CameraVision.h"
#include "Robot.h"
#if defined(REALTIME_API)
#include "llm/RealtimeLLMBase.h"
#endif
#if defined(ENABLE_CAMERA)
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "SpiRamJsonDocument.h"
#include "driver/Camera.h"
#endif

extern bool isOffline;

static volatile bool s_busy = false;
bool camera_is_busy() { return s_busy; }
void camera_vision_init() {}

bool camera_vision_look(const String& hint) {
#if defined(ENABLE_CAMERA) && defined(REALTIME_API)
    if (isOffline || robot == nullptr || robot->llm == nullptr) return false;
    if (s_busy) return false;
    s_busy = true;

    String b64;
    bool ok = camera_capture_base64(b64);
    if (!ok || b64.length() == 0) { s_busy = false; Serial.println("[vision] capture failed"); return false; }

    // Build the chat/completions request by hand (the base64 image makes the
    // body large; avoid a giant ArduinoJson document for the request).
    String p = hint.length() ? hint
                             : String("이 이미지에 보이는 것을 한국어로 한 문장으로 아주 간단히 설명해줘.");
    p.replace("\\", "\\\\"); p.replace("\"", "\\\""); p.replace("\n", " ");

    String body;
    body.reserve(b64.length() + 512);
    body  = "{\"model\":\"gpt-4o-mini\",\"max_tokens\":120,\"messages\":[{\"role\":\"user\",\"content\":[";
    body += "{\"type\":\"text\",\"text\":\"";
    body += p;
    body += "\"},{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/jpeg;base64,";
    body += b64;
    body += "\"}}]}]}";
    b64 = String();   // free the capture copy

    WiFiClientSecure client;
    client.setInsecure();              // dev-level: skip CA verification (matches FunctionCall)
    HTTPClient https;
    https.setTimeout(15000);
    if (!https.begin(client, "https://api.openai.com/v1/chat/completions")) {
        s_busy = false; Serial.println("[vision] https begin failed"); return false;
    }
    https.addHeader("Content-Type", "application/json");
    https.addHeader("Authorization", String("Bearer ") + robot->llm->param.api_key);
    int code = https.POST(body);
    String resp = (code > 0) ? https.getString() : String("");
    https.end();
    body = String();
    s_busy = false;

    if (code != 200) {
        Serial.printf("[vision] HTTP %d: %s\n", code, resp.substring(0, 160).c_str());
        return false;
    }

    SpiRamJsonDocument doc(resp.length() + 1024);
    if (deserializeJson(doc, resp)) { Serial.println("[vision] response parse error"); return false; }
    const char* desc = doc["choices"][0]["message"]["content"] | "";
    if (!desc || !*desc) { Serial.println("[vision] empty content"); return false; }

    String inject = String("[방금 카메라로 본 것: ") + desc +
                    "] 이걸 가족에게 자연스럽게 한두 문장으로 말해줘. 너무 길지 않게.";
    ((RealtimeLLMBase*)robot->llm)->pushUserText(inject);
    Serial.printf("[vision] %s\n", desc);
    return true;
#else
    (void)hint;
    return false;
#endif
}
