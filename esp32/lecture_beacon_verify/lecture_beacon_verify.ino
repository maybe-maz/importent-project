#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "mbedtls/md.h"

const char* WIFI_SSID = "Alaa_4G";
const char* WIFI_PASS = "A132457@";
const char* MDNS_HOST = "lecture-gate";
const char* GATE_SECRET = "maz";

WebServer webServer(80);

String hmacSha256Hex(const String& payload, const String& key) {
  unsigned char output[32];
  const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!mdInfo) return "";

  int result = mbedtls_md_hmac(
    mdInfo,
    reinterpret_cast<const unsigned char*>(key.c_str()),
    key.length(),
    reinterpret_cast<const unsigned char*>(payload.c_str()),
    payload.length(),
    output
  );
  if (result != 0) return "";

  const char* hex = "0123456789abcdef";
  String out;
  out.reserve(64);
  for (int i = 0; i < 32; i++) {
    out += hex[(output[i] >> 4) & 0x0F];
    out += hex[output[i] & 0x0F];
  }
  return out;
}

String buildNonce() {
  uint32_t a = esp_random();
  uint32_t b = esp_random();
  char buf[17];
  snprintf(buf, sizeof(buf), "%08lx%08lx", static_cast<unsigned long>(a), static_cast<unsigned long>(b));
  return String(buf);
}

String buildGateToken(const String& lectureId) {
  if (lectureId.length() == 0) return "";

  // Read actual Wi-Fi signal strength (RSSI in dBm)
  int32_t rssi = WiFi.RSSI();
  
  // Reject token if signal is too weak (out of range)
  if (rssi < -75) {
    Serial.println("RSSI too weak: " + String(rssi) + " dBm - rejecting token");
    return "ERROR:OUT_OF_RANGE";
  }

  const unsigned long issuedAtMs = millis();
  const String nonce = buildNonce();
  const String payload = lectureId + "." + String(issuedAtMs) + "." + nonce + "." + String(rssi);
  const String signature = hmacSha256Hex(payload, String(GATE_SECRET));
  if (signature.length() == 0) return "";

  return payload + "." + signature;
}

void handleTest() {
  int32_t rssi = WiFi.RSSI();
  String token = buildGateToken("TEST_LECTURE");
  
  String response = "{";
  response += "\"rssi\":" + String(rssi) + ",";
  response += "\"token\":\"" + token + "\",";
  response += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
  response += "\"status\":" + String(WiFi.status());
  response += "}";
  
  webServer.sendHeader("Content-Type", "application/json");
  webServer.send(200, "application/json", response);
}

void handleClaim() {
  const String lectureId = webServer.arg("lectureId");
  const String redirect = webServer.arg("redirect");
  if (lectureId.length() == 0 || redirect.length() == 0) {
    webServer.send(400, "text/plain", "lectureId and redirect are required");
    return;
  }

  const String gateToken = buildGateToken(lectureId);
  if (gateToken.length() == 0) {
    webServer.send(500, "text/plain", "Token signing failed");
    return;
  }

  String target = redirect;
  target += (target.indexOf('?') >= 0) ? "&" : "?";
  target += "gateToken=" + gateToken;

  webServer.sendHeader("Location", target, true);
  webServer.send(302, "text/plain", "Redirecting...");
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  Serial.print("Connecting to Wi-Fi");
  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startedAt) < 20000) {
    delay(400);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("STA IP: ");
    Serial.println(WiFi.localIP());

    if (MDNS.begin(MDNS_HOST)) {
      Serial.print("mDNS: http://");
      Serial.print(MDNS_HOST);
      Serial.println(".local");
    } else {
      Serial.println("mDNS start failed");
    }
  } else {
    Serial.println("Wi-Fi connect timeout; gate may be unreachable until network recovers.");
  }

  webServer.on("/test", HTTP_GET, handleTest);
  webServer.on("/claim", HTTP_GET, handleClaim);
  webServer.begin();
  Serial.println("Wi-Fi gate is ready");
}

void loop() {
  webServer.handleClient();
  delay(50);
}
