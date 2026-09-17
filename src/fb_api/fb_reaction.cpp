#include "src/fb_api/fb_reaction.h"
#include "src/core/app_state.h"
#include "src/core/logger.h"
#include "src/core/utils.h"
#include "src/core/config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ====== Counter cho __req (giống attr.ib(0).counter trong Python formAll) ======
static uint32_t g_reactReqCounter = 0;

// Đổi số nguyên sang base36 giống str_base(number, 36) của Python.
//   0->"0", 9->"9", 10->"a", 35->"z", 36->"10"
static String toBase36(uint32_t n) {
  const char* digits = "0123456789abcdefghijklmnopqrstuvwxyz";
  if (n == 0) return String("0");
  String out;
  while (n > 0) {
    char c = digits[n % 36];
    out = String(c) + out;
    n /= 36;
  }
  return out;
}

bool fbReactMessage(const String& messageId,
                    const String& emoji,
                    bool removeReaction) {

  Serial.printf("😀 fbReactMessage | mid=%s | emoji=%s | %s\n",
                messageId.c_str(), emoji.c_str(),
                removeReaction ? "REMOVE" : "ADD");
  logRAM("trc react");

  if (messageId.length() == 0) {
    Serial.println("   ❌ messageId rỗng");
    return false;
  }
  if (emoji.length() == 0) {
    Serial.println("   ❌ emoji rỗng");
    return false;
  }

  // ---- __req base36 (giống formAll) ----
  g_reactReqCounter++;
  String reqStr = toBase36(g_reactReqCounter);

  // ---- Build "variables" JSON (y hệt _build_request bên Python) ----
  // {"data":{"action":"ADD_REACTION","client_mutation_id":"1",
  //          "actor_id":"<uid>","message_id":"<mid>","reaction":"<emoji>"}}
  String variables;
  variables.reserve(128 + emoji.length() + messageId.length());
  variables += "{\"data\":{\"action\":\"";
  variables += (removeReaction ? "REMOVE_REACTION" : "ADD_REACTION");
  variables += "\",\"client_mutation_id\":\"1\",\"actor_id\":\"";
  variables += g_uid;
  variables += "\",\"message_id\":\"";
  variables += messageId;
  variables += "\",\"reaction\":\"";
  variables += emoji;      // giữ raw UTF-8 - urlEncode sẽ mã hoá sau
  variables += "\"}}";

  // ---- Build form body (tương đương formAll(dataFB, docID=1491398900900362)) ----
  // Thứ tự field giữ giống Python để dễ debug khi so sánh log:
  // fb_dtsg, jazoest, __a, __user, __req, __rev, av,
  // fb_api_caller_class, fb_api_req_friendly_name, server_timestamps,
  // doc_id, variables, dpr
  String form;
  form.reserve(form.length() + 512 + variables.length());
  auto add = [&](const String& k, const String& v) {
    if (form.length()) form += "&";
    form += k; form += "="; form += urlEncode(v);
  };

  add("fb_dtsg",                 g_fbDtsg);
  add("jazoest",                 g_jazoest);
  add("__a",                     "1");
  add("__user",                  g_uid);
  add("__req",                   reqStr);
  add("__rev",                   g_rev);
  add("av",                      g_uid);
  add("fb_api_caller_class",     "RelayModern");
  add("fb_api_req_friendly_name", "None");  // <- match Python: str(None) = "None"
  add("server_timestamps",       "true");
  add("doc_id",                  "1491398900900362");  // doc_id chuẩn của reaction
  add("variables",               variables);
  add("dpr",                     "1");

  // ---- HTTP POST ----
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setConnectTimeout(15000);
  https.setTimeout(30000);
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  if (!https.begin(client, "https://www.facebook.com/webgraphql/mutation/")) {
    Serial.println("   ❌ https.begin fail");
    logRAM("react begin fail");
    return false;
  }

  // Các header mà HTTPClient tự sinh (Host, Connection, Content-Length) không cần add.
  // Chỉ add các header đặc trưng FB cần:
  https.addHeader("Content-Type",   "application/x-www-form-urlencoded");
  https.addHeader("User-Agent",     WEB_UA);
  https.addHeader("Cookie",         g_cookies);
  https.addHeader("Accept",         "*/*");
  https.addHeader("Origin",         "https://www.facebook.com");
  https.addHeader("Referer",        "https://www.facebook.com");
  https.addHeader("Accept-Language","vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7");
  https.addHeader("Sec-Fetch-Site", "same-origin");
  https.addHeader("Sec-Fetch-Mode", "cors");
  https.addHeader("Sec-Fetch-Dest", "empty");

  int code = https.POST((uint8_t*)form.c_str(), form.length());
  if (code <= 0) {
    https.end();
    Serial.printf("   ❌ HTTP fail code=%d\n", code);
    logRAM("react http fail");
    return false;
  }
  String resp = https.getString();
  https.end();

  Serial.printf("   [HTTP] code=%d | %d bytes\n", code, resp.length());

  // Python chỉ làm raise_for_status -> 200 là OK.
  // Mình vẫn log preview để debug, và log riêng nếu có "error" trong body
  // (không đổi kết quả trả về để khớp Python).
  if (code == 200) {
    bool hasErrorKey = (resp.indexOf("\"error\"")  >= 0) ||
                       (resp.indexOf("\"errors\"") >= 0);
    if (hasErrorKey) {
      Serial.printf("   ⚠️ Body có 'error'/'errors' (code vẫn 200): %.200s\n",
                    resp.c_str());
      logRAM("react body has error");
    }
    Serial.println("   ✅ Reaction OK (HTTP 200)");
    logRAM("sau react OK");
    return true;
  }

  Serial.printf("   ❌ code=%d | %.200s\n", code, resp.c_str());
  logRAM("react fail");
  return false;
}