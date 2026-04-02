/*  web_server.cpp
 *
 *  Lightweight HTTP server for the ESP32Watch.
 *  Runs on port 80 after WiFi connects.
 *  The WatchLink HTML app polls this server every 5 seconds.
 *
 *  Endpoints:
 *  GET  /all      → JSON: steps, battery, notifications list
 *  GET  /steps    → JSON: { "steps": N }
 *  GET  /battery  → JSON: { "battery": N }
 *  GET  /notifs   → JSON: array of notification objects
 *  POST /notify   → body: {"app":"X","text":"Y","time":"Z"}  → stores notif
 *  POST /clear    → clears all stored notifications
 *  GET  /         → serves the WatchLink HTML webapp itself
 *
 *  No extra libraries needed — uses WiFiServer (built into ESP32 Arduino core).
 *
 *  Data sources: stepcounter.h, battery.h (already in your project).
 */

#include "web_server.h"
#include "stepcounter.h"
#include "battery.h"
#include <WiFi.h>

// ── Notification ring buffer ────────────────────────────────────

static WSNotification s_notifs[WS_NOTIF_MAX];
static int            s_count       = 0;
static bool           s_newFlag     = false;

static void pushNotif(const char* app, const char* text, const char* time) {
    if (s_count < WS_NOTIF_MAX) s_count++;
    for (int i = s_count - 1; i > 0; i--)
        s_notifs[i] = s_notifs[i - 1];
    strncpy(s_notifs[0].app,  app,  WS_NOTIF_APP_LEN  - 1);
    strncpy(s_notifs[0].text, text, WS_NOTIF_TEXT_LEN - 1);
    strncpy(s_notifs[0].time, time, WS_TIME_LEN       - 1);
    s_notifs[0].app [WS_NOTIF_APP_LEN  - 1] = '\0';
    s_notifs[0].text[WS_NOTIF_TEXT_LEN - 1] = '\0';
    s_notifs[0].time[WS_TIME_LEN       - 1] = '\0';
    s_notifs[0].unread = true;
    s_newFlag = true;
}

// ── Server state ────────────────────────────────────────────────

static WiFiServer s_server(80);
static bool       s_running = false;

// ── Tiny JSON helpers ───────────────────────────────────────────

// Extract "key":"value" from a JSON string (no ArduinoJson needed)
static bool jsonGetStr(const String& json, const char* key,
                        char* out, size_t outLen) {
    String pat = "\"";
    pat += key;
    pat += "\":\"";
    int idx = json.indexOf(pat);
    if (idx < 0) return false;
    idx += pat.length();
    int end = json.indexOf('"', idx);
    if (end < 0) return false;
    String val = json.substring(idx, end);
    strncpy(out, val.c_str(), outLen - 1);
    out[outLen - 1] = '\0';
    return true;
}

// Escape a string for JSON output (handles " and \)
static String jsonEscape(const char* s) {
    String out;
    while (*s) {
        if (*s == '"' || *s == '\\') out += '\\';
        out += *s++;
    }
    return out;
}

// ── HTML webapp (stored in flash) ───────────────────────────────
// This is the WatchLink HTML file served at GET /
// Paste your full HTML between the R"HTML( and )HTML" delimiters.
// The ESP32 serves it directly — no separate server needed.

static const char WEBAPP_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8"/>
  <meta name="viewport" content="width=device-width, initial-scale=1.0, viewport-fit=cover"/>
  <meta name="apple-mobile-web-app-capable" content="yes"/>
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent"/>
  <meta name="apple-mobile-web-app-title" content="WatchLink"/>
  <title>WatchLink</title>
  <link href="https://fonts.googleapis.com/css2?family=Syne:wght@700;800&family=DM+Mono:wght@300;400;500&display=swap" rel="stylesheet"/>
  <style>
    :root{--bg:#060a10;--surface:#0d1420;--card:#111927;--accent:#00e5ff;--accent2:#7b61ff;--green:#00ff94;--orange:#ff6b35;--red:#ff4560;--text:#e8f0fe;--muted:#3a4a60;--border:rgba(0,229,255,0.12);}
    *{margin:0;padding:0;box-sizing:border-box;-webkit-tap-highlight-color:transparent;}
    body{background:var(--bg);color:var(--text);font-family:'Syne',sans-serif;min-height:100vh;padding:env(safe-area-inset-top) env(safe-area-inset-right) env(safe-area-inset-bottom) env(safe-area-inset-left);overflow-x:hidden;}
    body::before{content:'';position:fixed;inset:0;background-image:linear-gradient(rgba(0,229,255,0.03) 1px,transparent 1px),linear-gradient(90deg,rgba(0,229,255,0.03) 1px,transparent 1px);background-size:36px 36px;pointer-events:none;z-index:0;}
    .app{max-width:480px;margin:0 auto;padding:20px 16px 100px;position:relative;z-index:1;}
    .header{display:flex;justify-content:space-between;align-items:center;margin-bottom:24px;}
    .logo{font-size:20px;font-weight:800;color:var(--accent);letter-spacing:3px;text-transform:uppercase;}
    .status-pill{display:flex;align-items:center;gap:6px;background:var(--surface);border:1px solid var(--border);border-radius:20px;padding:5px 12px;font-family:'DM Mono',monospace;font-size:10px;letter-spacing:1px;text-transform:uppercase;}
    .status-dot{width:7px;height:7px;border-radius:50%;background:var(--muted);transition:all 0.5s;}
    .status-dot.connected{background:var(--green);box-shadow:0 0 8px var(--green);animation:pulse 2s infinite;}
    .status-dot.error{background:var(--red);}
    @keyframes pulse{0%,100%{opacity:1;transform:scale(1);}50%{opacity:0.6;transform:scale(0.85);}}
    .steps-hero{background:var(--surface);border:1px solid var(--border);border-radius:24px;padding:28px 24px;margin-bottom:16px;text-align:center;position:relative;overflow:hidden;}
    .steps-hero::before{content:'';position:absolute;inset:0;background:radial-gradient(circle at 50% 0%,rgba(0,229,255,0.06),transparent 70%);}
    .steps-num{font-family:'DM Mono',monospace;font-size:56px;font-weight:500;color:var(--green);line-height:1;letter-spacing:-2px;}
    .steps-lbl{font-family:'DM Mono',monospace;font-size:10px;color:var(--muted);letter-spacing:3px;text-transform:uppercase;margin-top:6px;}
    .steps-bar-wrap{margin-top:16px;height:3px;background:rgba(0,229,255,0.08);border-radius:2px;overflow:hidden;}
    .steps-bar-fill{height:100%;background:linear-gradient(90deg,var(--green),var(--accent));border-radius:2px;transition:width 1s ease;}
    .steps-goal{font-family:'DM Mono',monospace;font-size:9px;color:var(--muted);margin-top:6px;letter-spacing:1px;}
    .row{display:flex;gap:12px;margin-bottom:16px;}
    .card{background:var(--surface);border:1px solid var(--border);border-radius:18px;padding:16px;flex:1;position:relative;overflow:hidden;}
    .card-icon{font-size:18px;margin-bottom:6px;}
    .card-val{font-family:'DM Mono',monospace;font-size:26px;color:var(--accent);line-height:1;}
    .card-unit{font-size:13px;color:var(--muted);}
    .card-lbl{font-family:'DM Mono',monospace;font-size:9px;color:var(--muted);letter-spacing:1px;text-transform:uppercase;margin-top:4px;}
    .bat-bar-wrap{height:3px;background:rgba(0,229,255,0.08);border-radius:2px;margin-top:8px;overflow:hidden;}
    .bat-bar-fill{height:100%;background:linear-gradient(90deg,var(--accent),var(--accent2));border-radius:2px;transition:width 1s ease;}
    .section-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;}
    .section-title{font-family:'DM Mono',monospace;font-size:10px;color:var(--accent);letter-spacing:3px;text-transform:uppercase;}
    .notif-count{font-family:'DM Mono',monospace;font-size:10px;color:var(--muted);letter-spacing:1px;}
    .notif-list{display:flex;flex-direction:column;gap:8px;}
    .notif-item{background:var(--card);border:1px solid rgba(123,97,255,0.15);border-radius:14px;padding:12px 14px;display:flex;gap:10px;align-items:flex-start;animation:slideIn 0.3s ease;}
    @keyframes slideIn{from{opacity:0;transform:translateX(-10px);}to{opacity:1;transform:translateX(0);}}
    .notif-icon{font-size:18px;margin-top:1px;flex-shrink:0;}
    .notif-body{flex:1;min-width:0;}
    .notif-app{font-family:'DM Mono',monospace;font-size:9px;color:var(--accent2);letter-spacing:1px;text-transform:uppercase;}
    .notif-text{font-size:13px;color:var(--text);margin-top:3px;line-height:1.4;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;}
    .notif-time{font-family:'DM Mono',monospace;font-size:9px;color:var(--muted);flex-shrink:0;margin-top:2px;}
    .notif-empty{background:var(--surface);border:1px solid var(--border);border-radius:14px;padding:24px;text-align:center;font-family:'DM Mono',monospace;font-size:11px;color:var(--muted);letter-spacing:1px;}
    .send-wrap{position:fixed;bottom:0;left:0;right:0;background:rgba(6,10,16,0.95);border-top:1px solid var(--border);padding:10px 16px 20px;z-index:50;backdrop-filter:blur(10px);}
    .send-row{display:flex;gap:8px;max-width:480px;margin:0 auto;}
    .send-app{background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:10px;font-family:'DM Mono',monospace;font-size:12px;color:var(--text);width:90px;outline:none;}
    .send-text{flex:1;background:var(--surface);border:1px solid var(--border);border-radius:10px;padding:10px;font-family:'DM Mono',monospace;font-size:12px;color:var(--text);outline:none;}
    .send-text::placeholder,.send-app::placeholder{color:var(--muted);}
    .send-btn{background:var(--accent);border:none;border-radius:10px;color:#000;font-family:'DM Mono',monospace;font-size:11px;font-weight:500;padding:10px 14px;cursor:pointer;flex-shrink:0;}
    .send-btn:active{transform:scale(0.96);}
  </style>
</head>
<body>
<div class="app">
  <div class="header">
    <div class="logo">WATCHLINK</div>
    <div class="status-pill">
      <div class="status-dot" id="status-dot"></div>
      <span id="status-text">connecting</span>
    </div>
  </div>
  <div class="steps-hero">
    <div class="steps-num" id="steps-val">—</div>
    <div class="steps-lbl">steps today</div>
    <div class="steps-bar-wrap"><div class="steps-bar-fill" id="steps-bar" style="width:0%"></div></div>
    <div class="steps-goal" id="steps-goal">— / 10,000 goal</div>
  </div>
  <div class="row">
    <div class="card">
      <div class="card-icon">🔋</div>
      <div class="card-val"><span id="bat-val">—</span><span class="card-unit">%</span></div>
      <div class="card-lbl">Watch Battery</div>
      <div class="bat-bar-wrap"><div class="bat-bar-fill" id="bat-bar" style="width:0%"></div></div>
    </div>
    <div class="card" style="display:flex;flex-direction:column;justify-content:center;align-items:center;gap:8px;">
      <div style="font-size:28px;cursor:pointer;" onclick="fetchAll()">🔄</div>
      <div class="card-lbl" id="last-update">tap to refresh</div>
    </div>
  </div>
  <div class="section-header">
    <div class="section-title">// Notifications</div>
    <div class="notif-count" id="notif-count">0 alerts</div>
  </div>
  <div class="notif-list" id="notif-list">
    <div class="notif-empty">No notifications yet.</div>
  </div>
</div>

<div class="send-wrap">
  <div class="send-row">
    <input class="send-app" id="s-app" placeholder="App" value="iPhone"/>
    <input class="send-text" id="s-text" placeholder="Send notification to watch…"/>
    <button class="send-btn" onclick="sendNotif()">SEND</button>
  </div>
</div>

<script>
const GOAL=10000;
const BASE=window.location.origin;
const icons={'whatsapp':'💬','phone':'📞','sms':'📩','messages':'📩','instagram':'📸','telegram':'✈️','mail':'📧','gmail':'📧','iphone':'📱','default':'🔔'};
function getIcon(a){const k=a.toLowerCase();for(const[p,v]of Object.entries(icons))if(k.includes(p))return v;return icons.default;}
function setStatus(s){const d=document.getElementById('status-dot'),t=document.getElementById('status-text');d.className='status-dot '+(s==='ok'?'connected':s==='error'?'error':'');t.textContent=s==='ok'?'connected':s==='error'?'offline':'syncing';}
async function fetchAll(){
  setStatus('syncing');
  try{
    const r=await fetch(BASE+'/all',{signal:AbortSignal.timeout(4000)});
    const d=await r.json();
    setStatus('ok');
    const pct=Math.min((d.steps/GOAL)*100,100);
    document.getElementById('steps-val').textContent=d.steps.toLocaleString();
    document.getElementById('steps-bar').style.width=pct+'%';
    document.getElementById('steps-goal').textContent=d.steps.toLocaleString()+' / '+GOAL.toLocaleString()+' goal';
    document.getElementById('bat-val').textContent=d.battery;
    document.getElementById('bat-bar').style.width=d.battery+'%';
    const bf=document.getElementById('bat-bar');
    if(d.battery<20)bf.style.background='var(--red)';
    else if(d.battery<50)bf.style.background='var(--orange)';
    else bf.style.background='linear-gradient(90deg,var(--accent),var(--accent2))';
    const list=document.getElementById('notif-list'),cnt=document.getElementById('notif-count');
    if(!d.notifications||d.notifications.length===0){list.innerHTML='<div class="notif-empty">No notifications yet.</div>';cnt.textContent='0 alerts';return;}
    cnt.textContent=d.notifications.length+' alert'+(d.notifications.length>1?'s':'');
    list.innerHTML=d.notifications.map(n=>`<div class="notif-item"><div class="notif-icon">${getIcon(n.app)}</div><div class="notif-body"><div class="notif-app">${n.app}</div><div class="notif-text">${n.text}</div></div><div class="notif-time">${n.time}</div></div>`).join('');
    const now=new Date();
    document.getElementById('last-update').textContent=now.getHours().toString().padStart(2,'0')+':'+now.getMinutes().toString().padStart(2,'0')+':'+now.getSeconds().toString().padStart(2,'0');
  }catch(e){setStatus('error');}
}
async function sendNotif(){
  const app=document.getElementById('s-app').value.trim()||'iPhone';
  const text=document.getElementById('s-text').value.trim();
  if(!text)return;
  const now=new Date();
  const time=now.getHours().toString().padStart(2,'0')+':'+now.getMinutes().toString().padStart(2,'0');
  try{
    await fetch(BASE+'/notify',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({app,text,time})});
    document.getElementById('s-text').value='';
    fetchAll();
  }catch(e){}
}
document.getElementById('s-text').addEventListener('keydown',e=>{if(e.key==='Enter')sendNotif();});
fetchAll();
setInterval(fetchAll,5000);
</script>
</body>
</html>
)HTML";

// ── HTTP helpers ────────────────────────────────────────────────

static void sendResponse(WiFiClient& client,
                          int code, const char* ctype,
                          const String& body) {
    client.print("HTTP/1.1 ");
    client.print(code);
    client.println(code == 200 ? " OK" : " Error");
    client.println("Content-Type: " + String(ctype));
    client.println("Access-Control-Allow-Origin: *");
    client.println("Access-Control-Allow-Methods: GET, POST, OPTIONS");
    client.println("Access-Control-Allow-Headers: Content-Type");
    client.println("Connection: close");
    client.println("Content-Length: " + String(body.length()));
    client.println();
    client.print(body);
}

static void sendHTML(WiFiClient& client) {
    // Serve HTML from PROGMEM in chunks to avoid RAM overflow
    String header = "HTTP/1.1 200 OK\r\n";
    header += "Content-Type: text/html\r\n";
    header += "Access-Control-Allow-Origin: *\r\n";
    header += "Connection: close\r\n\r\n";
    client.print(header);

    // Stream PROGMEM string in 256-byte chunks
    size_t len = strlen_P(WEBAPP_HTML);
    size_t sent = 0;
    char buf[256];
    while (sent < len) {
        size_t chunk = min((size_t)256, len - sent);
        memcpy_P(buf, WEBAPP_HTML + sent, chunk);
        client.write((uint8_t*)buf, chunk);
        sent += chunk;
    }
}

// ── Request parser ──────────────────────────────────────────────

static void handleRequest(WiFiClient& client) {
    String requestLine = "";
    String body = "";
    int contentLength = 0;
    bool inHeaders = true;

    unsigned long timeout = millis() + 1500;
    while (client.connected() && millis() < timeout) {
        if (!client.available()) { delay(1); continue; }

        String line = client.readStringUntil('\n');
        line.trim();

        if (requestLine.isEmpty()) {
            requestLine = line;
            continue;
        }

        if (inHeaders) {
            if (line.startsWith("Content-Length:")) {
                contentLength = line.substring(16).toInt();
            }
            if (line.isEmpty()) {
                inHeaders = false;
                if (contentLength > 0) {
                    // Read body
                    unsigned long bodyTimeout = millis() + 500;
                    while ((int)body.length() < contentLength && millis() < bodyTimeout) {
                        if (client.available()) body += (char)client.read();
                    }
                }
                break;
            }
        }
    }

    // Parse method + path
    String method = "", path = "";
    int s1 = requestLine.indexOf(' ');
    int s2 = requestLine.indexOf(' ', s1 + 1);
    if (s1 > 0 && s2 > s1) {
        method = requestLine.substring(0, s1);
        path   = requestLine.substring(s1 + 1, s2);
    }

    Serial.printf("[WS] %s %s\n", method.c_str(), path.c_str());

    // ── OPTIONS preflight (CORS) ──
    if (method == "OPTIONS") {
        sendResponse(client, 200, "text/plain", "");
        return;
    }

    // ── GET / → serve webapp ──
    if (method == "GET" && (path == "/" || path == "/index.html")) {
        sendHTML(client);
        return;
    }

    // ── GET /all → full JSON snapshot ──
    if (method == "GET" && path == "/all") {
        int steps   = (int)sc_getSteps();
        int battery = (int)battery_getPercentage();

        String json = "{\"steps\":" + String(steps) +
                      ",\"battery\":" + String(battery) +
                      ",\"notifications\":[";

        for (int i = 0; i < s_count; i++) {
            if (i > 0) json += ",";
            json += "{\"app\":\"" + jsonEscape(s_notifs[i].app)  + "\"," +
                     "\"text\":\""+ jsonEscape(s_notifs[i].text) + "\"," +
                     "\"time\":\""+ jsonEscape(s_notifs[i].time) + "\"}";
        }
        json += "]}";
        sendResponse(client, 200, "application/json", json);
        return;
    }

    // ── GET /steps ──
    if (method == "GET" && path == "/steps") {
        sendResponse(client, 200, "application/json",
                     "{\"steps\":" + String((int)sc_getSteps()) + "}");
        return;
    }

    // ── GET /battery ──
    if (method == "GET" && path == "/battery") {
        sendResponse(client, 200, "application/json",
                     "{\"battery\":" + String((int)battery_getPercentage()) + "}");
        return;
    }

    // ── GET /notifs ──
    if (method == "GET" && path == "/notifs") {
        String json = "[";
        for (int i = 0; i < s_count; i++) {
            if (i > 0) json += ",";
            json += "{\"app\":\"" + jsonEscape(s_notifs[i].app)  + "\"," +
                     "\"text\":\""+ jsonEscape(s_notifs[i].text) + "\"," +
                     "\"time\":\""+ jsonEscape(s_notifs[i].time) + "\"}";
        }
        json += "]";
        sendResponse(client, 200, "application/json", json);
        return;
    }

    // ── POST /notify → receive notification from webapp ──
    if (method == "POST" && path == "/notify") {
        char app[WS_NOTIF_APP_LEN]   = "App";
        char text[WS_NOTIF_TEXT_LEN] = "";
        char time[WS_TIME_LEN]       = "--:--";

        jsonGetStr(body, "app",  app,  sizeof(app));
        jsonGetStr(body, "text", text, sizeof(text));
        jsonGetStr(body, "time", time, sizeof(time));

        if (strlen(text) > 0) {
            pushNotif(app, text, time);
            Serial.printf("[WS] New notif from %s: %s\n", app, text);
        }
        sendResponse(client, 200, "application/json", "{\"ok\":true}");
        return;
    }

    // ── POST /clear ──
    if (method == "POST" && path == "/clear") {
        ws_clearAll();
        sendResponse(client, 200, "application/json", "{\"ok\":true}");
        return;
    }

    // ── 404 ──
    sendResponse(client, 404, "application/json", "{\"error\":\"not found\"}");
}

// ── Public API ──────────────────────────────────────────────────

void webserver_begin() {
    if (s_running) return;
    s_server.begin();
    s_running = true;
    Serial.println("[WS] Web server started on port 80");
    Serial.print("[WS] Open in browser: http://");
    Serial.println(WiFi.localIP());
}

void webserver_update() {
    if (!s_running) return;

    WiFiClient client = s_server.available();
    if (!client) return;

    // Wait briefly for data
    unsigned long t = millis();
    while (!client.available() && millis() - t < 500) delay(1);

    if (client.available()) {
        handleRequest(client);
    }

    client.stop();
}

void webserver_stop() {
    if (!s_running) return;
    s_server.end();
    s_running = false;
    Serial.println("[WS] Web server stopped");
}

bool webserver_isRunning() { return s_running; }

// ── Notification getters ────────────────────────────────────────

int ws_getNotificationCount() { return s_count; }

const WSNotification* ws_getNotification(int index) {
    if (index < 0 || index >= s_count) return nullptr;
    return &s_notifs[index];
}

void ws_markRead(int index) {
    if (index < 0 || index >= s_count) return;
    s_notifs[index].unread = false;
}

void ws_clearAll() {
    s_count = 0;
    s_newFlag = false;
    memset(s_notifs, 0, sizeof(s_notifs));
}

bool ws_hasNewNotification() {
    bool f = s_newFlag;
    s_newFlag = false;
    return f;
}
