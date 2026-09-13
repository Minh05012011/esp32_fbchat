#include "fb_send.h"
#include "app_state.h"
#include "logger.h"
#include "utils.h"
#include "config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

bool sendGroupMessage(const String& threadId, const String& body) {
  Serial.println("📤 sendGroupMessage...");
  logRAM("trc sendMsg");
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
    form += k; form += "="; form += urlEncode(v);
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
  add("is_unread", "false"); add("is_cleared", "false");
  add("is_forward", "false"); add("is_filtered_content", "false");
  add("is_filtered_content_bh", "false"); add("is_filtered_content_account", "false");
  add("is_filtered_content_quasar", "false"); add("is_filtered_content_invalid_app", "false");
  add("is_spoof_warning", "false");

  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  https.addHeader("User-Agent", WEB_UA);
  https.addHeader("Cookie", g_cookies);
  https.addHeader("Origin", "https://www.facebook.com");
  https.addHeader("Referer", "https://www.facebook.com/");
  int code = https.POST((uint8_t*)form.c_str(), form.length());
  if (code <= 0) { https.end(); logRAM("sendMsg http fail"); return false; }
  String resp = https.getString();
  https.end();
  int p = resp.indexOf("\"message_id\":\"");
  if (p > 0) {
    p += 14;
    int e = resp.indexOf("\"", p);
    Serial.printf("   ✅ message_id=%s\n", resp.substring(p, e).c_str());
    g_msgSent++;
    logRAM("sau sendMsg OK");
    return true;
  }
  Serial.printf("   ❌ %.200s\n", resp.c_str());
  logRAM("sendMsg fail");
  return false;
}