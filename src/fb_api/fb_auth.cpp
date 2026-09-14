#include "src/fb_api/fb_auth.h"
#include "src/core/app_state.h"
#include "src/core/logger.h"
#include "src/core/utils.h"
#include "src/fb_api/cookie.h"
#include "src/core/config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>

void syncTimeFromHTTP() {
  Serial.println("⏳ Sync time tu HTTP Date...");
  logRAM("trc time-sync");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setConnectTimeout(10000);
  https.setTimeout(10000);
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!https.begin(client, "https://www.facebook.com/")) return;
  https.addHeader("User-Agent", WEB_UA);
  const char* hdrs[] = {"Date"};
  https.collectHeaders(hdrs, 1);
  int code = https.GET();
  if (code <= 0) { https.end(); return; }
  String dateStr = https.header("Date");
  https.end();
  Serial.printf("   Date: '%s'\n", dateStr.c_str());
  time_t t = parseHttpDate(dateStr);
  if (t > 1700000000 && t < 2000000000) {
    struct timeval tv = { t, 0 };
    settimeofday(&tv, NULL);
    Serial.printf("✅ Time: %ld\n", (long)t);
  }
  logRAM("sau time-sync");
}

String verifyCookie() {
  Serial.println("🔍 verifyCookie()...");
  logRAM("trc verifyCookie");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setConnectTimeout(15000);
  https.setTimeout(15000);
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!https.begin(client, "https://www.facebook.com/api/graphqlbatch/"))
    return "";

  String form;
  form += "fb_dtsg=" + urlEncode(g_fbDtsg);
  form += "&jazoest=" + g_jazoest;
  form += "&__a=1";
  form += "&__user=" + g_uid;
  form += "&__req=1";
  form += "&__rev=" + g_rev;
  form += "&av=" + g_uid;
  form += "&queries=";
  form += urlEncode("{\"o0\":{\"doc_id\":\"3336396659757871\","
                    "\"query_params\":{\"limit\":1,\"before\":null,"
                    "\"tags\":[\"INBOX\"],\"includeDeliveryReceipts\":false,"
                    "\"includeSeqID\":true}}}");

  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  https.addHeader("User-Agent", WEB_UA);
  https.addHeader("Cookie", g_cookies);
  https.addHeader("Origin", "https://www.facebook.com");
  https.addHeader("Referer", "https://www.facebook.com/");

  int code = https.POST((uint8_t*)form.c_str(), form.length());
  if (code <= 0) { https.end(); Serial.println("   ❌ HTTP fail"); logRAM("verifyCookie fail"); return ""; }
  String body = https.getString();
  https.end();
  Serial.printf("   [HTTP] code=%d | %d bytes\n", code, body.length());

  int p = body.indexOf("\"sync_sequence_id\":\"");
  if (p < 0) {
    if (body.indexOf("1357004") >= 0) Serial.println("   ⚠️ fb_dtsg sai/hết hạn");
    else Serial.printf("   ❌ Preview: %.200s\n", body.c_str());
    logRAM("verifyCookie err");
    return "";
  }
  p += 20;
  int e = body.indexOf("\"", p);
  String seq = body.substring(p, e);
  Serial.printf("   ✅ sync_seq=%s\n", seq.c_str());
  logRAM("sau verifyCookie");
  return seq;
}

bool doLogin() {
  Serial.println("🔐 Login b-graph...");
  logRAM("trc login");
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient https;
  https.setConnectTimeout(15000);
  https.setTimeout(30000);
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!https.begin(client, "https://b-graph.facebook.com/auth/login"))
    return false;

  String did = genClientId();
  String form;
  auto add = [&](const char* k, const String& v) {
    if (form.length()) form += "&";
    form += k; form += "="; form += urlEncode(v);
  };
  add("adid", did); add("format", "json"); add("device_id", did);
  add("email", FB_USERNAME); add("password", FB_PASSWORD);
  add("generate_analytics_claim", "1"); add("community_id", "");
  add("cpl", "true"); add("try_num", "1");
  add("family_device_id", did); add("secure_family_device_id", did);
  add("credentials_type", "password");
  add("fb4a_shared_phone_cpl_experiment", "fb4a_shared_phone_nonce_cpl_at_risk_v3");
  add("fb4a_shared_phone_cpl_group", "enable_v3_at_risk");
  add("enroll_misauth", "false");
  add("generate_session_cookies", "1");
  add("error_detail_type", "button_with_disabled");
  add("source", "login"); add("machine_id", did);
  add("meta_inf_fbmeta", ""); add("advertiser_id", did);
  add("encrypted_msisdn", ""); add("currently_logged_in_userid", "0");
  add("locale", "vi_VN"); add("client_country_code", "VN");
  add("fb_api_req_friendly_name", "authenticate");
  add("fb_api_caller_class", "Fb4aAuthHandler");
  add("api_key", "882a8490361da98702bf97a021ddc14d");
  add("access_token", "350685531728|62f8ce9f74b12f84c123cc23437a4a32");
  add("jazoest", "22421");

  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  https.addHeader("User-Agent", FB_LOGIN_UA);

  int code = https.POST((uint8_t*)form.c_str(), form.length());
  if (code <= 0) { https.end(); logRAM("login http fail"); return false; }
  String body = https.getString();
  https.end();
  Serial.printf("   [HTTP] code=%d | %d bytes\n", code, body.length());

  if (body.indexOf("\"session_cookies\"") < 0) {
    Serial.printf("   ❌ %.200s\n", body.c_str());
    logRAM("login fail");
    return false;
  }
  String newUid = jsonGetStr(body, "uid");
  String newCookies = extractCookiesFromLogin(body);
  if (newUid.length() == 0 || newCookies.length() == 0) { logRAM("login parse fail"); return false; }

  g_cookies = newCookies;
  g_uid = extractCookieValue(g_cookies, "c_user");
  if (g_uid.length() == 0) g_uid = newUid;

  Serial.printf("   ✅ uid=%s\n", g_uid.c_str());
  Serial.println("   🍪 Cookie mới (dán lại MANUAL_COOKIE):");
  Serial.println("   " + g_cookies);
  logRAM("sau login OK");
  return true;
}