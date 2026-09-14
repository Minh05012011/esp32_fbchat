#include "src/commands/commands.h"
#include "src/core/app_state.h"
#include "src/core/logger.h"
#include "src/core/config.h"
#include "src/core/utils.h"
#include "src/fb_api/fb_send.h"
#include "src/fb_api/fb_reaction.h"
#include "src/ai/gemini.h"

// ============================================================
//  Ack helper — có mid thì thả tim, không có thì gửi text
// ============================================================
static void sendAck(const String& threadId, const String& mid) {
  // Chỉ thả tim khi có mid THẬT (không phải mid giả dạng "ts:...")
  if (mid.length() > 0 && !mid.startsWith("ts:")) {
    Serial.printf("   ❤️  Thả tim vào mid=%s\n", mid.c_str());
    bool ok = fbReactMessage(mid, String("❤️"), /*removeReaction=*/false);
    Serial.printf("   → react %s\n", ok ? "OK" : "FAIL");
    if (ok) return;   // thành công → không gửi text nữa
    Serial.println("   ⚠️ React fail → fallback gửi text");
  }
  // Không có mid thật → gửi text
  sendGroupMessage(threadId, "⚡ Đã nhận lệnh, đang xử lý...");
}

// ============================================================
//  Dispatch lệnh
// ============================================================
void handleGroupCommand(const String& threadId,
                        const String& actorId,
                        const String& body,
                        const String& mid,
                        long long timestamp) {

  String bodyStr = body;
  bodyStr.trim();

  // ==========================================================
  //  /q — Groq
  // ==========================================================
  if (bodyStr == "/q" || bodyStr.startsWith("/q ")) {
    if (g_aiPending) {
      Serial.println("   ⏳ /q: Đang xử lý câu trước, bỏ qua");
      sendGroupMessage(threadId, "⏳ Đang xử lý câu trước, đợi chút nhé!");
      return;
    }

    String prompt = (bodyStr.length() > 2) ? bodyStr.substring(3) : "";
    prompt.trim();
    if (prompt.length() == 0) {
      Serial.println("   ❓ /q thiếu prompt");
      sendGroupMessage(threadId, "❓ Cú pháp: /q <câu hỏi>");
      return;
    }

    sendAck(threadId, mid);

    g_aiPending      = true;
    g_aiService      = "groq";
    g_aiPrompt       = prompt;
    g_aiThreadId     = threadId;
    g_aiReplyToMsgId = mid;

    Serial.printf("   ✅ /q pending | prompt='%.60s'\n", prompt.c_str());
    return;
  }

  // ==========================================================
  //  /ai — Gemini
  // ==========================================================
  if (bodyStr == "/ai" || bodyStr.startsWith("/ai ")) {
    if (g_aiPending) {
      Serial.println("   ⏳ /ai: Đang xử lý câu trước, bỏ qua");
      sendGroupMessage(threadId, "⏳ Đang xử lý câu trước, đợi chút nhé!");
      return;
    }

    String prompt = (bodyStr.length() > 3) ? bodyStr.substring(4) : "";
    prompt.trim();
    if (prompt.length() == 0) {
      Serial.println("   ❓ /ai thiếu prompt");
      sendGroupMessage(threadId, "❓ Cú pháp: /ai <câu hỏi>");
      return;
    }

    sendAck(threadId, mid);

    g_aiPending      = true;
    g_aiService      = "gemini";
    g_aiPrompt       = prompt;
    g_aiThreadId     = threadId;
    g_aiReplyToMsgId = mid;

    Serial.printf("   ✅ /ai pending | prompt='%.60s'\n", prompt.c_str());
    return;
  }

  // ==========================================================
  //  AUTO_REPLY — auto reply Gemini cho mọi tin (config đang off)
  // ==========================================================
#if AUTO_REPLY
  if (bodyStr.length() >= 2) {
    String userMsg = bodyStr;
    if (userMsg.length() > 300) userMsg = userMsg.substring(0, 300);

    Serial.println("   🤖 AUTO_REPLY: gọi Gemini...");
    String reply;
    if (geminiAsk(userMsg, reply)) {
      Serial.printf("   ↩️  Reply: %.80s\n", reply.c_str());
      sendGroupMessage(threadId, reply);
    } else {
      Serial.println("   ⚠️ Gemini fail → câu dự phòng");
      sendGroupMessage(threadId, getRandomMessage());
    }
  }
#endif

  // Tin nhắn thường — bỏ qua
  Serial.printf("   (bỏ qua tin không phải lệnh: '%.60s')\n", bodyStr.c_str());
}