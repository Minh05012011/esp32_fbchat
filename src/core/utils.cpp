#include "src/core/utils.h"
#include "src/core/config.h"

// ---- MSG POOL ----
static const char* MSG_POOL[] = {
  "Xin chao tu ESP32 👋",
  "🤖 ESP32 da online!",
  "Nghe ro roi nhe!",
  "Beep boop 🤖",
  "ESP32 here 🚀",
  "OK da nhan!",
  "Chuc mot ngay tot lanh ✨"
};
static const int MSG_POOL_SIZE = sizeof(MSG_POOL) / sizeof(MSG_POOL[0]);

String urlEncode(const String& s) {
  String out; const char* hx = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c=='-'||c=='_'||c=='.'||c=='~') out += c;
    else { out += '%'; out += hx[(c>>4)&0xF]; out += hx[c&0xF]; }
  }
  return out;
}

String jsonGetStr(const String& src, const String& key) {
  String pat = "\"" + key + "\":\"";
  int p = src.indexOf(pat);
  if (p < 0) return "";
  p += pat.length();
  int e = src.indexOf("\"", p);
  if (e < 0) return "";
  return src.substring(p, e);
}

time_t parseHttpDate(const String& s) {
  if (s.length() < 20) return 0;
  const char* months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                          "Jul","Aug","Sep","Oct","Nov","Dec"};
  int comma = s.indexOf(',');
  String rest = (comma >= 0) ? s.substring(comma + 1) : s;
  rest.trim();
  int sp1 = rest.indexOf(' ');
  int day = rest.substring(0, sp1).toInt();
  rest = rest.substring(sp1 + 1); rest.trim();
  int sp2 = rest.indexOf(' ');
  String monStr = rest.substring(0, sp2);
  rest = rest.substring(sp2 + 1); rest.trim();
  int sp3 = rest.indexOf(' ');
  int year = rest.substring(0, sp3).toInt();
  rest = rest.substring(sp3 + 1); rest.trim();
  int c1 = rest.indexOf(':');
  int c2 = rest.indexOf(':', c1 + 1);
  int hh = rest.substring(0, c1).toInt();
  int mm = rest.substring(c1 + 1, c2).toInt();
  int ss = rest.substring(c2 + 1, c2 + 3).toInt();
  int month = -1;
  for (int i = 0; i < 12; i++)
    if (monStr == months[i]) { month = i; break; }
  if (month < 0) return 0;
  struct tm t = {};
  t.tm_mday = day; t.tm_mon = month; t.tm_year = year - 1900;
  t.tm_hour = hh; t.tm_min = mm; t.tm_sec = ss;
  return mktime(&t);
}

String genThreadingId() {
  uint64_t ts = (uint64_t)time(nullptr) * 1000ULL;
  uint64_t id = (ts << 22) | (esp_random() & 0x3FFFFF);
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu", (unsigned long long)id);
  return String(buf);
}

String getRandomMessage() {
  return String(MSG_POOL[random(MSG_POOL_SIZE)]);
}

String genClientId() {
  char buf[64];
  snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%08x%04x",
           (unsigned)esp_random(), (unsigned)(esp_random() & 0xFFFF),
           (unsigned)(esp_random() & 0xFFFF),
           (unsigned)(esp_random() & 0xFFFF),
           (unsigned)esp_random(), (unsigned)(esp_random() & 0xFFFF));
  return String(buf);
}

String genSessionId() {
  char buf[32];
  snprintf(buf, sizeof(buf), "%llu",
           (unsigned long long)(((uint64_t)esp_random() << 32) | esp_random()));
  return String(buf);
}