#include <WiFi.h>
#include <WebServer.h>
#include "mbedtls/md.h"

// Captive Portal AP mode
const char* AP_SSID = "LECTURE_BEACON";
const char* AP_PASS = "12345678";
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
  int32_t rssi = WiFi.RSSI();
  if (rssi < -75) {
    Serial.println("RSSI too weak: " + String(rssi) + " dBm");
    return "ERROR:OUT_OF_RANGE";
  }
  const unsigned long issuedAtMs = millis();
  const String nonce = buildNonce();
  const String payload = lectureId + "." + String(issuedAtMs) + "." + nonce + "." + String(rssi);
  const String signature = hmacSha256Hex(payload, String(GATE_SECRET));
  return payload + "." + signature;
}

void handlePortal() {
  String html = R"rawliteral(<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>Gate Portal</title><style>body{font-family:Arial;background:#f0f0f0;display:flex;justify-content:center;align-items:center;height:100vh}.container{background:white;padding:40px;border-radius:10px;width:90%;max-width:400px}.form-group{margin:20px 0}label{display:block;margin:8px 0;font-weight:bold}input{width:100%;padding:12px;border:1px solid #ddd;border-radius:5px;font-size:16px}button{width:100%;padding:12px;background:#0066cc;color:white;border:none;border-radius:5px;font-size:16px;cursor:pointer;margin-top:20px}button:hover{background:#0052a3}.error{color:red;text-align:center;margin:10px 0}</style></head><body><div class="container"><h1>Student Check-In</h1><form id="form"><div class="form-group"><label>Student ID:</label><input type="text" id="studentId" required></div><div class="form-group"><label>Student Name:</label><input type="text" id="studentName" required></div><button type="submit">Get Token</button></form><div id="error" class="error"></div></div><script>document.getElementById('form').onsubmit=async(e)=>{e.preventDefault();const s=document.getElementById('studentId').value;const n=document.getElementById('studentName').value;try{const r=await fetch('/verify',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({studentId:s,studentName:n})});const d=await r.json();if(d.success){window.location.href='https://maybe-maz.github.io/importent-project/student-checkin.html?gateToken='+encodeURIComponent(d.gateToken)+'&lectureId='+encodeURIComponent(d.lectureId);}else{document.getElementById('error').textContent=d.error||'Failed';}}catch(e){document.getElementById('error').textContent='Error: '+e.message;}};</script></body></html>)rawliteral";
  webServer.send(200, "text/html", html);
}

void handleVerify() {
  String body = webServer.arg("plain");
  int idStart = body.indexOf("\"studentId\":\"") + 13;
  int idEnd = body.indexOf("\"", idStart);
  String studentId = body.substring(idStart, idEnd);
  
  String gateToken = buildGateToken("LECTURE_001");
  String response = "{\"success\":true,\"gateToken\":\"" + gateToken + "\",\"lectureId\":\"LECTURE_001\"}";
  webServer.send(200, "application/json", response);
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.println("\n\nAP Mode:");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());
  
  webServer.on("/", HTTP_GET, handlePortal);
  webServer.on("/portal", HTTP_GET, handlePortal);
  webServer.on("/verify", HTTP_POST, handleVerify);
  webServer.begin();
  Serial.println("Server ready");
}

void loop() {
  webServer.handleClient();
  delay(50);
}