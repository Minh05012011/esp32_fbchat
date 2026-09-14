#pragma once

// ================== WiFi ==================
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ================== GEMINI ==================
#define GEMINI_API_KEY      "YOUR_GEMINI_API_KEY"      // API key của bạn
#define GEMINI_MODEL        "gemini-3.6-flash"
#define GEMINI_MAX_TOKENS   256                        // giới hạn output, tránh tràn RAM
#define GEMINI_TIMEOUT_MS   30000                      // 30s

// ================== GROQ ==================
#define GROQ_API_KEY \
  "YOUR_GROQ_API_KEY"
#define GROQ_MODEL          "openai/gpt-oss-120b"
#define GROQ_MAX_TOKENS   513
#define GROQ_TIMEOUT_MS   30000

#define GROQ_SYSTEM_PROMPT \
  "Bạn là trợ lý ảo vui vẻ, thân thiện trong group chat. " \
  "Trả lời ngắn gọn (tối đa 3 câu), dùng tiếng Việt, " \
  "có thể dùng emoji. Không trả lời dài dòng."

// Prompt hệ thống – định hình tính cách bot
#define GEMINI_SYSTEM_PROMPT \
  "Bạn là trợ lý ảo vui vẻ, thân thiện trong group chat. " \
  "Trả lời ngắn gọn, dùng tiếng Việt, " \
  "có thể dùng emoji. Không trả lời dài dòng."

// ================== AUTO REBOOT ==================
#define AUTO_REBOOT_ENABLE    0
#define AUTO_REBOOT_PERIOD_MS  100000UL   // 1 phút (60000 ms)

// ================== FB TOKENS ==================
#define FB_DTSG_HARDCODED    "YOUR_FB_DTSG"
#define FB_JAZOEST_HARDCODED "YOUR_JAZOEST"
#define FB_REV_HARDCODED     "YOUR_FB_REV"

// ================== COOKIE (paste tại đây) ==================
#define MANUAL_COOKIE \
  "YOUR_FACEBOOK_COOKIE"

// ================== LOGIN FALLBACK ==================
#define FB_USERNAME "YOUR_FB_UID_OR_EMAIL"
#define FB_PASSWORD "YOUR_FB_PASSWORD"

// ================== TARGET ==================
#define TARGET_THREAD_ID "YOUR_TARGET_THREAD_ID"

// ================== OPTIONS ==================
#define AUTO_REPLY        0
#define DEBUG_RAW         0
#define LOG_RAM_PERIODIC  1
#define RAM_LOG_PERIOD_MS 60000

// ================== USER AGENTS ==================
#define WEB_UA \
  "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 " \
  "(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"

#define FB_LOGIN_UA \
  "Dalvik/2.1.0 (Linux; U; Android 7.1.2; SM-G988N Build/NRD90M) " \
  "[FBAN/FB4A;FBAV/340.0.0.27.113;FBPN/com.facebook.katana;FBLC/vi_VN;" \
  "FBBV/324485361;FBCR/Viettel Mobile;FBMF/samsung;FBBD/samsung;" \
  "FBDV/SM-G988N;FBSV/7.1.2;FBCA/x86:armeabi-v7a;" \
  "FBDM/{density=1.0,width=540,height=960};FB_FW/1;FBRV/0;]"