#include "src/ui/setup_portal.h"
#include "src/core/app_state.h"
#include "src/core/storage.h"
#include "src/core/logger.h"
#include "src/core/config.h"

#include <WiFi.h>
#include <WebServer.h>

static WebServer* g_server = nullptr;

static const char HTML_FORM[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>ESP32 FB Setup</title>
<style>
  body{font-family:sans-serif;max-width:640px;margin:40px auto;padding:0 20px;background:#f5f5f5}
  h1{color:#0066cc}
  textarea,input{width:100%;box-sizing:border-box;padding:8px;font-family:monospace;
    font-size:13px;border:1px solid #ccc;border-radius:4px;background:#fff}
  label{display:block;margin-top:14px;font-weight:bold;color:#333}
  button{margin-top:24px;padding:14px 32px;background:#0066cc;color:#fff;
    border:none;font-size:16px;border-radius:4px;cursor:pointer}
  button:hover{background:#0055aa}
  .hint{color:#666;font-size:12px;margin-top:4px}
  .box{background:#fff;padding:24px;border-radius:8px;box-shadow:0 2px 8px rgba(0,0,0,0.1)}
</style></head><body>
<div class="box">
<h1>ESP32 FB Setup</h1>
<p>Nhập cookie và token Facebook mới:</p>
<form method="POST" action="/save">
  <label>Cookie Facebook (bắt buộc)</label>
  <textarea name="cookie" rows="6" required
    placeholder="datr=...; sb=...; c_user=...; xs=...; fr=...;"></textarea>
  <div class="hint">Chuỗi cookie đầy đủ, cách nhau bởi dấu <code>;</code></div>

  <label>fb_dtsg</label>
  <textarea name="dtsg" rows="2" placeholder="NAfxQr..."></textarea>
  <div class="hint">Để trống nếu không đổi</div>

  <label>jazoest</label>
  <input type="text" name="jazoest" placeholder="25852">
  <div class="hint">Để trống nếu không đổi</div>

  <label>rev</label>
  <input type="text" name="rev" placeholder="1047410072">
  <div class="hint">Để trống nếu không đổi</div>

  <button type="submit">💾 Lưu và Reboot</button>
</form>
</div></body></html>)HTML";

static const char HTML_OK[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>OK</title>
<style>body{font-family:sans-serif;text-align:center;padding:80px 20px;background:#f5f5f5}
h1{color:#00aa00;font-size:48px}
.box{background:#fff;padding:40px;border-radius:8px;display:inline-block;
  box-shadow:0 2px 8px rgba(0,0,0,0.1)}</style></head><body>
<div class="box">
<h1>✅ OK</h1>
<p>Đã lưu cookie. ESP32 sẽ reboot sau 2 giây...</p>
</div></body></html>)HTML";

static const char HTML_FAIL[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><title>Fail</title>
<style>body{font-family:sans-serif;text-align:center;padding:80px 20px;background:#f5f5f5}
h1{color:#cc0000;font-size:48px}
.box{background:#fff;padding:40px;border-radius:8px;display:inline-block;
  box-shadow:0 2px 8px rgba(0,0,0,0.1)}</style></head><body>
<div class="box">
<h1>❌ FAIL</h1>
<p>Thiếu cookie hoặc cookie quá ngắn.</p>
<a href="/new_cookie">← Quay lại</a>
</div></body></html>)HTML";

static void handleRoot() {
  Serial.println("[Portal] GET /");
  g_server->send_P(200, "text/html; charset=utf-8", HTML_FORM);
}

static void handleNewCookie() {
  Serial.println("[Portal] GET /new_cookie");
  g_server->send_P(200, "text/html; charset=utf-8", HTML_FORM);
}

static void handleSave() {
  Serial.println();
  Serial.println("╔══════════════════════════════════════════");
  Serial.println("║ 📥 POST /save");
  Serial.println("╚══════════════════════════════════════════");

  String cookie  = g_server->arg("cookie");
  String dtsg    = g_server->arg("dtsg");
  String jazoest = g_server->arg("jazoest");
  String rev     = g_server->arg("rev");

  cookie.trim(); dtsg.trim(); jazoest.trim(); rev.trim();

  Serial.printf("   cookie  : %d bytes | %.60s...\n",
                cookie.length(), cookie.c_str());
  Serial.printf("   dtsg    : %d bytes\n", dtsg.length());
  Serial.printf("   jazoest : %d bytes | %s\n", jazoest.length(), jazoest.c_str());
  Serial.printf("   rev     : %d bytes | %s\n", rev.length(), rev.c_str());

  if (cookie.length() < 20) {
    Serial.println("   ❌ cookie quá ngắn (<20 bytes)");
    g_server->send_P(400, "text/html; charset=utf-8", HTML_FAIL);
    return;
  }

  if (dtsg.length() == 0)    dtsg    = String(FB_DTSG_HARDCODED);
  if (jazoest.length() == 0) jazoest = String(FB_JAZOEST_HARDCODED);
  if (rev.length() == 0)     rev     = String(FB_REV_HARDCODED);

  FBConfig cfg;
  cfg.cookie = cookie;
  cfg.dtsg = dtsg;
  cfg.jazoest = jazoest;
  cfg.rev = rev;

  storageSave(cfg);

  g_server->send_P(200, "text/html; charset=utf-8", HTML_OK);
  Serial.println("✅ [Portal] Đã gửi OK. Reboot sau 2s...");
  Serial.flush();

  delay(2000);
  ESP.restart();
}

static void handleNotFound() {
  Serial.printf("[Portal] 404 %s\n", g_server->uri().c_str());
  g_server->send(404, "text/plain; charset=utf-8", "Not found");
}

void runSetupPortal() {
  Serial.println();
  Serial.println("╔══════════════════════════════════════════");
  Serial.println("║ 🌐 SETUP PORTAL — WEB SERVER");
  Serial.println("╚══════════════════════════════════════════");

  logRAMFull("trc portal");

  // Đảm bảo WiFi kết nối
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("⚠️ WiFi mất kết nối → thử lại...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ Không kết nối được WiFi");
    while (true) { delay(1000); }
  }

  String ip = WiFi.localIP().toString();

  Serial.println("╔══════════════════════════════════════════");
  Serial.printf ("║ IP      : %s\n", ip.c_str());
  Serial.printf ("║ SSID    : %s\n", WiFi.SSID().c_str());
  Serial.printf ("║ RSSI    : %d dBm\n", WiFi.RSSI());
  Serial.println("║ ────────────────────────────────────────");
  Serial.println("║ MỞ BROWSER:");
  Serial.printf ("║   http://%s/new_cookie\n", ip.c_str());
  Serial.println("║ ────────────────────────────────────────");
  Serial.println("║ HOẶC PYTHON POST:");
  Serial.printf ("║   http://%s/save\n", ip.c_str());
  Serial.println("╚══════════════════════════════════════════");

  static WebServer server(80);
  g_server = &server;

  server.on("/",           HTTP_GET,  handleRoot);
  server.on("/new_cookie", HTTP_GET,  handleNewCookie);
  server.on("/save",       HTTP_POST, handleSave);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[Portal] HTTP server: port 80");
  Serial.println("[Portal] Đang chờ cookie mới...");
  Serial.flush();

  unsigned long lastWifiCheck = 0;
  while (true) {
    server.handleClient();

    if (millis() - lastWifiCheck > 5000) {
      lastWifiCheck = millis();
      if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️ WiFi rớt — reconnect...");
        WiFi.reconnect();
      }
    }
    delay(2);
  }
}