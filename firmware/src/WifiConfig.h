#ifndef _WIFI_CONFIG_H
#define _WIFI_CONFIG_H

#include <Arduino.h>

// Web-editable Wi-Fi network list, persisted to SPIFFS (/wifi.json).
//
// This augments the existing SD-card YAML multi-network flow: at boot,
// tryMultiNetworkWifi() asks here FIRST (wifi_config_get_networks). If the user
// has saved networks via the web UI they win; otherwise we fall back to the
// SD YAML wifi.networks[] / single ssid as before.
//
// Changes apply on REBOOT (intentional — avoids dropping the connection you're
// editing over). The web UI exposes a reboot button.

#define WIFI_CFG_MAX_NETWORKS 5

// Fill ssid/pwd arrays from SPIFFS /wifi.json. Returns count added (0 if none).
int    wifi_config_get_networks(String* ssidOut, String* pwdOut, int maxN);

String wifi_get_json();                       // saved networks + live status (/wifi_get)
bool   wifi_set_json(const String& json);     // persist networks (/wifi_set)
String wifi_scan_json();                       // scan nearby APs (/wifi_scan)

#endif  // _WIFI_CONFIG_H
