#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>

#include "src/core/storage.h"
#include "src/core/config.h"
#include "src/core/app_state.h"
#include "src/core/logger.h"
#include "src/core/utils.h"
#include "src/ui/setup_portal.h"
#include "src/ui/serial_cmd.h"
#include "src/fb_api/cookie.h"
#include "src/fb_api/fb_auth.h"
#include "src/fb_api/fb_send.h"
#include "src/fb_api/fb_quick_reply.h"   
#include "src/net/ws_client.h"
#include "src/net/mqtt.h"
#include "src/ai/gemini.h"
#include "src/commands/commands.h"


static unsigned long g_lastPingMs = 0;

static const unsigned long PING_INTERVAL_MS = 4000;

static void sendMqttPing() {
  uint8_t pkt[2] = { 0xC0, 0x00 }; // PINGREQ — fixed header only
  wsSendFrame(0x2, pkt, sizeof(pkt));
  Serial.println("[MQTT] -> PINGREQ");
}

static bool connectWsAndMqtt() {
  String sid = genSessionId();
  if (!wsConnect(sid)) {
    Serial.println("❌ [WS] Connect fail");
    return false;
  }
  mqttRxBuffer = "";

  g_syncToken  = "";
  sendMqttConnect();
  g_lastPingMs = millis();
  return true;
}

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  randomSeed(esp_random());

  Serial.println("\n=== ESP32 FB LISTENER (WS + MQTT + Quick Reply test) ===");
  Serial.printf("🎯 Group: %s\n", TARGET_THREAD_ID);
  Serial.printf("🤖 Auto-reply: %s\n", AUTO_REPLY ? "ON" : "OFF");

  logRAMFull("BOOT");

  // ============================================================
  //  [NVS] Load config
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
  //  [1] Verify cookie (fallback login)
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
  g_lastSeqId      = seq;
  g_syncToken      = "";
  Serial.printf("✅ uid=%s seq=%s\n", g_uid.c_str(), g_lastSeqId.c_str());

  // ============================================================
  //  [2] WS + MQTT connect
  // ============================================================
  Serial.println("\n=== [2] Connect WS + MQTT ===");
  if (!connectWsAndMqtt()) {
    Serial.println("⚠️ Connect lần đầu fail — sẽ tự retry trong loop()");
  }

  // ============================================================
  //  Timers
  // ============================================================
  lastRamLogMs   = millis();
  g_lastRebootMs = millis();

  logRAMFull("SETUP DONE");
  printSerialHelp();
  Serial.println("💡 Sẵn sàng! Gõ /qr hello:hello để test Quick Reply.");
  Serial.println();
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  pollSerialInput();

  // ==========================================================
  //  Auto-reboot (chỉ khi rảnh)
  // ==========================================================
#if AUTO_REBOOT_ENABLE
  if (millis() - g_lastRebootMs > AUTO_REBOOT_PERIOD_MS) {
    bool busy = false;
    if (g_aiPending)                busy = true;
    if (g_serialBuf.length() > 0)   busy = true;

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
      Serial.println("╚══════════════════════════════════════════");
      Serial.flush();
      delay(200);
      ESP.restart();
    } else {
      static unsigned long lastBusyLog = 0;
      if (millis() - lastBusyLog > 10000) {
        lastBusyLog = millis();
        Serial.printf("⏳ [AutoReboot] Hoãn — busy: aiPending=%d serial=%d\n",
                      (int)g_aiPending,
                      g_serialBuf.length());
      }
    }
  }
#endif

  // ==========================================================
  //  AI pending — ưu tiên
  // ==========================================================
  if (g_aiPending) {
    handlePendingAI();
    delay(10);
    return;
  }

  // ==========================================================
  //  Reconnect WS nếu rớt
  // ==========================================================
  if (!wsClient.connected()) {
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 3000) {
      lastRetry = millis();
      Serial.println("🔄 [WS] Mất kết nối — thử reconnect...");
      mqttConnected = false;
      connectWsAndMqtt();
    }
    delay(50);
    return;
  }

  // ==========================================================
  //  Đọc dữ liệu WS -> gom vào mqttRxBuffer -> parse MQTT
  // ==========================================================
  String payload;
  uint8_t opcode = wsPoll(payload);
  if ((opcode == 0x1 || opcode == 0x2) && payload.length() > 0) {
    mqttRxBuffer += payload;
    processMqttBuffer();
  } else if (opcode == 0x8) {
    mqttConnected = false;
  }

  // ==========================================================
  //  Giữ kết nối sống bằng PINGREQ (mỗi 8s — khớp keepalive=10s)
  // ==========================================================
  if (mqttConnected && millis() - g_lastPingMs > PING_INTERVAL_MS) {
    sendMqttPing();
    g_lastPingMs = millis();
  }

  // ==========================================================
  //  RAM log periodic
  // ==========================================================
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