````cpp
#pragma once

// ================== WiFi ==================

#define WIFI_SSID      "yourwifiname"
#define WIFI_PASSWORD  "yourwifipassword"


// ================== GEMINI ==================

#define GEMINI_API_KEY     "your_gemini_api_key"
#define GEMINI_MODEL       "gemini-3.6-flash"
#define GEMINI_MAX_TOKENS  1024
#define GEMINI_TIMEOUT_MS  30000


// ================== GROQ ==================

#define MAX_QR_FROM_AI     6
#define MAX_QR_TITLE_LEN   20

#define GROQ_API_KEY       "your_groq_api_key"
#define GROQ_MODEL         "openai/gpt-oss-120b"
#define GROQ_MAX_TOKENS    1024
#define GROQ_TIMEOUT_MS    30000


// --- Quick Reply do AI tạo ---

#define GROQ_SYSTEM_PROMPT \
  "Bạn là trợ lý ảo vui vẻ, thân thiện trong group chat Facebook Messenger. " \
  "Trả lời ngắn gọn, dùng tiếng Việt, có thể dùng emoji. " \
  "KHÔNG dùng markdown (**, ##, ```) vì Facebook không render. " \
  "\n" \
  "QUY TẮC QUICK REPLY (nút bấm): " \
  "CHỈ tạo Quick Reply khi user YÊU CẦU RÕ RÀNG bằng các từ khóa: " \
  "\"lựa chọn nhanh\", \"tạo nút\", \"gợi ý nút\", \"quick reply\", " \
  "\"menu\", \"cho tôi chọn\", \"tạo lựa chọn\", \"tạo menu\". " \
  "Khi có yêu cầu, PHẢI bắt đầu câu trả lời bằng block: " \
  "[QR]Title1:Title1;Title2:Title2;Title3:Title3[/QR] " \
  "rồi XUỐNG DÒNG và viết text giải thích phía sau. " \
  "QUY TẮC BLOCK QR (BẮT BUỘC): tối đa 5 nút; " \
  "mỗi title tối đa 20 ký tự, ngắn gọn, có thể có emoji; " \
  "payload LUÔN giống hệt title (VD: Phở bò:Phở bò); " \
  "các nút cách nhau bởi dấu ';', KHÔNG có khoảng trắng thừa; " \
  "KHÔNG xuống dòng trong block QR; " \
  "KHÔNG viết gì khác trong block QR ngoài các nút. " \
  "Khi KHÔNG có yêu cầu rõ ràng, KHÔNG được thêm block [QR]...[/QR]. " \
  "Text phía sau block QR: 1-3 câu dẫn dắt ngắn gọn. " \
  "\n" \
  "VÍ DỤ 1 (có QR): User: tạo cho tôi 3 lựa chọn nhanh về đồ ăn. " \
  "Assistant: [QR]Phở bò:Phở bò;Bún chả:Bún chả;Cơm tấm:Cơm tấm[/QR]\n" \
  "Đây là 3 món ngon mình gợi ý cho bạn, chọn 1 nhé! 🍜 " \
  "\n" \
  "VÍ DỤ 2 (không QR): User: hôm nay thời tiết thế nào. " \
  "Assistant: Mình không có dữ liệu thời tiết realtime, bạn thử mở app thời tiết xem sao nhé! 🌤️"


// Prompt hệ thống – định hình tính cách bot

#define GEMINI_SYSTEM_PROMPT \
  "Bạn là trợ lý ảo vui vẻ, thân thiện trong group chat. " \
  "Trả lời ngắn gọn, dùng tiếng Việt, " \
  "có thể dùng emoji. Không trả lời dài dòng."


// ================== AUTO REBOOT ==================

#define AUTO_REBOOT_ENABLE     0
#define AUTO_REBOOT_PERIOD_MS  100000UL


// ================== FB TOKENS ==================

#define FB_DTSG_HARDCODED      "your_fb_dtsg"
#define FB_JAZOEST_HARDCODED   "your_fb_jazoest"
#define FB_REV_HARDCODED       "your_fb_revision"


// ================== COOKIE ==================

#define MANUAL_COOKIE \
  "your_facebook_cookie_here"


// ================== LOGIN FALLBACK ==================

#define FB_USERNAME "your_facebook_username"
#define FB_PASSWORD "your_facebook_password"


// ================== TARGET ==================

#define TARGET_THREAD_ID "your_target_thread_id"


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
````
