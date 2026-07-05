#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_wifi.h"

// --- AP config (students connect here) ---------------------------------------
const char* AP_SSID     = "LECTURE_BEACON";
const char* AP_PASS     = "12345678";

// --- STA config (ESP connects here for internet / Supabase) ------------------
const char* STA_SSID    = "Alaa_4G";
const char* STA_PASS    = "A132457@";

// --- Supabase ----------------------------------------------------------------
const char* SUPA_URL    = "https://nwvwqmcezaymypkictil.supabase.co";
const char* SUPA_KEY    = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im53dndxbWNlemF5bXlwa2ljdGlsIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODI1NzI5MjAsImV4cCI6MjA5ODE0ODkyMH0.sv03G8WixXmy6cx4P5PZvBdbJK6yQAPLZd60C__KeEI";

// --- RSSI threshold ----------------------------------------------------------
const int RSSI_THRESHOLD = -75;

WebServer webServer(80);

// --- Get RSSI of the station whose IP matches the current HTTP client ---------
int8_t getClientRssi() {
  IPAddress clientIP = webServer.client().remoteIP();

  wifi_sta_list_t stalist;
  if (esp_wifi_ap_get_sta_list(&stalist) != ESP_OK || stalist.num == 0) {
    Serial.println("[RSSI] No stations – defaulting -50");
    return -50;
  }

  tcpip_adapter_sta_list_t tcpip_list;
  if (tcpip_adapter_get_sta_list(&stalist, &tcpip_list) == ESP_OK) {
    for (int i = 0; i < (int)tcpip_list.num; i++) {
      uint32_t raw = tcpip_list.sta[i].ip.addr;
      IPAddress stIP(raw & 0xFF, (raw >> 8) & 0xFF, (raw >> 16) & 0xFF, (raw >> 24) & 0xFF);
      if (stIP == clientIP) {
        Serial.printf("[RSSI] Client %s -> %d dBm\n", clientIP.toString().c_str(), stalist.sta[i].rssi);
        return stalist.sta[i].rssi;
      }
    }
  }

  Serial.printf("[RSSI] IP match failed – first station RSSI: %d\n", stalist.sta[0].rssi);
  return stalist.sta[0].rssi;
}

// --- Call Supabase checkin_student RPC ---------------------------------------
String callSupabase(const String& lectureId, const String& studentId,
                    const String& studentName, const String& password) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(SUPA_URL) + "/rest/v1/rpc/checkin_student";
  http.begin(client, url);
  http.addHeader("Content-Type",  "application/json");
  http.addHeader("apikey",        SUPA_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPA_KEY);

  String body = "{\"p_lecture_id\":\"" + lectureId   + "\","
                 "\"p_student_id\":\"" + studentId    + "\","
                 "\"p_student_name\":\"" + studentName + "\","
                 "\"p_password\":\"" + password + "\"}";

  Serial.println("[Supabase] POST -> " + url);
  Serial.println("[Supabase] Body: " + body);

  int code = http.POST(body);
  String resp = "";
  if (code > 0) {
    resp = http.getString();
    Serial.printf("[Supabase] HTTP %d -> %s\n", code, resp.c_str());
  } else {
    Serial.printf("[Supabase] Error: %s\n", http.errorToString(code).c_str());
    resp = "{\"ok\":false,\"error\":\"Cannot reach Supabase\"}";
  }
  http.end();
  return resp;
}

// --- Portal HTML page ---------------------------------------------------------
void handlePortal() {
  String lectureId = webServer.arg("lectureId");

  String html = "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>Student Check-In</title>"
    "<style>"
    "body{font-family:Arial,sans-serif;background:#f0f4ff;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0}"
    ".box{background:#fff;padding:32px 24px;border-radius:14px;box-shadow:0 4px 20px rgba(0,0,0,.1);width:92%;max-width:380px}"
    "h2{color:#2563eb;margin:0 0 20px;text-align:center}"
    "label{display:block;font-size:13px;font-weight:bold;color:#374151;margin-bottom:6px}"
    "input{width:100%;padding:11px;border:1px solid #d1d5db;border-radius:8px;font-size:15px;box-sizing:border-box;margin-bottom:14px}"
    "button{width:100%;padding:13px;background:#2563eb;color:#fff;border:none;border-radius:8px;font-size:16px;cursor:pointer;font-weight:bold}"
    "button:hover{background:#1d4ed8}"
    "#msg{margin-top:14px;text-align:center;font-size:14px;font-weight:bold;min-height:20px}"
    ".ok{color:#16a34a}.err{color:#dc2626}.warn{color:#d97706}"
    "</style></head><body>"
    "<div class='box'>"
    "<h2>Student Check-In</h2>"
    "<form id='f'>"
    "<input type='hidden' id='lid' value='" + lectureId + "'>"
    "<label>Student ID</label>"
    "<input type='text' id='sid' placeholder='e.g. 241100150' required>"
    "<label>Student Name</label>"
    "<input type='text' id='sname' placeholder='Full name' required>"
    "<label>Password (if required)</label>"
    "<input type='password' id='spwd' placeholder='Leave empty if not needed'>"
    "<button type='submit'>Submit Attendance</button>"
    "</form>"
    "<div id='msg'></div>"
    "</div>"
    "<script>"
    "document.getElementById('f').onsubmit=async(e)=>{"
    "e.preventDefault();"
    "const msg=document.getElementById('msg');"
    "msg.className='warn';msg.textContent='Checking...';"
    "const lid=document.getElementById('lid').value;"
    "const sid=document.getElementById('sid').value.trim();"
    "const sname=document.getElementById('sname').value.trim();"
    "const spwd=document.getElementById('spwd').value.trim();"
    "if(!lid){msg.className='err';msg.textContent='No lecture ID – ask doctor to regenerate QR.';return;}"
    "try{"
    "const r=await fetch('/submit',{method:'POST',"
    "headers:{'Content-Type':'application/json'},"
    "body:JSON.stringify({lectureId:lid,studentId:sid,studentName:sname,password:spwd})});"
    "const d=await r.json();"
    "if(d.ok){"
    "msg.className='ok';"
    "msg.textContent='Attendance recorded: '+d.status+(d.note&&d.note!=='-'?' ('+d.note+')':'');"
    "}else{"
    "msg.className='err';"
    "msg.textContent=(d.error||'Failed');"
    "}"
    "}catch(err){msg.className='err';msg.textContent='Connection error: '+err.message;}"
    "};"
    "</script>"
    "</body></html>";

  webServer.send(200, "text/html", html);
}

// --- Submit: RSSI check then Supabase ----------------------------------------
void handleSubmit() {
  String body = webServer.arg("plain");

  auto extract = [&](const String& key) -> String {
    String search = "\"" + key + "\":\"";
    int s = body.indexOf(search);
    if (s < 0) return "";
    s += search.length();
    int e = body.indexOf("\"", s);
    return e < 0 ? "" : body.substring(s, e);
  };

  String lectureId   = extract("lectureId");
  String studentId   = extract("studentId");
  String studentName = extract("studentName");
  String password    = extract("password");

  if (lectureId.isEmpty() || studentId.isEmpty() || studentName.isEmpty()) {
    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing fields\"}");
    return;
  }

  // RSSI check
  int8_t rssi = getClientRssi();
  Serial.printf("[Submit] Student=%s RSSI=%d\n", studentId.c_str(), rssi);

  if (rssi < RSSI_THRESHOLD) {
    String resp = "{\"ok\":false,\"error\":\"Out of range (RSSI "
                  + String(rssi) + " dBm). Move closer to the device.\"}";
    webServer.send(200, "application/json", resp);
    return;
  }

  // Internet check
  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(200, "application/json",
      "{\"ok\":false,\"error\":\"Gateway has no internet. Contact lecturer.\"}");
    return;
  }

  // Call Supabase
  String supaResp = callSupabase(lectureId, studentId, studentName, password);
  webServer.send(200, "application/json", supaResp);
}

// --- Setup --------------------------------------------------------------------
void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("\n[AP] SSID: %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  WiFi.begin(STA_SSID, STA_PASS);
  Serial.print("[STA] Connecting to ");
  Serial.print(STA_SSID);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(400);
    Serial.print('.');
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[STA] Connected – IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[STA] No internet – Supabase calls will fail");
  }

  webServer.on("/",       HTTP_GET,  handlePortal);
  webServer.on("/portal", HTTP_GET,  handlePortal);
  webServer.on("/submit", HTTP_POST, handleSubmit);
  webServer.begin();
  Serial.println("[Gate] Ready – http://192.168.4.1/");
}

// --- Loop ---------------------------------------------------------------------
void loop() {
  webServer.handleClient();
  delay(10);
}
