#include "src/ai/gemini.h"
#include "src/core/app_state.h"
#include "src/core/logger.h"
#include "src/core/utils.h"
#include "src/core/config.h"
#include "src/fb_api/fb_send.h"
#include "src/net/ws_client.h"
#include "src/ai/groq.h" 
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
//  JSON escape
// ============================================================
static String jsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 16);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:
        if ((uint8_t)c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", (uint8_t)c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// ============================================================
//  geminiAsk — gọi API Gemini, trả về câu trả lời
// ============================================================
bool geminiAsk(const String& prompt, String& outReply) {
  outReply = "";
  unsigned long tStart = millis();

  Serial.println("╔══════════════════════════════════════════");
  Serial.println("║ 🤖 GEMINI ASK");
  Serial.println("╠══════════════════════════════════════════");
  Serial.printf ("║ Model      : %s\n", GEMINI_MODEL);
  Serial.printf ("║ MaxTokens  : %d\n", GEMINI_MAX_TOKENS);
  Serial.printf ("║ Prompt len : %d chars\n", prompt.length());
  Serial.printf ("║ Prompt     : %.120s%s\n",
                 prompt.c_str(),
                 prompt.length() > 120 ? "..." : "");
  Serial.printf ("║ Free heap  : %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.printf ("║ MaxBlk     : %u bytes\n", (unsigned)ESP.getMaxAllocHeap());
  Serial.println("╚══════════════════════════════════════════");

  logRAM("trc gemini");

  // ---- Validate input ----
  if (prompt.length() == 0) {
    Serial.println("❌ [Gemini] prompt rỗng → abort");
    return false;
  }
  if (String(GEMINI_API_KEY).length() < 10) {
    Serial.println("❌ [Gemini] Chưa cấu hình GEMINI_API_KEY");
    return false;
  }

  // ---- Build JSON body ----
  String body;
  body.reserve(512 + prompt.length());
  body += "{\"contents\":[{\"parts\":[{\"text\":\"";
  body += jsonEscape(prompt);
  body += "\"}]}],\"systemInstruction\":{\"parts\":[{\"text\":\"";
  body += jsonEscape(String(GEMINI_SYSTEM_PROMPT));
  body += "\"}]},\"generationConfig\":{\"maxOutputTokens\":";
  body += String(GEMINI_MAX_TOKENS);
  body += ",\"temperature\":0.8}}";

  Serial.printf("📦 [Gemini] Body size: %d bytes\n", body.length());

  // ---- Build URL ----
  String url = "https://generativelanguage.googleapis.com/v1beta/models/";
  url += GEMINI_MODEL;
  url += ":generateContent";
  Serial.printf("🌐 [Gemini] URL: %s\n", url.c_str());

  // ---- HTTP POST ----
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(GEMINI_TIMEOUT_MS / 1000);  // seconds

  HTTPClient https;
  https.setConnectTimeout(15000);
  https.setTimeout(GEMINI_TIMEOUT_MS);
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  Serial.println("🔌 [Gemini] https.begin()...");
  if (!https.begin(client, url)) {
    Serial.println("❌ [Gemini] https.begin FAIL");
    logRAM("gemini begin fail");
    return false;
  }
  Serial.println("✅ [Gemini] begin OK");

  https.addHeader("Content-Type",   "application/json");
  https.addHeader("Accept",         "application/json");
  https.addHeader("x-goog-api-key", GEMINI_API_KEY);

  Serial.printf("📤 [Gemini] POST %d bytes...\n", body.length());
  unsigned long tPost = millis();

  int code = https.POST((uint8_t*)body.c_str(), body.length());

  unsigned long tPostDone = millis();
  Serial.printf("⏱️  [Gemini] POST done in %lums | code=%d\n",
                tPostDone - tPost, code);

  if (code <= 0) {
    Serial.printf("❌ [Gemini] HTTP code=%d (<=0)\n", code);
    Serial.printf("   errorToString: %s\n",
                  https.errorToString(code).c_str());
    https.end();
    logRAM("gemini http fail");
    return false;
  }

  String resp = https.getString();
  https.end();

  Serial.printf("📥 [Gemini] Response: %d bytes | code=%d\n",
                resp.length(), code);

  // ---- HTTP error handling ----
  if (code != 200) {
    Serial.printf("❌ [Gemini] HTTP %d — không phải 200\n", code);
    Serial.printf("   Body preview (first 500):\n%.500s\n", resp.c_str());

    // Thử parse error JSON
    DynamicJsonDocument errDoc(2048);
    DeserializationError jerr = deserializeJson(errDoc, resp);
    if (!jerr) {
      const char* msg = errDoc["error"]["message"] | "(no message)";
      int         ec  = errDoc["error"]["code"]    | 0;
      const char* st  = errDoc["error"]["status"]  | "(no status)";
      Serial.println("   ──── Parsed error ────");
      Serial.printf ("   Code    : %d\n", ec);
      Serial.printf ("   Status  : %s\n", st);
      Serial.printf ("   Message : %s\n", msg);
    } else {
      Serial.printf("   ⚠️ Không parse được error JSON: %s\n",
                    jerr.c_str());
    }

    logRAM("gemini fail");
    return false;
  }

  // ---- 200: parse response ----
  Serial.println("🔍 [Gemini] Parse JSON response...");

  size_t docSize = resp.length() * 2 + 2048;
  Serial.printf("   Doc size estimate: %u bytes\n", (unsigned)docSize);

  DynamicJsonDocument doc(docSize);
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    Serial.printf("❌ [Gemini] JSON parse fail: %s\n", err.c_str());
    Serial.printf("   Body preview (first 500):\n%.500s\n", resp.c_str());
    logRAM("gemini json fail");
    return false;
  }
  Serial.println("   ✅ JSON parse OK");

  // ---- Kiểm tra cấu trúc ----
  if (!doc.containsKey("candidates")) {
    Serial.println("❌ [Gemini] Không có 'candidates'");
    // Có thể là promptFeedback (bị block)
    if (doc.containsKey("promptFeedback")) {
      const char* blockReason =
          doc["promptFeedback"]["blockReason"] | "(none)";
      Serial.printf("   ⚠️ Block reason: %s\n", blockReason);
    }
    Serial.printf("   Body preview:\n%.500s\n", resp.c_str());
    logRAM("gemini no candidates");
    return false;
  }

  JsonArray candidates = doc["candidates"].as<JsonArray>();
  Serial.printf("   Candidates: %d\n", candidates.size());

  if (candidates.size() == 0) {
    Serial.println("❌ [Gemini] candidates array rỗng");
    logRAM("gemini empty candidates");
    return false;
  }

  JsonObject cand0 = candidates[0];

  // finishReason
  const char* finishReason = cand0["finishReason"] | "(none)";
  Serial.printf("   finishReason: %s\n", finishReason);

  // parts[0].text
  if (!cand0.containsKey("content") ||
      !cand0["content"].containsKey("parts")) {
    Serial.println("❌ [Gemini] Không có content.parts");
    logRAM("gemini no parts");
    return false;
  }

  JsonArray parts = cand0["content"]["parts"].as<JsonArray>();
  Serial.printf("   Parts count: %d\n", parts.size());

  if (parts.size() == 0) {
    Serial.println("❌ [Gemini] parts array rỗng");
    logRAM("gemini empty parts");
    return false;
  }

  const char* text = parts[0]["text"] | "";
  if (text[0] == '\0') {
    Serial.println("⚠️ [Gemini] text rỗng");
    Serial.printf("   finishReason=%s (nếu SAFETY thì bị block)\n",
                  finishReason);
    logRAM("gemini empty text");
    return false;
  }

  outReply = String(text);
  outReply.trim();

  unsigned long tEnd = millis();
  Serial.println("╔══════════════════════════════════════════");
  Serial.printf ("║ ✅ GEMINI OK\n");
  Serial.printf ("║ Reply len  : %d chars\n", outReply.length());
  Serial.printf ("║ Reply      : %.150s%s\n",
                 outReply.c_str(),
                 outReply.length() > 150 ? "..." : "");
  Serial.printf ("║ Total time : %lums\n", tEnd - tStart);
  Serial.printf ("║ Free heap  : %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("╚══════════════════════════════════════════");

  logRAM("sau gemini OK");
  return true;
}

// ============================================================
//  geminiPrintInfo — in cấu hình
// ============================================================
void geminiPrintInfo() {
  Serial.println("╔══════════ GEMINI INFO ══════════");
  Serial.printf ("║ Model      : %s\n", GEMINI_MODEL);
  Serial.printf ("║ Max tokens : %d\n", GEMINI_MAX_TOKENS);
  Serial.printf ("║ Timeout    : %d ms\n", GEMINI_TIMEOUT_MS);
  Serial.printf ("║ Key        : %.8s...\n", GEMINI_API_KEY);
  Serial.printf ("║ System     : %.60s...\n", GEMINI_SYSTEM_PROMPT);
  Serial.println("╚═════════════════════════════════");
}

// ============================================================
//  handlePendingAI — chạy khi có /ai, KHÔNG đóng WS
// ============================================================
void handlePendingAI() {
  if (!g_aiPending) return;

  unsigned long tStart = millis();

  // Copy ra local rồi clear flag
  String prompt   = g_aiPrompt;
  String threadId = g_aiThreadId;
  String replyMid = g_aiReplyToMsgId;

  g_aiPending      = false;
  g_aiPrompt       = "";
  g_aiThreadId     = "";
  g_aiReplyToMsgId = "";

  Serial.println("\n╔══════════════════════════════════════════");
  Serial.println("║ [/ai PROCESS]");
  Serial.println("╠══════════════════════════════════════════");
  Serial.printf ("║ Prompt   : %.80s%s\n",
                 prompt.c_str(), prompt.length() > 80 ? "..." : "");
  Serial.printf ("║ Thread   : %s\n", threadId.c_str());
  Serial.printf ("║ ReplyMid : %s\n", replyMid.c_str());
  Serial.printf ("║ WS state : %s\n",
                 wsClient.connected() ? "connected" : "CLOSED");
  Serial.printf ("║ MQTT     : %s\n",
                 mqttConnected ? "connected" : "disconnected");
  Serial.printf ("║ Free heap: %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("╚══════════════════════════════════════════");

  // ==========================================================
  //  KEEP-ALIVE trước khi gọi Gemini (loop bị block 3-5s)
  // ==========================================================
  if (wsClient.connected()) {
    String mqttPing = String((char)0xC0) + String((char)0x00);
    bool ok1 = wsSendFrame(0x2, (const uint8_t*)mqttPing.c_str(),
                            mqttPing.length());
    lastPing = millis();

    bool ok2 = wsSendFrame(0x9, nullptr, 0);

    Serial.printf("[WS/MQTT] -> pre-Gemini PING (mqtt=%s, ws=%s)\n",
                  ok1 ? "ok" : "FAIL", ok2 ? "ok" : "FAIL");
  } else {
    Serial.println("⚠️ [AI] WS đã đóng trước khi gọi Gemini");
  }

  logRAM("trc gemini (in handlePendingAI)");

  // ==========================================================
  //  GỌI GEMINI
  // ==========================================================
  String reply;
  bool ok = false;

  if (g_aiService == "groq") {
    Serial.println("⚡ Service: GROQ");
    ok = groqAsk(prompt, reply);
  } else {
    Serial.println("🤖 Service: GEMINI");
    ok = geminiAsk(prompt, reply);
  }

  // ==========================================================
  //  SAU GEMINI — kiểm tra WS
  // ==========================================================
  bool wsAlive = wsClient.connected();
  Serial.printf("🔍 [AI] Sau Gemini: WS=%s | ok=%d | replyLen=%d\n",
                wsAlive ? "alive" : "DEAD",
                (int)ok, reply.length());

  if (!wsAlive) {
    Serial.println("⚠️ [AI] WS đã đóng trong lúc Gemini chạy");
    mqttConnected = false;
    g_syncToken = "";   // buộc create_queue lại
  }

  // ==========================================================
  //  GỬI REPLY
  // ==========================================================
  if (ok && reply.length() > 0) {
    Serial.println("📤 [AI] Gửi reply vào FB...");
    unsigned long tSend = millis();
    bool sendOk = sendGroupMessage(threadId, reply);
    Serial.printf("📤 [AI] sendGroupMessage %s in %lums\n",
                  sendOk ? "OK" : "FAIL", millis() - tSend);
  } else {
    Serial.println("⚠️ [AI] Gemini fail → gửi câu báo lỗi");
    bool sendOk = sendGroupMessage(threadId, "❌ Bot đang bận, thử lại sau nhé!");
    Serial.printf("📤 [AI] fallback send %s\n", sendOk ? "OK" : "FAIL");
  }

  Serial.printf("⏱️  [AI] Tổng thời gian xử lý: %lums\n",
                millis() - tStart);
  Serial.printf("💾 [AI] Free heap sau cùng: %u\n",
                (unsigned)ESP.getFreeHeap());

  logRAM("sau AI process");
}