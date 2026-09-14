#include "src/fb_api/cookie.h"
#include "src/core/app_state.h"

String extractCookieValue(const String& cookie, const String& key) {
  if (cookie.length() == 0 || key.length() == 0) return "";

  int start = -1;

  int p = cookie.indexOf("; " + key + "=");
  if (p >= 0) {
    start = p + 2 + key.length() + 1;
  } else {
    p = cookie.indexOf(";" + key + "=");
    if (p >= 0) {
      start = p + 1 + key.length() + 1;
    } else {
      if (cookie.startsWith(key + "=")) {
        start = key.length() + 1;
      }
    }
  }

  if (start < 0 || start >= (int)cookie.length()) return "";

  int end = cookie.indexOf(';', start);
  if (end < 0) end = cookie.length();

  String value = cookie.substring(start, end);
  value.trim();
  return value;
}

bool parseCookieAndFill(const String& rawCookie) {
  Serial.println("🍪 Parse cookie...");

  g_cookies = rawCookie;
  g_cookies.trim();

  if (g_cookies.length() == 0) {
    Serial.println("   ❌ Cookie rỗng");
    return false;
  }

  String uid = extractCookieValue(g_cookies, "c_user");
  if (uid.length() == 0) {
    Serial.println("   ❌ Không tìm thấy c_user trong cookie");
    Serial.printf("   Cookie preview: %.120s...\n", g_cookies.c_str());
    return false;
  }

  g_uid = uid;

  Serial.printf("   ✅ uid     = %s\n", g_uid.c_str());
  Serial.printf("   ✅ datr    = %s\n",
                extractCookieValue(g_cookies, "datr").c_str());
  Serial.printf("   ✅ sb      = %s\n",
                extractCookieValue(g_cookies, "sb").c_str());
  Serial.printf("   ✅ xs      = %.40s...\n",
                extractCookieValue(g_cookies, "xs").c_str());
  Serial.printf("   ✅ length  = %d bytes\n", g_cookies.length());
  return true;
}

String extractCookiesFromLogin(const String& body) {
  int p = body.indexOf("\"session_cookies\":[");
  if (p < 0) return "";
  p += 18;
  int depth = 1, e = p;
  while (e < (int)body.length() && depth > 0) {
    if (body[e]=='[') depth++;
    else if (body[e]==']') depth--;
    if (depth==0) break;
    e++;
  }
  String arr = body.substring(p, e);
  String cookie;
  int idx = 0;
  while (true) {
    int n = arr.indexOf("\"name\":\"", idx);
    if (n < 0) break;
    n += 8;
    int ne = arr.indexOf("\"", n);
    String name = arr.substring(n, ne);
    int v = arr.indexOf("\"value\":\"", ne);
    if (v < 0) break;
    v += 9;
    int ve = arr.indexOf("\"", v);
    String value = arr.substring(v, ve);
    cookie += name + "=" + value + "; ";
    idx = ve;
  }
  return cookie;
}