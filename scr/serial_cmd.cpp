#include "serial_cmd.h"
#include "app_state.h"
#include "logger.h"
#include "fb_send.h"
#include "utils.h"
#include "cookie.h"
#include "config.h"

void printSerialHelp() {
  Serial.println();
  Serial.println("╔══════════ SERIAL COMMAND ══════════");
  Serial.println("║ Gõ text bất kỳ + Enter → gửi vào group");
  Serial.println("║ Hoặc dùng lệnh có dấu '/' ở đầu:");
  Serial.println("║   /help     → hiện menu này");
  Serial.println("║   /info     → in RAM + trạng thái");
  Serial.println("║   /random   → gửi 1 câu random vào group");
  Serial.println("║   /ping     → test bot sống không");
  Serial.println("║   /reconnect→ ngắt WS, buộc reconnect");
  Serial.println("║   /clear    → xóa buffer serial");
  Serial.println("║   /cookie   → in cookie đang dùng (debug)");
  Serial.println("║ (Enter trống sẽ bị bỏ qua)");
  Serial.println("╚════════════════════════════════════");
  Serial.println();
}

void handleSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("/")) {
    String lc = cmd;
    lc.toLowerCase();

    if (lc == "/help" || lc == "/?") { printSerialHelp(); return; }
    if (lc == "/info") {
      logRAMFull("SERIAL /info");
      Serial.printf("UID          : %s\n", g_uid.c_str());
      Serial.printf("Sync token   : %s\n", g_syncToken.length() > 0 ? "YES" : "NO");
      Serial.printf("Last seq id  : %s\n", g_lastSeqId.c_str());
      Serial.printf("MQTT         : %s\n", mqttConnected ? "connected" : "disconnected");
      Serial.printf("WS           : %s\n", wsClient.connected() ? "connected" : "disconnected");
      Serial.println();
      return;
    }
    if (lc == "/cookie") {
      Serial.println("🍪 Cookie hiện tại:");
      Serial.println(g_cookies);
      Serial.printf("   c_user    = %s\n", extractCookieValue(g_cookies, "c_user").c_str());
      Serial.printf("   datr      = %s\n", extractCookieValue(g_cookies, "datr").c_str());
      Serial.printf("   xs        = %.50s...\n", extractCookieValue(g_cookies, "xs").c_str());
      return;
    }
    if (lc == "/random") {
      String m = getRandomMessage();
      Serial.printf("🎲 Random: %s\n", m.c_str());
      sendGroupMessage(String(TARGET_THREAD_ID), m);
      return;
    }
    if (lc == "/ping") {
      Serial.println("🏓 pong! Bot đang chạy bình thường.");
      logRAM("serial ping");
      return;
    }
    if (lc == "/reconnect") {
      Serial.println("♻️  Force WS reconnect...");
      wsClient.stop();
      mqttConnected = false;
      lastReconnectMs = millis() - 11000;
      return;
    }
    if (lc == "/clear") {
      g_serialBuf = "";
      Serial.println("🧹 Cleared serial buffer");
      return;
    }

    Serial.printf("❓ Lệnh không biết: %s (gõ /help để xem menu)\n", cmd.c_str());
    return;
  }

  Serial.printf("📤 [Serial→Group] '%s'\n", cmd.c_str());

  if (!wsClient.connected() || g_cookies.length() == 0) {
    Serial.println("   ⚠️ Bot chưa sẵn sàng (WS/cookie chưa OK)");
    return;
  }

  sendGroupMessage(String(TARGET_THREAD_ID), cmd);
}

void pollSerialInput() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (g_serialBuf.length() > 0) {
        handleSerialCommand(g_serialBuf);
        g_serialBuf = "";
      }
    } else {
      if (c >= 32 && c <= 126) {
        if (g_serialBuf.length() < 500) {
          g_serialBuf += c;
        } else {
          Serial.println("⚠️ Dòng quá dài, reset buffer");
          g_serialBuf = "";
        }
      }
    }
  }
}