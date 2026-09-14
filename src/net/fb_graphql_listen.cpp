#include "src/net/fb_graphql_listen.h"
#include "src/core/app_state.h"
#include "src/core/logger.h"
#include "src/core/utils.h"
#include "src/core/config.h"

#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// ============================================================
//  __req counter (base36)
// ============================================================
static uint32_t g_glReqCounter = 0;

static String toBase36(uint32_t n) {
  const char* d = "0123456789abcdefghijklmnopqrstuvwxyz";
  if (n == 0) return String("0");
  String out;
  while (n > 0) { char c = d[n % 36]; out = String(c) + out; n /= 36; }
  return out;
}

// ============================================================
//  State
// ============================================================
static FbMessageCallback g_callback   = nullptr;
static long long         g_lastSyncMs = 0;
static int               g_limit      = 15;

static const int DEDUP_SIZE = 100;
static String    g_seenIds[DEDUP_SIZE];
static int       g_seenCount = 0;
static int       g_seenHead  = 0;

static unsigned long g_pollCount   = 0;
static unsigned long g_newMsgCount = 0;

static const char* DOC_ID_MESSAGE_THREADS = "3336396659757871";

// ============================================================
//  Public setters / getters
// ============================================================
void fbGraphQLSetCallback(FbMessageCallback cb) { g_callback = cb; }
void fbGraphQLSetLimit(int limit) {
  if (limit > 0 && limit <= 50) g_limit = limit;
}
unsigned long fbGraphQLGetPollCount()   { return g_pollCount; }
unsigned long fbGraphQLGetNewMsgCount() { return g_newMsgCount; }
long long     fbGraphQLGetBaseline()    { return g_lastSyncMs; }

void fbGraphQLResetBaseline() {
  g_lastSyncMs = (long long)time(nullptr) * 1000LL;
  Serial.printf("🔧 [GQL] Baseline = %lld\n", g_lastSyncMs);
}

// ============================================================
//  Dedup
// ============================================================
static bool isSeen(const String& id) {
  for (int i = 0; i < g_seenCount; i++)
    if (g_seenIds[i] == id) return true;
  return false;
}

static void markSeen(const String& id) {
  if (g_seenCount < DEDUP_SIZE) {
    g_seenIds[g_seenCount++] = id;
  } else {
    g_seenIds[g_seenHead] = id;
    g_seenHead = (g_seenHead + 1) % DEDUP_SIZE;
  }
}

// ============================================================
//  String helpers — trích field từ JSON thủ công (không ArduinoJson)
// ============================================================
static String jsonDecode(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c != '\\') { out += c; continue; }
    if (i + 1 >= in.length()) break;
    char n = in[++i];
    switch (n) {
      case '"':  out += '"';  break;
      case '\\': out += '\\'; break;
      case '/':  out += '/';  break;
      case 'n':  out += '\n'; break;
      case 'r':  out += '\r'; break;
      case 't':  out += '\t'; break;
      case 'b':  out += '\b'; break;
      case 'f':  out += '\f'; break;
      case 'u': {
        if (i + 4 >= in.length()) break;
        char hex[5] = { in[i+1], in[i+2], in[i+3], in[i+4], 0 };
        uint16_t cp = (uint16_t)strtoul(hex, nullptr, 16);
        i += 4;
        if (cp < 0x80) {
          out += (char)cp;
        } else if (cp < 0x800) {
          out += (char)(0xC0 | (cp >> 6));
          out += (char)(0x80 | (cp & 0x3F));
        } else {
          out += (char)(0xE0 | (cp >> 12));
          out += (char)(0x80 | ((cp >> 6) & 0x3F));
          out += (char)(0x80 | (cp & 0x3F));
        }
        break;
      }
      default: out += n;
    }
  }
  return out;
}

// Trích string value từ "key":"value" bắt đầu tìm tại vị trí `from`
static String extractStr(const String& s, int from, const String& key) {
  String pat = "\"" + key + "\":\"";
  int p = s.indexOf(pat, from);
  if (p < 0) return "";
  p += pat.length();
  int e = p;
  while (e < (int)s.length()) {
    if (s[e] == '\\') { e += 2; continue; }
    if (s[e] == '"')  break;
    e++;
  }
  if (e >= (int)s.length()) return "";
  return jsonDecode(s.substring(p, e));
}

// Trích số nguyên từ "key":123 HOẶC "key":"123"
static long long extractNum(const String& s, int from, const String& key) {
  String pat = "\"" + key + "\":";
  int p = s.indexOf(pat, from);
  if (p < 0) return 0;
  p += pat.length();

  // Bỏ qua khoảng trắng
  while (p < (int)s.length() && (s[p] == ' ' || s[p] == '\t')) p++;
  if (p >= (int)s.length()) return 0;

  // Trường hợp "key":"123"
  if (s[p] == '"') {
    p++;
    int e = s.indexOf('"', p);
    if (e < 0) return 0;
    return atoll(s.substring(p, e).c_str());
  }

  // Trường hợp "key":123
  return atoll(s.c_str() + p);
}

// ============================================================
//  Build form (giống Python formAll + queries)
// ============================================================
static String buildForm() {
  g_glReqCounter++;
  String reqStr = toBase36(g_glReqCounter);

  String queries;
  queries.reserve(200);
  queries += "{\"o0\":{\"doc_id\":\"";
  queries += DOC_ID_MESSAGE_THREADS;
  queries += "\",\"query_params\":{\"limit\":";
  queries += String(g_limit);
  queries += ",\"before\":null,\"tags\":[\"INBOX\"]";
  queries += ",\"includeDeliveryReceipts\":false";
  queries += ",\"includeSeqID\":true}}}";

  String form;
  form.reserve(512 + queries.length());
  auto add = [&](const String& k, const String& v) {
    if (form.length()) form += "&";
    form += k; form += "="; form += urlEncode(v);
  };
  add("fb_dtsg", g_fbDtsg);
  add("jazoest", g_jazoest);
  add("__a",     "1");
  add("__user",  g_uid);
  add("__req",   reqStr);
  add("__rev",   g_rev);
  add("av",      g_uid);
  add("queries", queries);
  return form;
}

// ============================================================
//  Fetch inbox
// ============================================================
bool fbGraphQLFetchInbox(String& outRaw) {
  outRaw = "";

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(20);

  HTTPClient https;
  https.setConnectTimeout(15000);
  https.setTimeout(25000);
  https.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);

  if (!https.begin(client, "https://www.facebook.com/api/graphqlbatch/")) {
    Serial.println("   ❌ [GQL] https.begin fail");
    return false;
  }

  https.addHeader("Content-Type", "application/x-www-form-urlencoded");
  https.addHeader("User-Agent",   WEB_UA);
  https.addHeader("Cookie",       g_cookies);
  https.addHeader("Origin",       "https://www.facebook.com");
  https.addHeader("Referer",      "https://www.facebook.com/");
  https.addHeader("Accept",       "*/*");

  String form = buildForm();
  int code = https.POST((uint8_t*)form.c_str(), form.length());
  if (code != 200) {
    Serial.printf("   ❌ [GQL] HTTP %d | err=%s\n",
                  code, https.errorToString(code).c_str());
    https.end();
    return false;
  }

  outRaw = https.getString();
  https.end();

  // Strip JSON-hijack
  if (outRaw.startsWith("for (;;);")) outRaw.remove(0, 9);

  // Cắt bỏ successful_results phía sau
  int sr = outRaw.indexOf("{\"successful_results\"");
  if (sr >= 0) outRaw = outRaw.substring(0, sr);

  return outRaw.length() > 0;
}

// ============================================================
//  Parse inbox — string parser, không ArduinoJson
// ============================================================
static void parseAndDispatch(const String& raw) {
  const String targetId = String(TARGET_THREAD_ID);

  // 1) Tìm thread_fbid của target
  String needle = "\"thread_fbid\":\"" + targetId + "\"";
  int pos = raw.indexOf(needle);
  if (pos < 0) {
    Serial.printf("   [GQL] Không thấy target thread trong %d bytes\n",
                  raw.length());
    return;
  }

  // 2) Tìm last_message sau đó (trong cùng node)
  int lmPos = raw.indexOf("\"last_message\":", pos);
  if (lmPos < 0) {
    Serial.println("   [GQL] Không có 'last_message'");
    return;
  }

  // 3) Lấy 12KB sau last_message — đủ chứa 1 node message + attachments
  int chunkEnd = lmPos + 12000;
  if (chunkEnd > (int)raw.length()) chunkEnd = raw.length();
  String chunk = raw.substring(lmPos, chunkEnd);

  // 4) Trích timestamp — thử nhiều field
  long long ts = extractNum(chunk, 0, "timestamp_precise");
  if (ts == 0) ts = extractNum(chunk, 0, "timestamp");

  if (ts == 0) {
    Serial.println("   ⚠️ [GQL] Không có timestamp_precise / timestamp.");
    Serial.println("   ─── Dump 400 ký tự sau last_message ───");
    Serial.printf("%.400s\n", chunk.c_str());
    Serial.println("   ───────────────────────────────────────");
    return;
  }

  // 5) Bỏ tin cũ → thoát sớm, không tốn RAM extract thêm
  if (ts <= g_lastSyncMs) return;

  // 6) Trích message_id (fallback offline_threading_id, fallback ts:<ts>)
  String mid = extractStr(chunk, 0, "message_id");
  if (mid.length() == 0) mid = extractStr(chunk, 0, "offline_threading_id");
  if (mid.length() == 0) {
    char buf[32]; snprintf(buf, sizeof(buf), "ts:%lld", ts);
    mid = String(buf);
  }

  // 7) Dedup
  if (isSeen(mid)) return;

  // 8) Trích snippet + sender id
  String body  = extractStr(chunk, 0, "snippet");
  String actor = "";
  int maPos = chunk.indexOf("\"messaging_actor\":");
  if (maPos >= 0) actor = extractStr(chunk, maPos, "id");

  // 9) Update state
  markSeen(mid);
  if (ts > g_lastSyncMs) g_lastSyncMs = ts;
  g_newMsgCount++;

  Serial.printf("   📊 [GQL] matched=1, new=1 | mid=%s | ts=%lld\n",
                mid.c_str(), ts);

  if (g_callback) g_callback(targetId, actor, body, mid, ts);
}

// ============================================================
//  Poll once
// ============================================================
bool fbGraphQLPollOnce() {
  g_pollCount++;
  unsigned long t0 = millis();

  String raw;
  if (!fbGraphQLFetchInbox(raw)) {
    Serial.printf("⚠️  [GQL] Poll #%lu FAIL (%lums)\n",
                  g_pollCount, millis() - t0);
    return false;
  }

  Serial.printf("📨 [GQL] Poll #%lu | %d bytes | %lums\n",
                g_pollCount, raw.length(), millis() - t0);

  parseAndDispatch(raw);
  return true;
}

// ============================================================
//  Listener blocking
// ============================================================
void fbGraphQLListen(unsigned long pollMs) {
  Serial.println("╔══════════════════════════════════════════");
  Serial.println("║ 📡 GraphQL Listener START");
  Serial.printf ("║ Poll interval : %lu ms\n", pollMs);
  Serial.printf ("║ Limit         : %d threads\n", g_limit);
  Serial.printf ("║ Target thread : %s\n", TARGET_THREAD_ID);
  Serial.printf ("║ Free heap     : %u bytes\n", (unsigned)ESP.getFreeHeap());
  Serial.println("╚══════════════════════════════════════════");

  if (g_lastSyncMs == 0) fbGraphQLResetBaseline();

  while (true) {
    unsigned long t0 = millis();

    String raw;
    if (!fbGraphQLFetchInbox(raw)) {
      g_pollCount++;
      Serial.printf("⚠️  [GQL] Poll #%lu FAIL\n", g_pollCount);
    } else {
      g_pollCount++;
      parseAndDispatch(raw);
      // Giải phóng capacity của raw ngay
            raw = "";
    }

    unsigned long elapsed = millis() - t0;
    if (elapsed < pollMs) delay(pollMs - elapsed);

    if (g_pollCount % 30 == 0) {
      char tag[32];
      snprintf(tag, sizeof(tag), "GQL poll #%lu", g_pollCount);
      logRAM(tag);
    }
  }
}