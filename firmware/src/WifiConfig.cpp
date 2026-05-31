#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "WifiConfig.h"

#define WIFI_SPIFFS_PATH  "/wifi.json"

int wifi_config_get_networks(String* ssidOut, String* pwdOut, int maxN) {
    // SPIFFS may not be mounted yet this early in boot; mount idempotently.
    if (!SPIFFS.begin(true)) {
        Serial.println("[wifi-cfg] SPIFFS begin failed");
        return 0;
    }
    if (!SPIFFS.exists(WIFI_SPIFFS_PATH)) {
        Serial.println("[wifi-cfg] no /wifi.json — falling back to SD/YAML");
        return 0;
    }
    File f = SPIFFS.open(WIFI_SPIFFS_PATH, "r");
    if (!f) return 0;
    String body = f.readString();
    f.close();
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, body)) {
        Serial.println("[wifi-cfg] /wifi.json parse error");
        return 0;
    }
    JsonArray nets = doc["networks"].as<JsonArray>();
    if (nets.isNull()) return 0;
    int n = 0;
    for (JsonObject net : nets) {
        if (n >= maxN) break;
        const char* ssid = net["ssid"] | "";
        const char* pwd  = net["password"] | "";
        if (ssid && ssid[0] != '\0') {
            ssidOut[n] = String(ssid);
            pwdOut[n]  = String(pwd);
            Serial.printf("[wifi-cfg] network[%d]: %s\n", n, ssid);
            n++;
        }
    }
    return n;
}

String wifi_get_json() {
    DynamicJsonDocument doc(2048);
    doc["version"] = 1;

    // saved networks
    JsonArray nets = doc.createNestedArray("networks");
    if (SPIFFS.exists(WIFI_SPIFFS_PATH)) {
        File f = SPIFFS.open(WIFI_SPIFFS_PATH, "r");
        if (f) {
            String body = f.readString();
            f.close();
            DynamicJsonDocument saved(2048);
            if (!deserializeJson(saved, body)) {
                JsonArray s = saved["networks"].as<JsonArray>();
                if (!s.isNull()) {
                    for (JsonObject net : s) {
                        JsonObject o = nets.createNestedObject();
                        o["ssid"]     = (const char*)(net["ssid"] | "");
                        o["password"] = (const char*)(net["password"] | "");
                    }
                }
            }
        }
    }

    // live status
    JsonObject st = doc.createNestedObject("status");
    bool connected = (WiFi.status() == WL_CONNECTED);
    st["connected"] = connected;
    st["ssid"]      = connected ? WiFi.SSID() : String("");
    st["ip"]        = connected ? WiFi.localIP().toString() : String("");
    st["rssi"]      = connected ? WiFi.RSSI() : 0;

    String out;
    serializeJson(doc, out);
    return out;
}

bool wifi_set_json(const String& json) {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc, json)) return false;
    JsonArray nets = doc["networks"].as<JsonArray>();
    if (nets.isNull()) return false;

    // Re-serialize only the networks (strip any client-sent status).
    DynamicJsonDocument out(2048);
    out["version"] = 1;
    JsonArray outNets = out.createNestedArray("networks");
    int n = 0;
    for (JsonObject net : nets) {
        if (n >= WIFI_CFG_MAX_NETWORKS) break;
        const char* ssid = net["ssid"] | "";
        if (!ssid || ssid[0] == '\0') continue;  // skip empty rows
        JsonObject o = outNets.createNestedObject();
        o["ssid"]     = ssid;
        o["password"] = (const char*)(net["password"] | "");
        n++;
    }

    File f = SPIFFS.open(WIFI_SPIFFS_PATH, "w");
    if (!f) { Serial.println("[wifi-cfg] SPIFFS open(w) failed"); return false; }
    String body;
    serializeJson(out, body);
    f.print(body);
    f.close();
    Serial.printf("[wifi-cfg] saved %d networks to /wifi.json\n", n);
    return true;
}

String wifi_scan_json() {
    DynamicJsonDocument doc(4096);
    JsonArray arr = doc.createNestedArray("aps");
    int n = WiFi.scanNetworks();
    for (int i = 0; i < n && i < 30; i++) {
        JsonObject o = arr.createNestedObject();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["enc"]  = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? 0 : 1;
    }
    WiFi.scanDelete();
    String out;
    serializeJson(doc, out);
    return out;
}
