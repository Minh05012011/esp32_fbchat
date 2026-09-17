#include "src/fb_api/fb_quick_reply.h"
#include "src/core/app_state.h"
#include "src/core/logger.h"
#include "src/core/utils.h"
#include "src/core/config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ============================================================
//  Build JSON cho field "platform_xmd":
//  { "quick_replies": [ {"content_type":"text","title":"..","payload":".."} ] }
//  (tương đương __sendQuickReply.py bên Python — chỉ hỗ trợ loại "text")
// ============================================================
static String buildPlatformXmd(const QuickReplyBtn* buttons, int count) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  StaticJsonDocument<1024> doc;
#endif
  JsonArray arr = doc["quick_replies"].to<JsonArray>();
  for (int i = 0; i < count; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["content_type"] = "text";
    o["title"]         = buttons[i].title;
    o["payload"]       = buttons[i].payload;
  }
  String out;
  serializeJson(doc, out);
  return out;
}

bool sendQuickReplyMessage(const String& threadId,
                            const String& body,
                            const QuickReplyBtn* buttons,
                            int count) {
  if (count <= 0) {
    Serial.println("⚠️ sendQuickReplyMessage: không có nút nào");
    return false;
  }

  Serial.printf("📤 sendQuickReplyMessage... (%d nút)\n", count);
  logRAM("trc sendQR");

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setConnectTimeout(15000);
  https.setTimeout(30000);
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!https.begin(client, "https://www.facebook.com/messaging/send/"))
    return false;

  String form;
  auto add = [&](const String& k, const String& v) {
    if (form.length()) form += "&";
    form += k; form += "=" + urlEncode(v);
  };

  add("fb_dtsg", g_fbDtsg); add("jazoest", g_jazoest);
  add("__a", "1"); add("__user", g_uid); add("__req", "1");
  add("__rev", g_rev); add("av", g_uid);
  add("thread_fbid", threadId);
  add("action_type", "ma-type:user-generated-message");
  add("body", body);
  add("author", "fbid:" + g_uid);

  char nowBuf[24];
  uint64_t now_ms = (uint64_t)time(nullptr) * 1000ULL;
  snprintf(nowBuf, sizeof(nowBuf), "%llu", (unsigned long long)now_ms);
  add("timestamp", String(nowBuf));
  add("timestamp_absolute", "Today");
  add("source", "source:chat:web");
  add("source_tags[0]", "source:chat");

  String offlineId = genThreadingId();
  add("client_thread_id", "root:" + offlineId);
  add("offline_threading_id", offlineId);
  add("message_id", offlineId);

  char threadBuf[80];
  snprintf(threadBuf, sizeof(threadBuf), "<%llu:%u-%x@mail.projektitan.com>",
           (unsigned long long)now_ms, (unsigned)esp_random(),
           (unsigned)(esp_random() & 0x7FFFFFFF));
  add("threading_id", String(threadBuf));

  add("ephemeral_ttl_mode", "0");
  add("manual_retry_cnt", "0");
  add("ui_push_phase", "V3");

  // Cờ bắt buộc = false, giống sendGroupMessage
  add("is_unread", "false"); add("is_cleared", "false");
  add("is_forward", "false"); add("is_filtered_content", "false");
  add("is_filtered_content_bh", "false"); add("is_filtered_content_account", "false");
  add("is_filtered_content_quasar", "false"); add("is_filtered_content_invalid_app", "false");
  add("is_spoof_warning", "false");

  // ⭐ Điểm khác biệt duy nhất so với sendGroupMessage
  add("platform_xmd", buildPlatformXmd(buttons, count));

  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  https.addHeader("User-Agent", WEB_UA);
  https.addHeader("Cookie", g_cookies);
  https.addHeader("Origin", "https://www.facebook.com");
  https.addHeader("Referer", "https://www.facebook.com/");

  int code = https.POST((uint8_t*)form.c_str(), form.length());
  if (code <= 0) { https.end(); logRAM("sendQR http fail"); return false; }
  String resp = https.getString();
  https.end();

  int p = resp.indexOf("\"message_id\":\"");
  if (p > 0) {
    p += 15;
    int e = resp.indexOf("\"", p);
    Serial.printf("   ✅ message_id=%s\n", resp.substring(p, e).c_str());
    g_msgSent++;
    logRAM("sau sendQR OK");
    return true;
  }
  Serial.printf("   ❌ %.200s\n", resp.c_str());
  logRAM("sendQR fail");
  return false;
}