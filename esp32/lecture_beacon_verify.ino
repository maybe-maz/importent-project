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

void handlePortal() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="ar">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Lecture Gate Portal</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body { font-family: Arial, sans-serif; background: #f0f0f0; display: flex; justify-content: center; align-items: center; height: 100vh; }
    .container { background: white; padding: 40px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); width: 90%; max-width: 400px; }
    h1 { text-align: center; color: #333; margin-bottom: 30px; font-size: 24px; }
    .form-group { margin-bottom: 20px; }
    label { display: block; margin-bottom: 8px; color: #555; font-weight: bold; }
    input { width: 100%; padding: 12px; border: 1px solid #ddd; border-radius: 5px; font-size: 16px; }
    button { width: 100%; padding: 12px; background: #0066cc; color: white; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; margin-top: 20px; }
    button:hover { background: #0052a3; }
    .error { color: red; text-align: center; margin-top: 10px; }
    .loading { display: none; text-align: center; }
  </style>
</head>
<body>
  <div class="container">
    <h1>Student Check-In</h1>
    <form id="form" method="POST" action="/verify">
      <div class="form-group">
        <label for="studentId">Student ID:</label>
        <input type="text" id="studentId" name="studentId" required>
      </div>
      <div class="form-group">
        <label for="studentName">Student Name:</label>
        <input type="text" id="studentName" name="studentName" required>
      </div>
      <button type="submit">Verify & Get Token</button>
    </form>
    <div id="error" class="error"></div>
    <div id="loading" class="loading">Processing...</div>
  </div>
  <script>
    document.getElementById('form').onsubmit = async (e) => {
      e.preventDefault();
      document.getElementById('error').textContent = '';
      document.getElementById('loading').style.display = 'block';
      
      const studentId = document.getElementById('studentId').value;
      const studentName = document.getElementById('studentName').value;
      
      try {
        const response = await fetch('/verify', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ studentId, studentName })
        });
        
        const data = await response.json();
        
        if (data.success && data.gateToken) {
          // Redirect to check-in page with token
          window.location.href = 'https://maybe-maz.github.io/importent-project/student-checkin.html?gateToken=' + encodeURIComponent(data.gateToken) + '&lectureId=' + encodeURIComponent(data.lectureId);
        } else {
          document.getElementById('error').textContent = data.error || 'Verification failed';
        }
      } catch (err) {
        document.getElementById('error').textContent = 'Error: ' + err.message;
      }
      
      document.getElementById('loading').style.display = 'none';
    };
  </script>
</body>
</html>
  )rawliteral";
  
  webServer.send(200, "text/html", html);
}

void handleVerify() {
  String body = webServer.arg("plain");
  
  // Parse JSON (simple manual parsing)
  int idStart = body.indexOf("\"studentId\":\"") + 13;
  int idEnd = body.indexOf("\"", idStart);
  String studentId = body.substring(idStart, idEnd);
  
  int nameStart = body.indexOf("\"studentName\":\"") + 15;
  int nameEnd = body.indexOf("\"", nameStart);
  String studentName = body.substring(nameStart, nameEnd);
  
  // Check RSSI
  int32_t rssi = WiFi.softAPgetStationNum() > 0 ? -50 : -80; // Simplified
  
  if (rssi < -75) {
    webServer.send(200, "application/json", "{\"success\":false,\"error\":\"Out of range\"}");
    return;
  }
  
  // Generate token
  String gateToken = buildGateToken("LECTURE_001");
  
  String response = "{\"success\":true,\"gateToken\":\"" + gateToken + "\",\"lectureId\":\"LECTURE_001\"}";
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

  // Start AP mode (Captive Portal)
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  
  Serial.println("Wi-Fi AP started:");
  Serial.print("SSID: ");
  Serial.println(AP_SSID);
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  webServer.on("/", HTTP_GET, handlePortal);
  webServer.on("/portal", HTTP_GET, handlePortal);
  webServer.on("/verify", HTTP_POST, handleVerify);
  webServer.on("/test", HTTP_GET, handlePortal);
  webServer.begin();
  Serial.println("Web server started");
}

void loop() {
  webServer.handleClient();
  delay(50);
}
