#include "src/commands/qr_serial.h"
#include "src/fb_api/fb_quick_reply.h"

// Giới hạn số nút tối đa 1 lần gửi (đủ dùng cho test qua serial)
#define QR_MAX_BUTTONS 8

// Tách spec theo dấu ';' -> mỗi phần tách tiếp theo dấu ':' (title:payload)
// Nếu không có ':' -> payload = title (giống parse_qr_spec bên Python)
static int parseQrSpec(const String& spec, QuickReplyBtn* out, int maxOut) {
  int n = 0;
  int start = 0;
  int len = spec.length();

  while (start <= len && n < maxOut) {
    int semi = spec.indexOf(';', start);
    String item = (semi < 0) ? spec.substring(start) : spec.substring(start, semi);
    item.trim();

    if (item.length() > 0) {
      int colon = item.indexOf(':');
      QuickReplyBtn btn;
      if (colon >= 0) {
        btn.title   = item.substring(0, colon);
        btn.payload = item.substring(colon + 1);
        btn.title.trim();
        btn.payload.trim();
      } else {
        btn.title   = item;
        btn.payload = item;
      }
      out[n++] = btn;
    }

    if (semi < 0) break;
    start = semi + 1;
  }
  return n;
}

void handleQrSerialCommand(const String& threadId,
                            const String& spec,
                            const String& contentSend) {
  if (threadId.length() == 0) {
    Serial.println("⚠️ Chưa có threadId (TARGET_THREAD_ID rỗng?)");
    return;
  }
  if (spec.length() == 0) {
    Serial.println("⚠️ Dùng: /qr <title1>:<payload1>;<title2>:<payload2>");
    Serial.println("   VD  : /qr hello:hello");
    Serial.println("         /qr Bật:ON;Tắt:OFF");
    return;
  }

  QuickReplyBtn buttons[QR_MAX_BUTTONS];
  int n = parseQrSpec(spec, buttons, QR_MAX_BUTTONS);

  if (n == 0) {
    Serial.println("⚠️ Không parse được nút nào từ spec đã cho");
    return;
  }

  Serial.printf("📤 Đang gửi %d nút:\n", n);
  for (int i = 0; i < n; i++) {
    Serial.printf("   • '%s' -> payload='%s'\n",
                  buttons[i].title.c_str(), buttons[i].payload.c_str());
  }

  String body = (contentSend.length() > 0) ? contentSend : "Chọn một tùy chọn:";
  bool ok = sendQuickReplyMessage(threadId, body, buttons, n);
  Serial.println(ok ? "✅ Đã gửi Quick Reply" : "❌ Gửi thất bại");
}