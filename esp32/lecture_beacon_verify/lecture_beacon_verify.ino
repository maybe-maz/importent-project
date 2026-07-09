#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "esp_wifi.h"

const char* AP_SSID  = "LECTURE_BEACON";
const char* AP_PASS  = "12345678";
const char* STA_SSID = "Alaa_4G";
const char* STA_PASS = "A132457@";
const char* SUPA_URL = "https://nwvwqmcezaymypkictil.supabase.co";
const char* SUPA_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6Im53dndxbWNlemF5bXlwa2ljdGlsIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODI1NzI5MjAsImV4cCI6MjA5ODE0ODkyMH0.sv03G8WixXmy6cx4P5PZvBdbJK6yQAPLZd60C__KeEI";
const int RSSI_THRESHOLD = -75;
const int PASSWORD_AFTER_MINUTES = 15;
const int MAX_DEVICE_LOCKS = 80;

WebServer webServer(80);

struct DeviceLockEntry {
  bool used;
  String lectureId;
  String stationMac;
  String studentId;
  String studentName;
  String status;
  String note;
  unsigned long updatedAt;
};

DeviceLockEntry deviceLocks[MAX_DEVICE_LOCKS];

int8_t getClientRssi() {
  wifi_sta_list_t stalist;
  esp_wifi_ap_get_sta_list(&stalist);

  if (stalist.num == 0) {
    Serial.println("[RSSI] No stations");
    return -50;
  }

  int8_t rssi = stalist.sta[0].rssi;
  Serial.printf("[RSSI] RSSI: %d dBm\n", rssi);
  return rssi;
}

String stationMacToString(const uint8_t mac[6]) {
  char buff[18];
  snprintf(
    buff,
    sizeof(buff),
    "%02X:%02X:%02X:%02X:%02X:%02X",
    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
  );
  return String(buff);
}

String getFirstConnectedStationMac() {
  wifi_sta_list_t staList;
  esp_wifi_ap_get_sta_list(&staList);
  if (staList.num <= 0) return "";
  return stationMacToString(staList.sta[0].mac);
}

int findDeviceLockIndex(const String& lectureId, const String& stationMac) {
  for (int i = 0; i < MAX_DEVICE_LOCKS; i++) {
    if (!deviceLocks[i].used) continue;
    if (deviceLocks[i].lectureId == lectureId && deviceLocks[i].stationMac == stationMac) {
      return i;
    }
  }
  return -1;
}

int findFreeDeviceLockSlot() {
  for (int i = 0; i < MAX_DEVICE_LOCKS; i++) {
    if (!deviceLocks[i].used) return i;
  }

  int oldestIndex = 0;
  unsigned long oldestTime = deviceLocks[0].updatedAt;
  for (int i = 1; i < MAX_DEVICE_LOCKS; i++) {
    if (deviceLocks[i].updatedAt < oldestTime) {
      oldestTime = deviceLocks[i].updatedAt;
      oldestIndex = i;
    }
  }
  return oldestIndex;
}

void upsertDeviceLock(
  const String& lectureId,
  const String& stationMac,
  const String& studentId,
  const String& studentName,
  const String& status,
  const String& note
) {
  if (lectureId.isEmpty() || stationMac.isEmpty()) return;

  int idx = findDeviceLockIndex(lectureId, stationMac);
  if (idx < 0) {
    idx = findFreeDeviceLockSlot();
  }

  deviceLocks[idx].used = true;
  deviceLocks[idx].lectureId = lectureId;
  deviceLocks[idx].stationMac = stationMac;
  deviceLocks[idx].studentId = studentId;
  deviceLocks[idx].studentName = studentName;
  deviceLocks[idx].status = status;
  deviceLocks[idx].note = note;
  deviceLocks[idx].updatedAt = millis();
}

String escapeJson(const String& value) {
  String out;
  out.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); i++) {
    char c = value.charAt(i);
    if (c == '\\' || c == '"') {
      out += '\\';
      out += c;
    } else if (c == '\n') {
      out += "\\n";
    } else if (c == '\r') {
      out += "\\r";
    } else if (c == '\t') {
      out += "\\t";
    } else {
      out += c;
    }
  }
  return out;
}

String extractJsonValue(const String& body, const String& key) {
  String marker = "\"" + key + "\":";
  int keyPos = body.indexOf(marker);
  if (keyPos < 0) return "";

  int valuePos = keyPos + marker.length();
  while (valuePos < body.length() && (body.charAt(valuePos) == ' ' || body.charAt(valuePos) == '\n' || body.charAt(valuePos) == '\r' || body.charAt(valuePos) == '\t')) {
    valuePos++;
  }

  if (valuePos >= body.length()) return "";

  if (body.charAt(valuePos) == '"') {
    valuePos++;
    String out;
    bool escaped = false;
    for (int i = valuePos; i < body.length(); i++) {
      char c = body.charAt(i);
      if (escaped) {
        out += c;
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        return out;
      } else {
        out += c;
      }
    }
    return out;
  }

  int end = valuePos;
  while (end < body.length() && body.charAt(end) != ',' && body.charAt(end) != '}' && body.charAt(end) != '\n') {
    end++;
  }
  String raw = body.substring(valuePos, end);
  raw.trim();
  if (raw == "null") return "";
  return raw;
}

String httpGetSupabase(const String& pathAndQuery, int* outCode) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  String url = String(SUPA_URL) + pathAndQuery;
  http.begin(client, url);
  http.addHeader("apikey", SUPA_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPA_KEY);

  int code = http.GET();
  if (outCode) *outCode = code;
  String resp = code > 0 ? http.getString() : "";
  http.end();
  return resp;
}

String fetchLectureStartTime(const String& lectureId) {
  int code = 0;
  String path = "/rest/v1/lectures?id=eq." + lectureId + "&select=start_time";
  String resp = httpGetSupabase(path, &code);
  if (code < 200 || code >= 300) {
    return "";
  }

  String marker = "\"start_time\":\"";
  int start = resp.indexOf(marker);
  if (start < 0) return "";
  start += marker.length();
  int end = resp.indexOf('"', start);
  if (end < 0) return "";

  String value = resp.substring(start, end);
  value.trim();
  return value;
}

String callSupabaseCheckin(const String& lid, const String& sid, const String& sname, const String& pwd) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, String(SUPA_URL) + "/rest/v1/rpc/checkin_student");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPA_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPA_KEY);

  String body = "{\"p_lecture_id\":\"" + escapeJson(lid)
    + "\",\"p_student_id\":\"" + escapeJson(sid)
    + "\",\"p_student_name\":\"" + escapeJson(sname) + "\",";

  if (pwd.length() > 0) {
    body += "\"p_password\":\"" + escapeJson(pwd) + "\"}";
  } else {
    body += "\"p_password\":null}";
  }

  int code = http.POST(body);
  String resp = code > 0 ? http.getString() : "{\"ok\":false,\"message\":\"Cannot reach Supabase\"}";
  http.end();
  return resp;
}

String buildPortalHtml(const String& lectureId) {
  String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <title>Check-In</title>
  <style>
    body{font-family:Arial,sans-serif;background:#f0f4ff;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;padding:12px;box-sizing:border-box}
    .box{background:#fff;padding:28px 20px;border-radius:14px;box-shadow:0 4px 20px rgba(0,0,0,.1);width:100%;max-width:380px}
    h2{color:#2563eb;margin:0 0 14px;text-align:center}
    label{display:block;font-size:13px;font-weight:bold;color:#374151;margin-bottom:6px}
    input{width:100%;padding:11px;border:1px solid #d1d5db;border-radius:8px;font-size:15px;box-sizing:border-box;margin-bottom:12px}
    button{width:100%;padding:13px;background:#2563eb;color:#fff;border:none;border-radius:8px;font-size:16px;cursor:pointer;font-weight:bold}
    #msg{margin-top:14px;text-align:center;font-size:14px;font-weight:bold}
    #rule{margin-top:10px;font-size:12px;background:#fff7ed;border:1px solid #fed7aa;color:#9a3412;border-radius:8px;padding:8px 10px}
    .ok{color:#16a34a}
    .err{color:#dc2626}
    .warn{color:#d97706}
  </style>
</head>
<body>
  <div class='box'>
    <h2>Student Check-In</h2>
    <form id='f'>
      <input type='hidden' id='lid' value='__LECTURE_ID__'>
      <label>Student ID</label>
      <input type='text' id='sid' required>
      <label>Student Name</label>
      <input type='text' id='sname' required>
      <div id='passwordWrap'>
        <label>Password</label>
        <input type='password' id='spwd'>
      </div>
      <button type='submit' id='submitBtn'>Submit Attendance</button>
    </form>
    <div id='rule'>Loading lecture time rules...</div>
    <div id='msg'></div>
  </div>

  <script>
    const lid = document.getElementById('lid').value;
    const lockKey = `espCheckinLock::${lid}`;
    const metaKey = `espCheckinMeta::${lid}`;
    const lockCookieKey = `espCheckinLock_${lid}`;
    const metaCookieKey = `espCheckinMeta_${lid}`;
    let passwordRequired = false;
    let knownStartTime = '';

    function setCookie(name, value, maxAgeSeconds){
      document.cookie = `${encodeURIComponent(name)}=${encodeURIComponent(value)}; path=/; max-age=${maxAgeSeconds}; samesite=lax`;
    }

    function getCookie(name){
      const encodedName = `${encodeURIComponent(name)}=`;
      const parts = document.cookie ? document.cookie.split('; ') : [];
      for(const part of parts){
        if(part.startsWith(encodedName)){
          return decodeURIComponent(part.substring(encodedName.length));
        }
      }
      return '';
    }

    function hhmmValue(value){
      const m = String(value || '').match(/^(\d{2}:\d{2})/);
      return m ? m[1] : '';
    }

    function minutesFromStart(hhmm){
      const clean = hhmmValue(hhmm);
      if(!clean) return null;
      const parts = clean.split(':').map(Number);
      if(parts.length !== 2 || Number.isNaN(parts[0]) || Number.isNaN(parts[1])) return null;
      const now = new Date();
      const at = new Date(now.getFullYear(), now.getMonth(), now.getDate(), parts[0], parts[1], 0, 0);
      return (now.getTime() - at.getTime()) / 60000;
    }

    function renderRule(){
      const wrap = document.getElementById('passwordWrap');
      const rule = document.getElementById('rule');
      if(passwordRequired){
        wrap.style.display = 'block';
        rule.textContent = 'Password is required now because more than 15 minutes passed from lecture start.';
      } else {
        wrap.style.display = 'none';
        document.getElementById('spwd').value = '';
        rule.textContent = 'No password needed in the first 15 minutes from lecture start.';
      }
    }

    function toNextUrl(studentId, studentName, status, note){
      const q = new URLSearchParams({
        lectureId: lid,
        studentId: studentId || '',
        studentName: studentName || '',
        status: status || '',
        note: note || ''
      });
      return `/next?${q.toString()}`;
    }

    function redirectIfLocked(){
      const localLocked = localStorage.getItem(lockKey) === '1';
      const cookieLocked = getCookie(lockCookieKey) === '1';
      if(!localLocked && !cookieLocked) return false;

      try {
        const localMetaRaw = localStorage.getItem(metaKey) || '';
        const cookieMetaRaw = getCookie(metaCookieKey) || '';
        const raw = localMetaRaw || cookieMetaRaw || '{}';
        const meta = JSON.parse(raw);
        window.location.replace(toNextUrl(meta.studentId, meta.studentName, meta.status, meta.note));
      } catch {
        window.location.replace(toNextUrl('', '', '', ''));
      }
      return true;
    }

    async function enforceServerLock(){
      if(!lid) return false;
      try {
        const r = await fetch(`/lock-status?lectureId=${encodeURIComponent(lid)}`);
        const d = await r.json();
        if(d && d.ok && d.locked){
          window.location.replace(toNextUrl(d.studentId || '', d.studentName || '', d.status || '', d.note || ''));
          return true;
        }
      } catch {
      }
      return false;
    }

    async function loadLectureInfo(){
      if(!lid){
        document.getElementById('rule').textContent = 'Missing lecture ID in QR link.';
        return;
      }

      try {
        const r = await fetch(`/lecture-info?lectureId=${encodeURIComponent(lid)}`);
        const d = await r.json();
        if(d.ok){
          knownStartTime = d.startTime || '';
          const diff = minutesFromStart(knownStartTime);
          passwordRequired = typeof diff === 'number' ? diff > 15 : false;
          renderRule();
          return;
        }
        passwordRequired = false;
        renderRule();
        document.getElementById('rule').textContent = d.message || 'Could not load lecture time; password remains optional.';
      } catch {
        passwordRequired = false;
        renderRule();
        document.getElementById('rule').textContent = 'No internet to read lecture time; password remains optional.';
      }
    }

    document.getElementById('f').onsubmit = async (e) => {
      e.preventDefault();
      const msg = document.getElementById('msg');
      msg.className = 'warn';
      msg.textContent = 'Checking...';

      const sid = document.getElementById('sid').value.trim();
      const sname = document.getElementById('sname').value.trim();
      const pwd = document.getElementById('spwd').value.trim();

      if(!lid){
        msg.className = 'err';
        msg.textContent = 'No lecture ID.';
        return;
      }
      if(!sid || !sname){
        msg.className = 'err';
        msg.textContent = 'Student ID and name are required.';
        return;
      }
      if(passwordRequired && !pwd){
        msg.className = 'err';
        msg.textContent = 'Password is required after 15 minutes.';
        return;
      }

      try{
        const r = await fetch('/submit',{
          method:'POST',
          headers:{'Content-Type':'application/json'},
          body:JSON.stringify({lectureId:lid,studentId:sid,studentName:sname,password:pwd})
        });
        const d = await r.json();
        if(d.ok){
          msg.className = 'ok';
          msg.textContent = 'Attendance: ' + (d.status || 'saved');
          const savedMeta = JSON.stringify({
            studentId: sid,
            studentName: sname,
            status: d.status || '',
            note: d.note || ''
          });

          localStorage.setItem(lockKey, '1');
          localStorage.setItem(metaKey, savedMeta);
          setCookie(lockCookieKey, '1', 60 * 60 * 24 * 30);
          setCookie(metaCookieKey, savedMeta, 60 * 60 * 24 * 30);

          window.location.href = toNextUrl(sid, sname, d.status || '', d.note || '');
          return;
        }
        msg.className = 'err';
        msg.textContent = d.error || d.message || 'Check-in failed.';
      } catch(_){
        msg.className = 'err';
        msg.textContent = 'Network error.';
      }
    };

    if(!redirectIfLocked()){
      enforceServerLock().then((locked) => {
        if(!locked){
          loadLectureInfo();
        }
      });
      setInterval(loadLectureInfo, 30000);
    }
  </script>
</body>
</html>
)HTML";

  html.replace("__LECTURE_ID__", lectureId);
  return html;
}

String buildNextHtml(const String& lectureId, const String& studentId, const String& studentName, const String& status, const String& note) {
  String html = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset='UTF-8'>
  <meta name='viewport' content='width=device-width,initial-scale=1'>
  <title>Check-In Done</title>
  <style>
    body{margin:0;min-height:100vh;display:grid;place-items:center;padding:16px;box-sizing:border-box;font-family:Arial,sans-serif;background:#f4f8ff;color:#0f172a}
    .card{width:min(560px,100%);background:#fff;border:1px solid #dbeafe;border-radius:16px;box-shadow:0 10px 24px rgba(37,99,235,.12);padding:22px}
    h1{margin:0 0 10px;font-size:24px;color:#1d4ed8}
    p{margin:8px 0;color:#475569;line-height:1.5}
    .ok{background:#ecfeff;border:1px solid #99f6e4;color:#0f766e;padding:10px;border-radius:10px;font-weight:bold}
    .actions{margin-top:14px;display:flex;gap:10px;flex-wrap:wrap}
    .btn{border:none;border-radius:10px;padding:10px 14px;font-size:14px;font-weight:700;color:#fff;background:#2563eb;cursor:pointer}
    .btn.light{background:#0f766e}
  </style>
</head>
<body>
  <div class='card'>
    <h1>Check-in Saved</h1>
    <div class='ok'>You are already checked in on this device for this lecture.</div>
    <p>Lecture ID: <span id='lid'>-</span></p>
    <p>Student: <span id='student'>-</span></p>
    <p>Status: <span id='status'>-</span></p>
    <p>Note: <span id='note'>-</span></p>
    <div class='actions'>
      <button class='btn' onclick='goBreak()'>Break 5 Minutes</button>
      <button class='btn light' onclick='goExcuse()'>Excuse</button>
    </div>
  </div>

  <script>
    const lectureId = '__LECTURE_ID__';
    const studentId = '__STUDENT_ID__';
    const studentName = '__STUDENT_NAME__';
    const status = '__STATUS__';
    const note = '__NOTE__';
    const lockKey = `espCheckinLock::${lectureId}`;
    const metaKey = `espCheckinMeta::${lectureId}`;

    document.getElementById('lid').textContent = lectureId || '-';
    document.getElementById('student').textContent = `${studentName || '-'} (${studentId || '-'})`;
    document.getElementById('status').textContent = status || '-';
    document.getElementById('note').textContent = note || '-';

    if(lectureId){
      localStorage.setItem(lockKey, '1');
      localStorage.setItem(metaKey, JSON.stringify({studentId, studentName, status, note}));
    }

    function goBreak(){
      alert('Break request page can be linked here.');
    }

    function goExcuse(){
      alert('Excuse request page can be linked here.');
    }
  </script>
</body>
</html>
)HTML";

  html.replace("__LECTURE_ID__", lectureId);
  html.replace("__STUDENT_ID__", studentId);
  html.replace("__STUDENT_NAME__", studentName);
  html.replace("__STATUS__", status);
  html.replace("__NOTE__", note);
  return html;
}

void handlePortal() {
  String lectureId = webServer.arg("lectureId");
  webServer.send(200, "text/html", buildPortalHtml(lectureId));
}

void handleLectureInfo() {
  String lectureId = webServer.arg("lectureId");
  if (lectureId.isEmpty()) {
    webServer.send(400, "application/json", "{\"ok\":false,\"message\":\"Missing lectureId\"}");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(200, "application/json", "{\"ok\":false,\"message\":\"No internet\"}");
    return;
  }

  String startTime = fetchLectureStartTime(lectureId);
  if (startTime.isEmpty()) {
    webServer.send(200, "application/json", "{\"ok\":false,\"message\":\"Lecture start time not found\"}");
    return;
  }

  String payload = "{\"ok\":true,\"startTime\":\"" + escapeJson(startTime) + "\",\"passwordAfterMinutes\":" + String(PASSWORD_AFTER_MINUTES) + "}";
  webServer.send(200, "application/json", payload);
}

void handleLockStatus() {
  String lectureId = webServer.arg("lectureId");
  if (lectureId.isEmpty()) {
    webServer.send(400, "application/json", "{\"ok\":false,\"message\":\"Missing lectureId\"}");
    return;
  }

  String stationMac = getFirstConnectedStationMac();
  if (stationMac.isEmpty()) {
    webServer.send(200, "application/json", "{\"ok\":true,\"locked\":false}");
    return;
  }

  int idx = findDeviceLockIndex(lectureId, stationMac);
  if (idx < 0) {
    webServer.send(200, "application/json", "{\"ok\":true,\"locked\":false}");
    return;
  }

  String payload = "{\"ok\":true,\"locked\":true"
    ",\"studentId\":\"" + escapeJson(deviceLocks[idx].studentId) + "\""
    ",\"studentName\":\"" + escapeJson(deviceLocks[idx].studentName) + "\""
    ",\"status\":\"" + escapeJson(deviceLocks[idx].status) + "\""
    ",\"note\":\"" + escapeJson(deviceLocks[idx].note) + "\""
    "}";

  webServer.send(200, "application/json", payload);
}

void handleSubmit() {
  String body = webServer.arg("plain");
  String lectureId = extractJsonValue(body, "lectureId");
  String studentId = extractJsonValue(body, "studentId");
  String studentName = extractJsonValue(body, "studentName");
  String password = extractJsonValue(body, "password");

  if (lectureId.isEmpty() || studentId.isEmpty() || studentName.isEmpty()) {
    webServer.send(400, "application/json", "{\"ok\":false,\"error\":\"Missing fields\"}");
    return;
  }

  int8_t rssi = getClientRssi();
  if (rssi < RSSI_THRESHOLD) {
    webServer.send(200, "application/json", "{\"ok\":false,\"error\":\"Out of range (RSSI " + String(rssi) + " dBm)\"}");
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    webServer.send(200, "application/json", "{\"ok\":false,\"error\":\"No internet\"}");
    return;
  }

  String responseBody = callSupabaseCheckin(lectureId, studentId, studentName, password);
  String okValue = extractJsonValue(responseBody, "ok");
  String status = extractJsonValue(responseBody, "status");
  String note = extractJsonValue(responseBody, "note");

  if (okValue == "true" || okValue == "1") {
    String stationMac = getFirstConnectedStationMac();
    upsertDeviceLock(lectureId, stationMac, studentId, studentName, status, note);
  }

  webServer.send(200, "application/json", responseBody);
}

void handleNextPage() {
  String lectureId = webServer.arg("lectureId");
  String studentId = webServer.arg("studentId");
  String studentName = webServer.arg("studentName");
  String status = webServer.arg("status");
  String note = webServer.arg("note");

  webServer.send(200, "text/html", buildNextHtml(lectureId, studentId, studentName, status, note));
}

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.printf("\n[AP] %s  IP: %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());

  WiFi.begin(STA_SSID, STA_PASS);
  Serial.print("[STA] Connecting");
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) {
    delay(400);
    Serial.print('.');
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\n[STA] Connected" : "\n[STA] No internet");

  webServer.on("/", HTTP_GET, handlePortal);
  webServer.on("/portal", HTTP_GET, handlePortal);
  webServer.on("/lecture-info", HTTP_GET, handleLectureInfo);
  webServer.on("/lock-status", HTTP_GET, handleLockStatus);
  webServer.on("/submit", HTTP_POST, handleSubmit);
  webServer.on("/next", HTTP_GET, handleNextPage);
  webServer.begin();

  Serial.println("[Gate] Ready: http://192.168.4.1/");
}

void loop() {
  webServer.handleClient();
  delay(10);
}
