#include <WiFi.h>
#include <WebServer.h>
#include "esp_wifi.h"

const char* AP_SSID = "LECTURE_BEACON";
const char* AP_PASS = "12345678";

WebServer server(80);

const int RSSI_MIN = -70;
const int RSSI_MAX = -10;

void sendCorsText(int statusCode, const char* body) {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(statusCode, "text/plain", body);
}

void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204, "text/plain", "");
}

void handleVerify() {
  wifi_sta_list_t wifi_sta_list;
  memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));

  esp_wifi_ap_get_sta_list(&wifi_sta_list);

  if (wifi_sta_list.num == 0) {
    sendCorsText(403, "No client connected");
    return;
  }

  // This uses the first connected station as the currently verifying student.
  int rssi = wifi_sta_list.sta[0].rssi;
  Serial.print("RSSI = ");
  Serial.println(rssi);

  if (rssi >= RSSI_MIN && rssi <= RSSI_MAX) {
    sendCorsText(200, "OK");
  } else {
    sendCorsText(403, "Too far");
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);

  Serial.print("ESP IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/verify", HTTP_GET, handleVerify);
  server.on("/verify", HTTP_OPTIONS, handleOptions);
  server.onNotFound(handleVerify);
  server.begin();
}

void loop() {
  server.handleClient();
}
