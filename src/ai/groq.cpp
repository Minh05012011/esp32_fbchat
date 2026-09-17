#include "src/ai/groq.h"
#include "src/core/app_state.h"
#include "src/core/logger.h"
#include "src/core/utils.h"
#include "src/core/config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// ============================================================
//  JSON escape (copy từ gemini.cpp — không share để giữ độc lập)
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
//  groqAsk — gọi Groq API
// ============================================================
bool groqAsk(const String& prompt, String& outReply) {
  outReply = "";
  unsigned long tStart = millis();

  Serial.println("╔══════════════════════════════════════════");
  Serial.println("║ ⚡ GROQ ASK");
  Serial.println("╠══════════════════════════════════════════");
  Serial.printf ("║ Model      : %s\n", GROQ_MODEL);
  Serial.printf ("║ MaxTokens  : %d\n", GROQ_MAX_TOKENS);
  Serial.printf ("║ Prompt len : %d chars\n", prompt.length());
  Serial.printf ("║ Prompt     : %.120s%s\n",
                 prompt.c_str(),
                 prompt.length() > 120 ? "..." : "");
  Serial.printf ("║ Free heap  : %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.printf ("║ MaxBlk     : %u bytes\n", (unsigned)ESP.getMaxAllocHeap());
  Serial.println("╚══════════════════════════════════════════");

  logRAM("trc groq");

  // ---- Validate ----
  if (prompt.length() == 0) {
    Serial.println("❌ [Groq] prompt rỗng");
    return false;
  }
  if (String(GROQ_API_KEY).length() < 10) {
    Serial.println("❌ [Groq] Chưa cấu hình GROQ_API_KEY");
    return false;
  }

  // ---- Build JSON body (OpenAI-compatible) ----
  // {
  //   "model":"llama-3.3-70b-versatile",
  //   "messages":[
  //     {"role":"system","content":"..."},
  //     {"role":"user","content":"..."}
  //   ],
  //   "max_tokens":512,
  //   "temperature":0.8
  // }
  String body;
  body.reserve(512 + prompt.length());
  body += "{\"model\":\"";
  body += GROQ_MODEL;
  body += "\",\"messages\":[";

  // system
  body += "{\"role\":\"system\",\"content\":\"";
  body += jsonEscape(String(GROQ_SYSTEM_PROMPT));
  body += "\"},";

  // user
  body += "{\"role\":\"user\",\"content\":\"";
  body += jsonEscape(prompt);
  body += "\"}],";

  body += "\"max_tokens\":";
  body += String(GROQ_MAX_TOKENS);
  body += ",\"temperature\":0.8}";

  Serial.printf("📦 [Groq] Body size: %d bytes\n", body.length());

  // ---- URL ----
  String url = "https://api.groq.com/openai/v1/chat/completions";
  Serial.printf("🌐 [Groq] URL: %s\n", url.c_str());

  // ---- HTTP ----
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(GROQ_TIMEOUT_MS / 1000);

  HTTPClient https;
  https.setConnectTimeout(15000);
  https.setTimeout(GROQ_TIMEOUT_MS);
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  Serial.println("🔌 [Groq] https.begin()...");
  if (!https.begin(client, url)) {
    Serial.println("❌ [Groq] https.begin FAIL");
    logRAM("groq begin fail");
    return false;
  }
  Serial.println("✅ [Groq] begin OK");

  https.addHeader("Content-Type", "application/json");
  https.addHeader("Accept",       "application/json");
  https.addHeader("Authorization", String("Bearer ") + GROQ_API_KEY);

  Serial.printf("📤 [Groq] POST %d bytes...\n", body.length());
  unsigned long tPost = millis();
  int code = https.POST((uint8_t*)body.c_str(), body.length());
  unsigned long tPostDone = millis();
  Serial.printf("⏱️  [Groq] POST done in %lums | code=%d\n",
                tPostDone - tPost, code);

  if (code <= 0) {
    Serial.printf("❌ [Groq] HTTP code=%d (<=0)\n", code);
    Serial.printf("   errorToString: %s\n", https.errorToString(code).c_str());
    https.end();
    logRAM("groq http fail");
    return false;
  }

  String resp = https.getString();
  https.end();

  Serial.printf("📥 [Groq] Response: %d bytes | code=%d\n",
                resp.length(), code);

  // ---- HTTP error ----
  if (code != 200) {
    Serial.printf("❌ [Groq] HTTP %d — không phải 200\n", code);
    Serial.printf("   Body preview (first 500):\n%.500s\n", resp.c_str());

    DynamicJsonDocument errDoc(2048);
    DeserializationError jerr = deserializeJson(errDoc, resp);
    if (!jerr) {
      const char* msg  = errDoc["error"]["message"] | "(no message)";
      const char* type = errDoc["error"]["type"]    | "(no type)";
      const char* cd   = errDoc["error"]["code"]    | "(no code)";
      Serial.println("   ──── Parsed error ────");
      Serial.printf ("   Type    : %s\n", type);
      Serial.printf ("   Code    : %s\n", cd);
      Serial.printf ("   Message : %s\n", msg);
    } else {
      Serial.printf("   ⚠️ Không parse được error JSON: %s\n", jerr.c_str());
    }

    logRAM("groq fail");
    return false;
  }

  // ---- Parse 200 ----
  Serial.println("🔍 [Groq] Parse JSON response...");

  size_t docSize = resp.length() * 2 + 2048;
  Serial.printf("   Doc size estimate: %u bytes\n", (unsigned)docSize);

  DynamicJsonDocument doc(docSize);
  DeserializationError err = deserializeJson(doc, resp);
  if (err) {
    Serial.printf("❌ [Groq] JSON parse fail: %s\n", err.c_str());
    Serial.printf("   Body preview:\n%.500s\n", resp.c_str());
    logRAM("groq json fail");
    return false;
  }
  Serial.println("   ✅ JSON parse OK");

  if (!doc.containsKey("choices")) {
    Serial.println("❌ [Groq] Không có 'choices'");
    Serial.printf("   Body preview:\n%.500s\n", resp.c_str());
    logRAM("groq no choices");
    return false;
  }

  JsonArray choices = doc["choices"].as<JsonArray>();
  Serial.printf("   Choices: %d\n", choices.size());

  if (choices.size() == 0) {
    Serial.println("❌ [Groq] choices rỗng");
    logRAM("groq empty choices");
    return false;
  }

  JsonObject ch0 = choices[0];
  const char* finishReason = ch0["finish_reason"] | "(none)";
  Serial.printf("   finish_reason: %s\n", finishReason);

  const char* text = ch0["message"]["content"] | "";
  if (text[0] == '\0') {
    Serial.println("⚠️ [Groq] text rỗng");
    logRAM("groq empty text");
    return false;
  }

  outReply = String(text);
  outReply.trim();

  unsigned long tEnd = millis();
  Serial.println("╔══════════════════════════════════════════");
  Serial.printf ("║ ✅ GROQ OK\n");
  Serial.printf ("║ Reply len  : %d chars\n", outReply.length());
  Serial.printf ("║ Reply      : %.150s%s\n",
                 outReply.c_str(),
                 outReply.length() > 150 ? "..." : "");
  Serial.printf ("║ Total time : %lums\n", tEnd - tStart);
  Serial.printf ("║ Free heap  : %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("╚══════════════════════════════════════════");

  logRAM("sau groq OK");
  return true;
}

void groqPrintInfo() {
  Serial.println("╔══════════ GROQ INFO ══════════");
  Serial.printf ("║ Model      : %s\n", GROQ_MODEL);
  Serial.printf ("║ Max tokens : %d\n", GROQ_MAX_TOKENS);
  Serial.printf ("║ Timeout    : %d ms\n", GROQ_TIMEOUT_MS);
  Serial.printf ("║ Key        : %.12s...\n", GROQ_API_KEY);
  Serial.printf ("║ System     : %.60s...\n", GROQ_SYSTEM_PROMPT);
  Serial.println("╚═════════════════════════════════");
}

// ============================================================
//  Parse spec QR do AI trả về: "Title1:Payload1;Title2:Payload2;..."
//  - Strict hơn qr_serial: bỏ nút thiếu ':' hoặc title rỗng
//  - Title quá dài → truncate MAX_QR_TITLE_LEN
//  - Payload LUÔN ghi đè = title (để ổn định khi MQTT so khớp sau này)
// ============================================================
static int parseQrSpecFromAi(const String& spec,
                             QuickReplyBtn* out, int maxOut) {
  int n = 0;
  int start = 0;
  int len = spec.length();

  while (start <= len && n < maxOut) {
    int semi = spec.indexOf(';', start);
    String item = (semi < 0) ? spec.substring(start)
                             : spec.substring(start, semi);
    item.trim();

    if (item.length() > 0) {
      int colon = item.indexOf(':');
      // colon > 0: title không rỗng. colon == 0 hoặc <0 → bỏ nút.
      if (colon > 0) {
        QuickReplyBtn btn;
        btn.title   = item.substring(0, colon);
        btn.payload = item.substring(colon + 1);
        btn.title.trim();
        btn.payload.trim();

        if (btn.title.length() > 0) {
          if (btn.title.length() > MAX_QR_TITLE_LEN) {
            btn.title = btn.title.substring(0, MAX_QR_TITLE_LEN);
          }
          // Payload = title (theo yêu cầu, đảm bảo ổn định)
          btn.payload = btn.title;
          out[n++] = btn;
        }
      }
    }

    if (semi < 0) break;
    start = semi + 1;
  }
  return n;
}

int groqExtractQr(const String& reply, String& outText,
                  QuickReplyBtn* outBtns, int maxOut) {
  outText = reply;   // mặc định: giữ nguyên

  int qrOpen = reply.indexOf("[QR]");
  if (qrOpen < 0) return -1;

  int qrClose = reply.indexOf("[/QR]", qrOpen + 4);
  if (qrClose < 0) return 0;   // có mở, không đóng → fail

  String spec = reply.substring(qrOpen + 4, qrClose);
  spec.trim();

  int n = parseQrSpecFromAi(spec, outBtns, maxOut);
  if (n <= 0) return 0;

  // Cắt cả block QR (kể cả 2 marker), gộp text trước + text sau
  String text = reply.substring(0, qrOpen) +
                reply.substring(qrClose + 5);
  text.trim();
  outText = text;

  return n;
}