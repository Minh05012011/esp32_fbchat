#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>

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

// ================================================================
//  SETUP
// ================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  randomSeed(esp_random());

  Serial.println("\n=== ESP32 FB LISTENER + AUTO COOKIE ===");
  Serial.printf("🎯 Group: %s\n", TARGET_THREAD_ID);
  Serial.printf("🤖 Auto-reply: %s\n", AUTO_REPLY ? "ON" : "OFF");

  logRAMFull("BOOT");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\n✅ WiFi | IP=%s\n", WiFi.localIP().toString().c_str());
  logRAM("sau WiFi");

  syncTimeFromHTTP();

  g_fbDtsg  = String(FB_DTSG_HARDCODED);
  g_jazoest = String(FB_JAZOEST_HARDCODED);
  g_rev     = String(FB_REV_HARDCODED);

  Serial.println("\n=== [0] Parse MANUAL_COOKIE ===");
  if (!parseCookieAndFill(String(MANUAL_COOKIE))) {
    Serial.println("❌ Cookie không hợp lệ → DỪNG");
    return;
  }

  Serial.println("\n=== [1] Verify cookie ===");
  String seq = verifyCookie();

  if (seq.length() == 0) {
    Serial.println("❌ Cookie fail → Login");
    if (!doLogin()) {
      Serial.println("❌ Login fail → DỪNG");
      return;
    }
    seq = verifyCookie();
    if (seq.length() == 0) {
      Serial.println("❌ Verify sau login vẫn fail → DỪNG");
      return;
    }
  }

  g_syncSequenceId = seq;
  g_lastSeqId = seq;
  g_syncToken = "";
  Serial.printf("✅ seq=%s\n", g_lastSeqId.c_str());

  Serial.println("\n=== [2] WS connect ===");
  String sid = genSessionId();
  if (!wsConnect(sid)) {
    Serial.println("❌ WS connect fail");
    return;
  }

  Serial.println("\n=== [3] MQTT CONNECT ===");
  sendMqttConnect();

  lastPing = millis();
  lastReconnectMs = millis();
  lastRamLogMs = millis();

  logRAMFull("SETUP DONE");

  printSerialHelp();
  Serial.println("💡 Sẵn sàng! Gõ 'hello' + Enter để test gửi vào group.");
}

// ================================================================
//  LOOP
// ================================================================
void loop() {
  pollSerialInput();

  if (!wsClient.connected()) {
    if (millis() - lastReconnectMs > 10000) {
      lastReconnectMs = millis();
      g_wsReconnects++;
      Serial.println("🔌 WS mất kết nối → reconnect...");
      logRAM("trc WS reconnect");
      mqttConnected = false;
      wsRxBuf = "";
      mqttRxBuffer = "";

      String seq = verifyCookie();
      if (seq.length() > 0) g_lastSeqId = seq;
      g_syncToken = "";

      String sid = genSessionId();
      if (wsConnect(sid)) sendMqttConnect();
    }
    delay(10);
    return;
  }

  String frame;
  uint8_t op = wsPoll(frame);
  if (op == 0x2 || op == 0x1) {
    mqttRxBuffer += frame;
    processMqttBuffer();
  } else if (op == 0x8) {
    Serial.println("[WS] Close");
    mqttConnected = false;
  }

  if (mqttConnected && millis() - lastPing > 9000) {
    lastPing = millis();
    // ping MQTT
    String pkt = String((char)0xC0) + String((char)0x00);
    wsSendFrame(0x2, (const uint8_t*)pkt.c_str(), pkt.length());
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
