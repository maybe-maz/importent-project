#include <WiFi.h>
#include <WebServer.h>
#include "mbedtls/md.h"

const char* AP_SSID = "LECTURE_BEACON";
const char* AP_PASS = "12345678";
const char* GATE_SECRET = "CHANGE_ME_GATE_SECRET";

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

  const int rssi = 0;
  const unsigned long issuedAtMs = millis();
  const String nonce = buildNonce();
  const String payload = lectureId + "." + String(issuedAtMs) + "." + nonce + "." + String(rssi);
  const String signature = hmacSha256Hex(payload, String(GATE_SECRET));
  if (signature.length() == 0) return "";

  return payload + "." + signature;
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

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  webServer.on("/claim", HTTP_GET, handleClaim);
  webServer.begin();

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  Serial.println("Wi-Fi gate is ready");
}

void loop() {
  webServer.handleClient();
  delay(50);
}
