#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include "storage.h"
#include "setup_portal.h"
#include "config.h"
#include "app_state.h"
#include "logger.h"
#include "utils.h"
#include "cookie.h"
#include "fb_auth.h"
#include "fb_send.h"
#include "ws_client.h"
#include "mqtt.h"
#include "serial_cmd.h"
#include "gemini.h"
#include "setup_portal.h"   // ← cần cho runSetupPortal()
// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  randomSeed(esp_random());

  Serial.println("\n=== ESP32 FB LISTENER ===");
  Serial.printf("🎯 Group: %s\n", TARGET_THREAD_ID);
  Serial.printf("🤖 Auto-reply: %s\n", AUTO_REPLY ? "ON" : "OFF");

  logRAMFull("BOOT");

  // ============================================================
  //  [NVS] Load config từ flash
  // ============================================================
  Serial.println("\n=== [NVS] Load config ===");
  storagePrintInfo();

  FBConfig cfg;
  bool hasNvs = storageLoad(cfg);

  String cookieToUse;
  if (hasNvs) {
    Serial.println("✅ Dùng config từ NVS");
    cookieToUse = cfg.cookie;
    g_fbDtsg    = cfg.dtsg;
    g_jazoest   = cfg.jazoest;
    g_rev       = cfg.rev;
  } else {
    Serial.println("⚠️ NVS trống → dùng config.h");
    cookieToUse = String(MANUAL_COOKIE);
    g_fbDtsg    = String(FB_DTSG_HARDCODED);
    g_jazoest   = String(FB_JAZOEST_HARDCODED);
    g_rev       = String(FB_REV_HARDCODED);
  }

  // ============================================================
  //  WiFi + Static IP
  // ============================================================
  WiFi.mode(WIFI_STA);

  IPAddress local_IP(192, 168, 1, 11);
  IPAddress gateway (192, 168, 1, 1);
  IPAddress subnet  (255, 255, 255, 0);
  IPAddress dns1    (8, 8, 8, 8);
  IPAddress dns2    (1, 1, 1, 1);

  if (WiFi.config(local_IP, gateway, subnet, dns1, dns2)) {
    Serial.println("✅ Static IP: 192.168.1.11");
  } else {
    Serial.println("⚠️ WiFi.config FAIL — dùng DHCP");
  }

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\n✅ WiFi | IP=%s\n", WiFi.localIP().toString().c_str());
  Serial.printf("📡 MAC  | %s\n", WiFi.macAddress().c_str());
  logRAM("sau WiFi");

  syncTimeFromHTTP();

  // ============================================================
  //  [0] Parse cookie
  // ============================================================
  Serial.println("\n=== [0] Parse cookie ===");
  if (!parseCookieAndFill(cookieToUse)) {
    Serial.println("❌ Cookie format sai → SETUP PORTAL");
    runSetupPortal();
    return;
  }

  // ============================================================
  //  [1] Verify cookie
  // ============================================================
  Serial.println("\n=== [1] Verify cookie ===");
  String seq = verifyCookie();

  if (seq.length() == 0) {
    Serial.println("❌ Cookie fail → Login");
    if (!doLogin()) {
      Serial.println("❌ Login fail → SETUP PORTAL");
      logRAMFull("trc portal");
      runSetupPortal();
      return;
    }
    seq = verifyCookie();
    if (seq.length() == 0) {
      Serial.println("❌ Verify sau login vẫn fail → SETUP PORTAL");
      runSetupPortal();
      return;
    }
  }

  g_syncSequenceId = seq;
  g_lastSeqId = seq;
  g_syncToken = "";
  Serial.printf("✅ seq=%s\n", g_lastSeqId.c_str());

  // ============================================================
  //  [2] WS connect
  // ============================================================
  Serial.println("\n=== [2] WS connect ===");
  String sid = genSessionId();
  if (!wsConnect(sid)) {
    Serial.println("❌ WS connect fail");
    return;
  }

  // ============================================================
  //  [3] MQTT CONNECT
  // ============================================================
  Serial.println("\n=== [3] MQTT CONNECT ===");
  sendMqttConnect();

  lastPing = millis();
  lastReconnectMs = millis();
  lastRamLogMs = millis();
  g_lastRebootMs = millis();

  logRAMFull("SETUP DONE");
  printSerialHelp();
  Serial.println("💡 Sẵn sàng!");
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  pollSerialInput();
  #if AUTO_REBOOT_ENABLE
  if (millis() - g_lastRebootMs > AUTO_REBOOT_PERIOD_MS) {

    // Kiểm tra "busy" — chỉ reboot khi hệ thống đang rảnh
    bool busy = false;
    if (g_aiPending)                busy = true;  // đang chờ AI xử lý
    if (g_serialBuf.length() > 0)   busy = true;  // user đang gõ Serial
    if (mqttRxBuffer.length() > 0)  busy = true;  // còn MQTT data chưa parse

    if (!busy) {
      Serial.println();
      Serial.println("╔══════════════════════════════════════════");
      Serial.println("║ 🔄 AUTO REBOOT");
      Serial.printf ("║ Period     : %lu s\n", (unsigned long)(AUTO_REBOOT_PERIOD_MS / 1000));
      Serial.printf ("║ Uptime     : %lu s\n", millis() / 1000);
      Serial.printf ("║ Free heap  : %u bytes\n", (unsigned)ESP.getFreeHeap());
      Serial.printf ("║ MinFree    : %u bytes\n", (unsigned)ESP.getMinFreeHeap());
      Serial.printf ("║ Msg recv   : %u\n", g_msgReceived);
      Serial.printf ("║ Msg sent   : %u\n", g_msgSent);
      Serial.printf ("║ WS reconn  : %u\n", g_wsReconnects);
      Serial.println("╚══════════════════════════════════════════");
      Serial.flush();
      delay(200);
      ESP.restart();
    } else {
      // Log lý do hoãn (mỗi 10s một lần để không spam)
      static unsigned long lastBusyLog = 0;
      if (millis() - lastBusyLog > 10000) {
        lastBusyLog = millis();
        Serial.printf("⏳ [AutoReboot] Hoãn — busy: aiPending=%d serial=%d mqtt=%d\n",
                      (int)g_aiPending,
                      g_serialBuf.length(),
                      mqttRxBuffer.length());
      }
    }
  }
#endif
  // ===== [AI] Ưu tiên xử lý /ai pending =====
  if (g_aiPending) {
    handlePendingAI();
    delay(10);
    return;
  }

  // ==========================================================
  //  WS reconnect — cố định mỗi 5s
  // ==========================================================
    if (!wsClient.connected()) {
    if (millis() - lastReconnectMs > 5000) {
      lastReconnectMs = millis();
      g_wsReconnects++;
      g_consecutiveConnFails++;

      Serial.printf("🔌 WS mất kết nối → reconnect (5s, failCount=%d)...\n",
                    g_consecutiveConnFails);
      logRAM("trc WS reconnect");
      mqttConnected = false;
      wsRxBuf = "";
      mqttRxBuffer = "";

      // ==========================================================
      //  Sau 6 lần fail liên tiếp (~30s) → nghi cookie chết
      //  → verify 1 lần → nếu fail → SETUP PORTAL
      // ==========================================================
      if (g_consecutiveConnFails >= 6) {
        Serial.println();
        Serial.println("⚠️ Fail 6 lần liên tiếp → verify cookie...");
        logRAMFull("trc verify in loop");

        String seq = verifyCookie();

        if (seq.length() == 0) {
          Serial.println("❌ Cookie CHẾT → vào SETUP PORTAL");
          logRAMFull("trc portal (loop)");
          runSetupPortal();   // BLOCKING — không return
          return;
        }

        // Cookie còn sống → reset counter, tiếp tục
        Serial.printf("✅ Cookie còn sống (seq=%s) → reset counter\n",
                      seq.c_str());
        if (seq.length() > 0) g_lastSeqId = seq;
        g_syncToken = "";
        g_consecutiveConnFails = 0;
      }

      String sid = genSessionId();
      if (wsConnect(sid)) {
        sendMqttConnect();
        // Không reset counter ở đây — đợi CONNACK rc=0
      }
    }
    delay(10);
    return;
  }

  // ===== WS frame poll =====
  String frame;
  uint8_t op = wsPoll(frame);
  if (op == 0x2 || op == 0x1) {
    mqttRxBuffer += frame;
    processMqttBuffer();
  } else if (op == 0x8) {
    Serial.println("[WS] Close");
    mqttConnected = false;
    g_syncToken = "";   // ← queue đã bị FB xóa, phải create_queue lại
  }

  // ===== MQTT PINGREQ (application layer) — mỗi 9s =====
  if (mqttConnected && millis() - lastPing > 9000) {
    lastPing = millis();
    String pkt = String((char)0xC0) + String((char)0x00);
    wsSendFrame(0x2, (const uint8_t*)pkt.c_str(), pkt.length());
  }

  // ==========================================================
  //  WS PING (opcode 0x9, transport layer) — mỗi 25s
  // ==========================================================
  static unsigned long lastWsPing = 0;
  if (millis() - lastWsPing > 25000UL) {
    lastWsPing = millis();
    wsSendFrame(0x9, nullptr, 0);   // PING, payload rỗng
    Serial.println("[WS] -> PING");
  }

#if LOG_RAM_PERIODIC
  if (millis() - lastRamLogMs > RAM_LOG_PERIOD_MS) {
    lastRamLogMs = millis();
    char tag[32];
    snprintf(tag, sizeof(tag), "periodic %lus", millis() / 1000);
    logRAM(tag);
  }
#endif

  delay(10);
}