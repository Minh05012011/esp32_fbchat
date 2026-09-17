#include "src/ui/serial_cmd.h"
#include "src/core/app_state.h"
#include "src/core/storage.h"
#include "src/ui/setup_portal.h"
#include "src/core/logger.h"
#include "src/fb_api/fb_send.h"
#include "src/fb_api/fb_reaction.h"       // ← THÊM MỚI
#include "src/core/utils.h"
#include "src/fb_api/cookie.h"
#include "src/core/config.h"
#include "src/ai/gemini.h" 
#include "src/commands/qr_serial.h"       // ← THÊM MỚI (Quick Reply test)
// ============================================================
//  Emoji shortcut map — gõ tên thay vì phải dán emoji
//  (serial terminal nhiều khi không gõ được emoji)
// ============================================================
struct EmojiAlias { const char* name; const char* emoji; };

static const EmojiAlias EMOJI_ALIASES[] = {
  { "like",  "👍" }, { ":like:",  "👍" }, { "thich",  "👍" },
  { "love",  "❤️" }, { ":love:",  "❤️" }, { "tim",    "❤️" },
  { "haha",  "😆" }, { ":haha:",  "😆" }, { "cuoi",   "😆" },
  { "wow",   "😮" }, { ":wow:",   "😮" }, { "oha",    "😮" },
  { "sad",   "😢" }, { ":sad:",   "😢" }, { "buon",   "😢" },
  { "angry", "😡" }, { ":angry:", "😡" }, { "gian",   "😡" },
  { "fire",  "🔥" }, { ":fire:",  "🔥" }, { "lua",    "🔥" },
  { "care",  "🥰" }, { ":care:",  "🥰" },
};
static const int EMOJI_ALIAS_COUNT =
  sizeof(EMOJI_ALIASES) / sizeof(EMOJI_ALIASES[0]);

// Trả về emoji thật nếu `in` là alias, ngược lại trả về chính `in` (raw UTF-8)
static String resolveEmoji(const String& in) {
  String k = in;
  k.trim();
  k.toLowerCase();
  for (int i = 0; i < EMOJI_ALIAS_COUNT; i++) {
    if (k == EMOJI_ALIASES[i].name) return String(EMOJI_ALIASES[i].emoji);
  }
  return in;   // không phải alias → giữ nguyên (dùng cho emoji dán trực tiếp)
}

// Tách chuỗi theo khoảng trắng, trả về mảng token (bỏ token rỗng)
static int splitArgs(const String& s, String* out, int maxOut) {
  int n = 0;
  int i = 0;
  while (i < (int)s.length() && n < maxOut) {
    while (i < (int)s.length() && s[i] == ' ') i++;
    if (i >= (int)s.length()) break;
    int start = i;
    while (i < (int)s.length() && s[i] != ' ') i++;
    out[n++] = s.substring(start, i);
  }
  return n;
}

// ============================================================
//  HELP
// ============================================================
void printSerialHelp() {
  Serial.println();
  Serial.println("╔══════════ SERIAL COMMAND ══════════");
  Serial.println("║ Gõ text bất kỳ + Enter → gửi vào group");
  Serial.println("║ Hoặc dùng lệnh có dấu '/' ở đầu:");
  Serial.println("║   /help                    → hiện menu này");
  Serial.println("║   /info                    → in RAM + trạng thái");
  Serial.println("║   /random                  → gửi 1 câu random vào group");
  Serial.println("║   /ping                    → test bot sống không");
  Serial.println("║   /reconnect               → ngắt WS, buộc reconnect");
  Serial.println("║   /clear                   → xóa buffer serial");
  Serial.println("║   /cookie                  → in cookie đang dùng (debug)");
  Serial.println("║   /react <mid> <emoji>     → thả reaction");
  Serial.println("║   /unreact <mid>           → gỡ reaction");
  Serial.println("║     emoji: có thể dán trực tiếp (👍 ❤️ 😆 ...)");
  Serial.println("║            hoặc dùng tên: like love haha wow sad");
  Serial.println("║            angry fire care");
  Serial.println("║     VD: /react mid.$abc... love");
  Serial.println("║         /react mid.$abc... 👍");
  Serial.println("║   /qr <title>:<payload>;...  → gửi Quick Reply (nút bấm)");
  Serial.println("║     VD: /qr hello:hello");
  Serial.println("║         /qr Bật đèn:ON;Tắt đèn:OFF");
  Serial.println("║ (Enter trống sẽ bị bỏ qua)");
  Serial.println("╚════════════════════════════════════");
  Serial.println();
}

// ============================================================
//  CMD HANDLER
// ============================================================
void handleSerialCommand(String cmd) {
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd.startsWith("/")) {
    // Lấy nguyên dòng gốc (chưa lowercase) để preserve emoji
    String raw = cmd;

    // Tách command (token 0) và lowercase chỉ riêng nó để so sánh
    String args[4];
    int nArgs = splitArgs(raw, args, 4);

    if (nArgs == 0) return;
    String lcCmd = args[0];
    lcCmd.toLowerCase();

    if (lcCmd == "/help" || lcCmd == "/?") { printSerialHelp(); return; }
        if (lcCmd == "/setup") {
      Serial.println("🌐 Mở setup portal...");
      delay(500);
      runSetupPortal();   // BLOCKING
      return;
    }

    if (lcCmd == "/nvs-info") {
      storagePrintInfo();
      return;
    }

    if (lcCmd == "/nvs-clear") {
      storageClear();
      Serial.println("⚠️ Reboot để về config.h...");
      delay(1000);
      ESP.restart();
      return;
    }
    if (lcCmd == "/info") {
      logRAMFull("SERIAL /info");
      Serial.printf("UID          : %s\n", g_uid.c_str());
      Serial.printf("Sync token   : %s\n", g_syncToken.length() > 0 ? "YES" : "NO");
      Serial.printf("Last seq id  : %s\n", g_lastSeqId.c_str());
      Serial.printf("MQTT         : %s\n", mqttConnected ? "connected" : "disconnected");
      Serial.printf("WS           : %s\n", wsClient.connected() ? "connected" : "disconnected");
      Serial.println();
      return;
    }

    if (lcCmd == "/cookie") {
      Serial.println("🍪 Cookie hiện tại:");
      Serial.println(g_cookies);
      Serial.printf("   c_user    = %s\n", extractCookieValue(g_cookies, "c_user").c_str());
      Serial.printf("   datr      = %s\n", extractCookieValue(g_cookies, "datr").c_str());
      Serial.printf("   xs        = %.50s...\n", extractCookieValue(g_cookies, "xs").c_str());
      return;
    }
    if (lcCmd == "/ai") {
      // Lấy phần còn lại của cmd SAU token "/ai"
      int sp = raw.indexOf(' ');
      if (sp < 0) {
        Serial.println("❓ Cú pháp: /ai <câu hỏi>");
        Serial.println("   VD: /ai hôm nay ăn gì ngon?");
        return;
      }
      String prompt = raw.substring(sp + 1);
      prompt.trim();
      Serial.printf("🤖 Gửi Gemini: '%s'\n", prompt.c_str());

      String reply;
      if (geminiAsk(prompt, reply)) {
        Serial.println("╔══════ GEMINI REPLY ══════");
        Serial.println(reply);
        Serial.println("╚══════════════════════════");
      } else {
        Serial.println("❌ Gemini thất bại");
      }
      return;
    }

    // ---------- /ai-info ----------
    if (lcCmd == "/ai-info") {
      geminiPrintInfo();
      return;
    }
    if (lcCmd == "/random") {
      String m = getRandomMessage();
      Serial.printf("🎲 Random: %s\n", m.c_str());
      sendGroupMessage(String(TARGET_THREAD_ID), m);
      return;
    }

    if (lcCmd == "/ping") {
      Serial.println("🏓 pong! Bot đang chạy bình thường.");
      logRAM("serial ping");
      return;
    }

    if (lcCmd == "/reconnect") {
      Serial.println("♻️  Force WS reconnect...");
      wsClient.stop();
      mqttConnected = false;
      lastReconnectMs = millis() - 11000;
      return;
    }

    if (lcCmd == "/clear") {
      g_serialBuf = "";
      Serial.println("🧹 Cleared serial buffer");
      return;
    }

    // ---------- /react <mid> <emoji> ----------
    if (lcCmd == "/react") {
      if (nArgs < 3) {
        Serial.println("❓ Cú pháp: /react <messageID> <emoji>");
        Serial.println("   VD: /react mid.$abc... love");
        Serial.println("       /react mid.$abc... 👍");
        return;
      }
      String mid   = args[1];
      String emoji = resolveEmoji(args[2]);

      Serial.printf("→ react | mid=%s | emoji=%s\n", mid.c_str(), emoji.c_str());
      bool ok = fbReactMessage(mid, emoji, /*removeReaction=*/false);
      Serial.println(ok ? "✅ Done" : "❌ Failed");
      return;
    }

    // ---------- /unreact <mid> ----------
    if (lcCmd == "/unreact") {
      if (nArgs < 2) {
        Serial.println("❓ Cú pháp: /unreact <messageID>");
        return;
      }
      String mid = args[1];
      Serial.printf("→ unreact | mid=%s\n", mid.c_str());
      // Gỡ reaction: Facebook cần biết emoji nào để gỡ, nhưng nếu gửi
      // bất kỳ emoji nào mà mình KHÔNG thả thì FB trả OK luôn (no-op).
      // Python sample cũng hardcode string tuỳ ý vào action REMOVE_REACTION.
      bool ok = fbReactMessage(mid, String("👍"), /*removeReaction=*/true);
      Serial.println(ok ? "✅ Done" : "❌ Failed");
      return;
    }

    // ---------- /qr <title1>:<payload1>;<title2>:<payload2> ----------
    if (lcCmd == "/qr") {
      // Lấy nguyên phần sau "/qr " (không tách theo dấu cách, vì title có
      // thể chứa khoảng trắng) — giống cách /ai lấy prompt.
      int sp = raw.indexOf(' ');
      String spec = (sp < 0) ? "" : raw.substring(sp + 1);
      spec.trim();
      handleQrSerialCommand(String(TARGET_THREAD_ID), spec, "");
      return;
    }

    Serial.printf("❓ Lệnh không biết: %s (gõ /help để xem menu)\n", cmd.c_str());
    return;
  }

  Serial.printf("📤 [Serial→Group] '%s'\n", cmd.c_str());

  if (g_cookies.length() == 0) {
    Serial.println("   ⚠️ Bot chưa sẵn sàng (WS/cookie chưa OK)");
    return;
  }

  sendGroupMessage(String(TARGET_THREAD_ID), cmd);
}

// ============================================================
//  SERIAL POLL  — SỬA ĐỂ NHẬN ĐƯỢC BYTE UTF-8 (emoji)
// ============================================================
void pollSerialInput() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (g_serialBuf.length() > 0) {
        handleSerialCommand(g_serialBuf);
        g_serialBuf = "";
      }
    } else {
      // CŨ: if (c >= 32 && c <= 126)   → chặn byte UTF-8 (emoji)
      // MỚI: nhận cả byte >= 128 để emoji đi qua được.
      //      Chỉ chặn các ký tự điều khiển < 32 và DEL (127).
      uint8_t u = (uint8_t)c;
      if (u >= 32 && u != 127) {
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