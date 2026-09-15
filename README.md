# 🤖 ESP32 FB Chat Bot

Bot Facebook Messenger chạy **trực tiếp trên chip ESP32** (Arduino framework) — không cần server trung gian, không cần Node.js/Python chạy nền. Bot đăng nhập bằng cookie Facebook, **polling GraphQL** để đọc tin nhắn mới trong một nhóm chat, và tự động trả lời bằng AI (**Google Gemini** hoặc **Groq**). Kèm theo là một script Python (`push_cookie.py`) để tự đăng nhập Facebook và bơm cookie mới lên ESP32 mỗi khi cookie cũ hết hạn.

> ⚠️ **Lưu ý quan trọng:** Repo này thao tác với Facebook thông qua các API nội bộ (không chính thức) của Messenger Web, bằng cookie tài khoản cá nhân. Việc này **vi phạm Điều khoản dịch vụ của Meta**, có thể khiến tài khoản bị khoá/hạn chế, và **không phải** là hình thức chatbot chính thức (Meta có Messenger Platform API dùng Page token cho mục đích đó). Hãy chỉ dùng trên tài khoản thử nghiệm, tự chịu trách nhiệm rủi ro, không dùng để spam hay xâm phạm quyền riêng tư người khác.

---

## 📑 Mục lục

1. [Tổng quan](#1-tổng-quan)
2. [Tính năng](#2-tính-năng)
3. [Kiến trúc thư mục](#3-kiến-trúc-thư-mục)
4. [Luồng hoạt động](#4-luồng-hoạt-động)
5. [Yêu cầu phần cứng & phần mềm](#5-yêu-cầu-phần-cứng--phần-mềm)
6. [Cài đặt & build firmware](#6-cài-đặt--build-firmware)
7. [Cấu hình `config.h`](#7-cấu-hình-configh)
8. [Lệnh trong group chat](#8-lệnh-trong-group-chat)
9. [Lệnh qua Serial Monitor](#9-lệnh-qua-serial-monitor)
10. [Web Setup Portal](#10-web-setup-portal)
11. [`push_cookie.py` — tự động refresh cookie](#11-push_cookiepy--tự-động-refresh-cookie)
12. [Cơ chế hoạt động chi tiết](#12-cơ-chế-hoạt-động-chi-tiết)
13. [Lưu trữ NVS](#13-lưu-trữ-nvs)
14. [Troubleshooting](#14-troubleshooting)
15. [Ghi chú kỹ thuật, hạn chế & rủi ro](#15-ghi-chú-kỹ-thuật-hạn-chế--rủi-ro)
16. [Giấy phép](#16-giấy-phép)

---

## 1. Tổng quan

Đây là firmware ESP32 (Arduino framework, viết bằng `main.ino` + thư viện con trong `src/`) hoạt động như một bot Facebook Messenger:

- **Đăng nhập** bằng cookie Facebook (dán trong `config.h` hoặc nạp qua Web Portal / `push_cookie.py`), không cần OAuth hay Messenger Platform chính thức.
- **Đọc tin nhắn mới** trong một nhóm/luồng chat cụ thể (`TARGET_THREAD_ID`) bằng cách **polling API GraphQL nội bộ** của Facebook (`api/graphqlbatch`) mỗi 2 giây — đây là cơ chế đang **thực sự chạy** trong `main.ino` hiện tại.
- **Trả lời tự động bằng AI** khi có lệnh `/ai` (Google Gemini) hoặc `/q` (Groq).
- Có thể **thả/gỡ reaction** vào tin nhắn, **gửi tin nhắn văn bản**, cập nhật cookie **không cần re-flash** qua Web Portal, và điều khiển/debug qua Serial Monitor.
- Ngoài ra repo còn có sẵn module **WebSocket + MQTT-over-WebSocket** (`src/net/ws_client.*`, `src/net/mqtt.*`) — mô phỏng đúng cách Messenger Web kết nối real-time tới `edge-chat.facebook.com`. Module này đã được viết đầy đủ nhưng **không được gọi trong `main.ino` hiện tại** (không có `#include` tương ứng) — có thể xem là code tham khảo/legacy cho hướng real-time, trong khi bản build hiện hành dùng polling GraphQL cho đơn giản và ổn định hơn.

**Điểm mạnh:**

- Không cần server trung gian — chạy hoàn toàn trên MCU.
- Không dùng `PubSubClient`/`WebSocketsClient` nặng cho phần MQTT/WS — code tự viết để tiết kiệm RAM (dù nhánh này hiện chưa active).
- Parser JSON tối giản viết tay cho response GraphQL (không cần load toàn bộ payload vào `ArduinoJson`, đỡ tốn heap).
- Cookie tự nạp ưu tiên từ NVS (Flash), fallback về `config.h` nếu NVS trống.
- Có kèm script Python để tự đăng nhập lại Facebook và đẩy cookie mới lên ESP32 khi cookie cũ hết hạn.

---

## 2. Tính năng

| Nhóm | Tính năng |
|---|---|
| **Nhận tin nhắn** | Polling `api/graphqlbatch` (doc_id message threads) mỗi 2s, tự parse JSON bằng string-matching, lọc đúng `TARGET_THREAD_ID`, dedup theo `message_id`/`offline_threading_id` |
| **Messaging** | Gửi tin nhắn text vào group (`/messaging/send/`), fallback message khi AI lỗi |
| **AI** | Tích hợp **Gemini** (`gemini-3.6-flash` qua `generativelanguage.googleapis.com`) và **Groq** (`openai/gpt-oss-120b` qua API tương thích OpenAI) |
| **Reaction** | Thả / gỡ reaction bằng emoji (`webgraphql/mutation`) |
| **Auth** | Parse cookie thủ công, đồng bộ giờ hệ thống qua header `Date` HTTP, verify cookie qua `graphqlbatch`, login dự phòng qua `b-graph.facebook.com` khi cookie chết |
| **Persistence** | Lưu cookie/`fb_dtsg`/`jazoest`/`__rev` vào NVS (`Preferences`, namespace `fbcfg`) |
| **Web UI** | Portal tại `http://<esp32-ip>/` để dán cookie mới không cần flash lại |
| **Serial CLI** | ~13 lệnh debug: `/info`, `/react`, `/ai`, `/setup`, `/nvs-clear`… |
| **Tự động hoá refresh cookie** | `push_cookie.py` — tự đăng nhập Facebook (email/password/2FA), lấy `fb_dtsg`/`jazoest`/`cookie`/`clientRevision` mới, `POST` thẳng lên `/save` của ESP32 |
| **Logging** | Log RAM có tag, log RAM định kỳ, cảnh báo fragmentation |
| **Auto-reboot** | Có sẵn cơ chế reboot định kỳ khi rảnh (tắt mặc định bằng macro `AUTO_REBOOT_ENABLE`) |
| **Real-time (dự phòng/chưa active)** | WebSocket + MQTT-over-WS đầy đủ trong `src/net/ws_client.*` & `src/net/mqtt.*`, chưa được gọi trong `main.ino` bản hiện tại |

---

## 3. Kiến trúc thư mục

```text
esp32_fbchat/
├── main.ino                    # Entry point: setup() + loop() — dùng GraphQL polling
├── push_cookie.py              # Script Python: tự login FB + đẩy cookie mới lên ESP32
├── LICENSE                     # GPL-2.0
├── LOG.md                      # Nhật ký thay đổi / debug log của tác giả
│
└── src/
    ├── core/
    │   ├── app_state.{h,cpp}      # Biến global (cookie, uid, WS state, AI pending…)
    │   ├── config.h               # Toàn bộ macro cấu hình (WiFi, API key, FB, options)
    │   ├── logger.{h,cpp}         # logRAM / logRAMFull — theo dõi heap & fragmentation
    │   ├── storage.{h,cpp}        # NVS load/save/clear (namespace "fbcfg")
    │   └── utils.{h,cpp}          # urlEncode, jsonGetStr, genThreadingId, parseHttpDate…
    │
    ├── fb_api/
    │   ├── cookie.{h,cpp}         # extractCookieValue, parseCookieAndFill
    │   ├── fb_auth.{h,cpp}        # syncTimeFromHTTP, verifyCookie, doLogin (b-graph fallback)
    │   ├── fb_send.{h,cpp}        # sendGroupMessage() → POST /messaging/send/
    │   └── fb_reaction.{h,cpp}    # fbReactMessage() → POST webgraphql/mutation
    │
    ├── net/
    │   ├── fb_graphql_listen.{h,cpp}  # ✅ ĐANG DÙNG — polling GraphQL đọc tin nhắn mới
    │   ├── ws_client.{h,cpp}          # WebSocket handshake + frame encode/decode (chưa active)
    │   └── mqtt.{h,cpp}               # MQTT packet build/parse nhúng trong WS (chưa active)
    │
    ├── ai/
    │   ├── gemini.{h,cpp}         # geminiAsk() + handlePendingAI() — gọi Google Gemini
    │   └── groq.{h,cpp}           # groqAsk() — gọi Groq (OpenAI-compatible)
    │
    ├── commands/
    │   └── commands.{h,cpp}       # handleGroupCommand() — dispatch /ai, /q trong group
    │
    └── ui/
        ├── serial_cmd.{h,cpp}     # CLI qua Serial Monitor
        └── setup_portal.{h,cpp}   # WebServer (port 80) để nhập cookie mới
```

---

## 4. Luồng hoạt động

### 4.1. Khởi động (`setup()` trong `main.ino`)

```text
BOOT
 └─▶ storageLoad() từ NVS
     ├─ Có cookie hợp lệ (≥ 20 ký tự) trong NVS?
     │   ├─ Có  → dùng cookie/dtsg/jazoest/rev từ NVS
     │   └─ Không → dùng MANUAL_COOKIE / FB_*_HARDCODED trong config.h
     └─▶ WiFi.begin(SSID, PASSWORD) (kèm cấu hình static IP 192.168.1.11)
         └─▶ syncTimeFromHTTP() ──► lấy giờ chuẩn qua header Date của facebook.com
             └─▶ parseCookieAndFill() ──► tách g_uid (c_user), g_cookies
                 ├─ Parse lỗi → mở SETUP PORTAL, dừng lại chờ người dùng nhập cookie
                 └─▶ verifyCookie() ──► gọi graphqlbatch, lấy sync_sequence_id
                     ├─ Fail → doLogin() (login qua b-graph.facebook.com bằng FB_USERNAME/PASSWORD)
                     │         → verify lại; nếu vẫn fail → mở SETUP PORTAL
                     └─▶ fbGraphQLSetCallback(onNewMessage)
                         fbGraphQLResetBaseline()  ← chỉ nhận tin có timestamp SAU thời điểm boot
```

### 4.2. Vòng lặp chính (`loop()`)

```text
loop()
 ├─▶ pollSerialInput()          # đọc lệnh gõ trên Serial Monitor
 ├─▶ (tuỳ chọn) AUTO_REBOOT     # reboot định kỳ nếu rảnh (mặc định tắt)
 ├─▶ nếu g_aiPending == true:
 │     handlePendingAI()  → ưu tiên xử lý AI trước, return ngay trong loop này
 ├─▶ mỗi GQL_POLL_INTERVAL_MS (2000ms):
 │     fbGraphQLPollOnce()  → POST tới api/graphqlbatch, parse & dispatch tin mới
 └─▶ (tuỳ chọn) log RAM định kỳ mỗi RAM_LOG_PERIOD_MS
```

### 4.3. Xử lý tin nhắn đến (`fb_graphql_listen.cpp`)

```text
fbGraphQLPollOnce()
 └─▶ fbGraphQLFetchInbox() — POST api/graphqlbatch (doc_id message_threads, tags=INBOX)
     └─▶ parseAndDispatch(raw)
         ├─ Tìm "thread_fbid":"<TARGET_THREAD_ID>" trong response
         ├─ Tìm "last_message" ngay sau đó, cắt ra 1 chunk ~12KB để trích field
         ├─ Lấy timestamp (timestamp_precise hoặc timestamp)
         │   └─ Nếu timestamp ≤ baseline (g_lastSyncMs) → bỏ qua (tin cũ)
         ├─ Lấy message_id (fallback offline_threading_id, fallback "ts:<timestamp>")
         │   └─ Nếu đã "seen" (dedup ring-buffer 100 phần tử) → bỏ qua
         ├─ Trích "snippet" (nội dung) và "messaging_actor".id (người gửi)
         └─▶ gọi callback onNewMessage() (trong main.ino)
             ├─ Bỏ qua nếu actorId == g_uid (tin do chính bot gửi)
             └─▶ handleGroupCommand(threadId, actorId, body, mid, timestamp)
                 ├─ body bắt đầu bằng "/ai " hoặc "/q "?
                 │   ├─ Nếu đang có AI pending → gửi "⏳ Đang xử lý câu trước..."
                 │   └─ Ngược lại → gửi ACK (thả ❤️ nếu có mid thật, hoặc text),
                 │                  set g_aiPending = true, lưu prompt/service/thread
                 └─ Không phải lệnh → bỏ qua (trừ khi AUTO_REPLY = 1)
```

> ℹ️ Vì `fbGraphQLFetchInbox()` gọi API **danh sách hội thoại** (message threads) chứ không phải API lấy toàn bộ lịch sử tin nhắn của 1 thread, bot chỉ nhìn thấy **tin nhắn cuối cùng** của mỗi thread mỗi lần poll — đủ để bắt lệnh mới nhất, nhưng có thể **bỏ lỡ tin nhắn** nếu trong vòng 2 giây có ≥ 2 tin được gửi liên tiếp vào group.

### 4.4. Gọi AI (`handlePendingAI()` trong `gemini.cpp`)

```text
handlePendingAI()
 ├─ Copy g_aiPrompt / g_aiThreadId / g_aiService ra biến local, clear g_aiPending
 ├─ geminiAsk(prompt, reply)   hoặc   groqAsk(prompt, reply)   ← HTTPS, block vài giây
 └─▶ sendGroupMessage(threadId, reply)
     └─ Nếu AI fail → gửi "❌ Bot đang bận, thử lại sau nhé!" (hoặc câu random dự phòng)
```

---

## 5. Yêu cầu phần cứng & phần mềm

**Phần cứng**

- Bất kỳ board **ESP32** có WiFi (khuyến nghị ESP32-WROOM-32 hoặc ESP32-S3, ≥ 4MB flash).
- Kết nối **WiFi 2.4GHz** (ESP32 không hỗ trợ 5GHz).

**Phần mềm để build firmware**

- Arduino IDE (≥ 2.x) hoặc PlatformIO, với **ESP32 Arduino core** (đã dùng các API `Preferences`, `WebServer`, `WiFiClientSecure`, `HTTPClient` đi kèm core — khuyến nghị core ≥ 2.0.x).
- Thư viện ngoài duy nhất cần cài thêm: **[ArduinoJson](https://arduinojson.org/)** (dùng ở `fb_auth.cpp` để parse JSON login).

**Phần mềm cho `push_cookie.py`** (tuỳ chọn, để tự động refresh cookie)

- Python ≥ 3.10 (script dùng cú pháp `TypeAlias`, `X | Y` union type).
- Thư viện: `httpx`, `requests`, `pyotp`.

**Tài khoản / thông tin cần có**

- Một tài khoản Facebook (khuyến nghị dùng tài khoản phụ/thử nghiệm) với cookie hợp lệ đang đăng nhập.
- ID của nhóm/luồng chat (`thread_fbid`) muốn bot theo dõi.
- API key **Gemini** (Google AI Studio) và/hoặc **Groq** nếu muốn dùng tính năng AI.

---

## 6. Cài đặt & build firmware

### 6.1. Clone & mở project

```bash
git clone https://github.com/Minh05012011/esp32_fbchat.git
cd esp32_fbchat
```

Mở `main.ino` bằng **Arduino IDE** (project dùng đường dẫn include kiểu `#include "src/core/config.h"`, nên **mở đúng thư mục gốc `esp32_fbchat` làm sketch folder**, không tách lẻ file).

### 6.2. Cài thư viện

Trong Arduino IDE: **Sketch → Include Library → Manage Libraries…** → cài `ArduinoJson` (bblanchon).

Board: **Tools → Board → ESP32 Arduino** → chọn đúng dòng board của bạn (vd. `ESP32 Dev Module`).

### 6.3. Cấu hình trước khi build

Mở `src/core/config.h` và điền các giá trị của bạn (xem chi tiết ở [mục 7](#7-cấu-hình-configh)). Đây là bước **bắt buộc** — nếu để nguyên placeholder (`YOUR_WIFI_SSID`, `YOUR_FACEBOOK_COOKIE`…) bot sẽ không kết nối được và tự chuyển sang Setup Portal.

> ⚠️ File `main.ino` có cấu hình **static IP cứng `192.168.1.11`** (`WiFi.config(...)` trong `setup()`). Nếu mạng của bạn dùng dải IP khác (không phải `192.168.1.x/24`, gateway `192.168.1.1`), hãy sửa lại 4 dòng `IPAddress` trong `main.ino`, hoặc xoá đoạn `WiFi.config(...)` để dùng DHCP mặc định.

### 6.4. Build & Upload

Chọn đúng cổng COM (**Tools → Port**) → nhấn **Upload**.

### 6.5. Chạy lần đầu

1. Mở **Serial Monitor** ở baudrate **115200**.
2. Theo dõi log boot — nếu mọi thứ đúng sẽ thấy:
   ```text
   ✅ WiFi | IP=192.168.1.11
   ✅ seq=...
   💡 Sẵn sàng! (GraphQL polling — không WS, không MQTT)
   ```
3. Gửi tin `/ai xin chào` vào đúng group có `TARGET_THREAD_ID` → chờ vài giây, bot sẽ thả ❤️ (ACK) rồi gửi câu trả lời.
4. Nếu thấy `❌ Cookie fail → Login` hoặc `❌ Login fail → SETUP PORTAL` → xem [mục 10](#10-web-setup-portal) để nhập cookie qua web.

---

## 7. Cấu hình `config.h`

File: `src/core/config.h`

```cpp
// ================== WiFi ==================
#define WIFI_SSID     "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// ================== GEMINI ==================
#define GEMINI_API_KEY      "YOUR_GEMINI_API_KEY"
#define GEMINI_MODEL        "gemini-3.6-flash"
#define GEMINI_MAX_TOKENS   256      // giới hạn output, tránh tràn RAM
#define GEMINI_TIMEOUT_MS   30000

// ================== GROQ ==================
#define GROQ_API_KEY  "YOUR_GROQ_API_KEY"
#define GROQ_MODEL    "openai/gpt-oss-120b"
#define GROQ_MAX_TOKENS 513
#define GROQ_TIMEOUT_MS 30000

// Prompt hệ thống — định hình "tính cách" của bot
#define GEMINI_SYSTEM_PROMPT "Bạn là trợ lý ảo vui vẻ, thân thiện trong group chat..."
#define GROQ_SYSTEM_PROMPT   "Bạn là trợ lý ảo vui vẻ, thân thiện trong group chat..."

// ================== AUTO REBOOT (đang tắt) ==================
#define AUTO_REBOOT_ENABLE    0
#define AUTO_REBOOT_PERIOD_MS 100000UL

// ================== FB TOKENS (fallback khi NVS trống) ==================
#define FB_DTSG_HARDCODED    "YOUR_FB_DTSG"
#define FB_JAZOEST_HARDCODED "YOUR_JAZOEST"
#define FB_REV_HARDCODED     "YOUR_FB_REV"

// ================== COOKIE thủ công (fallback khi NVS trống) ==================
#define MANUAL_COOKIE "YOUR_FACEBOOK_COOKIE"

// ================== Login dự phòng (khi cookie/token hết hạn) ==================
#define FB_USERNAME "YOUR_FB_UID_OR_EMAIL"
#define FB_PASSWORD "YOUR_FB_PASSWORD"

// ================== Thread đích ==================
#define TARGET_THREAD_ID "YOUR_TARGET_THREAD_ID"

// ================== Tuỳ chọn ==================
#define AUTO_REPLY        0     // 1 = trả lời MỌI tin nhắn (tốn quota AI + RAM!)
#define DEBUG_RAW         0
#define LOG_RAM_PERIODIC  1
#define RAM_LOG_PERIOD_MS 60000

// ================== User-Agent ==================
#define WEB_UA       "Mozilla/5.0 (Windows NT 10.0; Win64; x64) ..."
#define FB_LOGIN_UA  "Dalvik/2.1.0 ... [FBAN/FB4A;FBAV/340.0.0.27.113;...]"
```

**Cách lấy các giá trị Facebook:**

| Giá trị | Cách lấy |
|---|---|
| `MANUAL_COOKIE` | Đăng nhập `facebook.com` trên trình duyệt → F12 → tab Network → chọn 1 request bất kỳ → copy header `Cookie` |
| `FB_DTSG_HARDCODED` | Trong HTML trang chủ Facebook, tìm chuỗi `"token":"..."` sau `DTSGInitialData` (hoặc dùng `push_cookie.py` để lấy tự động) |
| `FB_JAZOEST_HARDCODED` | Tìm chuỗi `jazoest=` trong cùng trang |
| `FB_REV_HARDCODED` | Tìm `"client_revision":...` trong HTML |
| `TARGET_THREAD_ID` | Mở group chat trên Facebook Web → xem URL hoặc dùng DevTools để lấy `thread_fbid` |

> ⚠️ **Bảo mật:** Tuyệt đối không commit `config.h` chứa cookie/API key/mật khẩu thật lên GitHub public. Thêm `src/core/config.h` vào `.gitignore` (hoặc dùng file `config.local.h` riêng) trước khi push, hoặc lưu các giá trị nhạy cảm bên ngoài (NVS / Web Portal) thay vì hard-code.

---

## 8. Lệnh trong group chat

| Lệnh | Mô tả | Service |
|---|---|---|
| `/ai <câu hỏi>` | Hỏi đáp với AI | Google Gemini |
| `/q <câu hỏi>` | Hỏi đáp với AI | Groq |
| `<text>` khác | Bị bỏ qua (trừ khi `AUTO_REPLY = 1`, khi đó bot trả lời mọi tin) | — |

**Quy trình xử lý lệnh:**

1. Bot gửi ACK — thả ❤️ nếu có `message_id` thật, hoặc gửi text "⚡ Đã nhận lệnh, đang xử lý..." nếu không có mid (mid rỗng hoặc dạng giả `ts:...`, thường gặp khi chạy chế độ GraphQL polling).
2. Bot gọi AI tương ứng (Gemini hoặc Groq).
3. Bot gửi kết quả vào group.
4. Nếu AI lỗi → bot gửi "❌ Bot đang bận, thử lại sau nhé!".

**Chống spam:** nếu đang có 1 lệnh AI chưa xử lý xong (`g_aiPending == true`), lệnh mới sẽ bị từ chối kèm câu "⏳ Đang xử lý câu trước, đợi chút nhé!".

---

## 9. Lệnh qua Serial Monitor

Mở Serial Monitor ở **115200 bps**, gõ lệnh rồi Enter:

| Lệnh | Chức năng |
|---|---|
| `/help` hoặc `/?` | In menu trợ giúp |
| `/info` | In RAM + trạng thái UID / sync token / seq id / MQTT / WS |
| `/random` | Gửi 1 câu ngẫu nhiên vào group |
| `/ping` | Test bot còn sống ("pong!") |
| `/reconnect` | Buộc ngắt & reset trạng thái WS/MQTT (thuộc nhánh WS/MQTT chưa active) |
| `/clear` | Xoá buffer serial |
| `/cookie` | In cookie hiện tại + tách sẵn `c_user`, `datr`, `xs` (debug) |
| `/ai <câu hỏi>` | Gọi Gemini trực tiếp, in kết quả ra Serial (không gửi vào group) |
| `/ai-info` | In cấu hình Gemini hiện tại |
| `/react <mid> <emoji>` | Thả reaction — emoji dán trực tiếp hoặc dùng alias: `like`, `love`, `haha`, `wow`, `sad`, `angry`, `fire`, `care` |
| `/unreact <mid>` | Gỡ reaction |
| `/setup` | Bật Web Setup Portal (blocking) để nhập cookie mới |
| `/nvs-info` | In cấu hình đang lưu trong NVS |
| `/nvs-clear` | Xoá NVS → reboot, quay về dùng giá trị trong `config.h` |

Gõ text bất kỳ **không có `/`** ở đầu → gửi thẳng nội dung đó vào `TARGET_THREAD_ID`.

---

## 10. Web Setup Portal

Kích hoạt bằng lệnh Serial `/setup`, hoặc tự động bật khi cookie/login thất bại lúc boot. Portal chạy trên **port 80**:

| Route | Chức năng |
|---|---|
| `GET /` | Form HTML để nhập cookie/dtsg/jazoest/rev |
| `GET /new_cookie` | Giống `/` |
| `POST /save` | Lưu vào NVS → reboot ESP32 sau ~2 giây |
| Route khác | 404 |

**Trường nhập:**

- `cookie` — **bắt buộc**, tối thiểu 20 ký tự.
- `dtsg`, `jazoest`, `rev` — để trống nếu muốn giữ giá trị hardcode trong `config.h`.

**Cách dùng thủ công:**

1. Gõ `/setup` vào Serial Monitor.
2. Ghi lại IP ESP32 in ra (mặc định `192.168.1.11` nếu dùng static IP mặc định trong `main.ino`).
3. Mở trình duyệt tới `http://<ip-esp32>/`, dán cookie mới → **Save**.
4. ESP32 tự reboot và dùng cookie mới.

**Gửi tự động bằng `curl`/script** (thay giá trị thật trước khi dùng):

```bash
curl -X POST http://192.168.1.11/save \
  -d "cookie=<cookie_moi>" \
  -d "dtsg=<fb_dtsg>" \
  -d "jazoest=<jazoest>" \
  -d "rev=<rev>"
```

---

## 11. `push_cookie.py` — tự động refresh cookie

Cookie/`fb_dtsg` của Facebook hết hạn theo thời gian, khiến bot ngừng hoạt động (`graphqlbatch` trả lỗi). `push_cookie.py` là một script Python độc lập (chạy trên máy tính, **không chạy trên ESP32**) giúp tự động hoá việc này:

1. **Đăng nhập Facebook** bằng email/mật khẩu (hỗ trợ luôn 2FA qua `pyotp`) thông qua endpoint nội bộ `b-graph.facebook.com/auth/login`, giả lập request từ app Facebook Android (FB4A).
2. Lấy về `fb_dtsg`, `jazoest`, `sessionID`, `clientRevision`, và cookie đầy đủ.
3. Lưu cache phiên đăng nhập vào `config.json` cạnh script (để lần sau không phải đăng nhập lại từ đầu nếu chưa hết hạn).
4. **`POST`** trực tiếp các giá trị này lên `http://<esp32-ip>/save` (mặc định `http://192.168.1.11/save`, đúng route của Setup Portal ở mục 10) — ESP32 sẽ tự lưu vào NVS và reboot.

### Cài đặt

```bash
pip install httpx requests pyotp
```

### Cấu hình

Mở `push_cookie.py`, sửa các hằng số ở đầu file (hoặc tạo file `.env` ở thư mục gốc repo — script tự đọc `KEY=VALUE` từ đó nếu biến môi trường tương ứng chưa được set):

```python
EMAIL = "email_hoac_sdt_facebook_cua_ban"
PASSWORD = "mat_khau_facebook"
OTP = None              # mã 2FA cố định nếu có; để None nếu dùng pyotp tự sinh / không cần
ESP32_URL = "http://192.168.1.11/save"   # sửa lại nếu ESP32 dùng IP khác
```

### Chạy

```bash
python push_cookie.py
```

Script sẽ in ra thông tin đăng nhập (FacebookID, clientRevision, jazoest, độ dài cookie…) và kết quả `POST` lên ESP32. Nếu thấy `❌ Không kết nối được tới ESP32`, kiểm tra: ESP32 có đang bật Setup Portal / cùng mạng LAN / đúng IP hay không.

> 💡 Gợi ý: có thể đặt script này chạy định kỳ bằng cron job / Task Scheduler để cookie luôn được làm mới mà không cần thao tác tay.

---

## 12. Cơ chế hoạt động chi tiết

### 12.1. Polling GraphQL để nhận tin nhắn (`fb_graphql_listen.cpp`)

Mỗi 2 giây (`GQL_POLL_INTERVAL_MS` trong `main.ino`), bot gửi 1 request:

```text
POST https://www.facebook.com/api/graphqlbatch/
Content-Type: application/x-www-form-urlencoded
Cookie: <g_cookies>

fb_dtsg=...&jazoest=...&__a=1&__user=<uid>&__req=<base36 counter>&__rev=...
&queries={"o0":{"doc_id":"3336396659757871","query_params":{"limit":15,"before":null,"tags":["INBOX"],"includeDeliveryReceipts":false,"includeSeqID":true}}}
```

Response được strip phần chống JSON-hijack (`for (;;);`), sau đó bot **không dùng ArduinoJson** để parse toàn bộ mà tìm trực tiếp bằng `indexOf()`:

1. Tìm `"thread_fbid":"<TARGET_THREAD_ID>"`.
2. Tìm `"last_message":` gần nhất sau đó, cắt ra một đoạn ~12KB xung quanh.
3. Trích `timestamp_precise`/`timestamp`, `message_id`/`offline_threading_id`, `snippet`, `messaging_actor.id`.
4. So sánh với baseline (`g_lastSyncMs`, được set = thời điểm boot) và dedup ring-buffer 100 phần tử để tránh xử lý trùng.

Cách làm này **rất tiết kiệm RAM** (không cần buffer JSON khổng lồ) nhưng đổi lại **chỉ thấy được tin nhắn mới nhất** của mỗi thread mỗi lần poll.

### 12.2. Xác thực & đồng bộ thời gian

- `syncTimeFromHTTP()`: lấy header `Date` từ response HTTP của `facebook.com` để set giờ hệ thống (`settimeofday`) — cần thiết vì ESP32 không có RTC pin và các request ký (`__req`, `timestamp`) cần giờ tương đối chính xác.
- `verifyCookie()`: gọi `graphqlbatch` để kiểm tra cookie còn sống, lấy `sync_sequence_id` ban đầu.
- `doLogin()`: nếu cookie chết, thử đăng nhập lại qua `b-graph.facebook.com` bằng `FB_USERNAME`/`FB_PASSWORD` (giả lập app FB4A qua `FB_LOGIN_UA`).

### 12.3. Gửi tin & reaction

- `sendGroupMessage()` → `POST https://www.facebook.com/messaging/send/` với các field chuẩn Messenger Web (`fb_dtsg`, `action_type=ma-type:user-generated-message`, `thread_fbid`, `body`, `author=fbid:<uid>`, `timestamp`…).
- `fbReactMessage()` → `POST` tới `webgraphql/mutation` (doc_id `1491398900900362`) để thêm/gỡ reaction lên một `message_id`.

### 12.4. Module WebSocket + MQTT (có sẵn, chưa được gọi trong bản build hiện tại)

`src/net/ws_client.cpp` và `src/net/mqtt.cpp` cài đặt đầy đủ:

- WebSocket handshake thủ công tới `edge-chat.facebook.com` (`Sec-WebSocket-Protocol: mqtt`), tự encode/decode frame (mask khi gửi, xử lý PING/PONG).
- Gói tin MQTT 3.1 (`CONNECT`, `CONNACK`, `PUBLISH`, `SUBACK`, `PUBACK`, `PINGREQ`) nhúng trong payload WebSocket, đúng cách Messenger Web dùng để nhận sự kiện real-time qua topic `/t_ms`.

Đây là hướng tiếp cận **real-time** (không cần polling, độ trễ thấp hơn nhiều), nhưng `main.ino` hiện tại **không `#include`** hai file này (chỉ include `fb_graphql_listen.h`) — nghĩa là bản firmware bạn build ra từ repo ở trạng thái hiện tại chạy **hoàn toàn bằng polling GraphQL**. Nếu muốn dùng lại nhánh WS/MQTT, cần tự nối lại phần gọi `wsConnect()`/`sendMqttConnect()`/`wsPoll()` vào `setup()`/`loop()` trong `main.ino`.

---

## 13. Lưu trữ NVS

Namespace: **`fbcfg`** trong `Preferences`.

| Key | Ý nghĩa |
|---|---|
| `cookie` | Chuỗi cookie Facebook đầy đủ |
| `dtsg` | `fb_dtsg` |
| `jazoest` | `jazoest` |
| `rev` | `__rev` (`clientRevision`) |

**Thứ tự ưu tiên khi boot:**

```text
NVS có "cookie" ≥ 20 ký tự?
  ├─ Có  → dùng cookie/dtsg/jazoest/rev từ NVS
  └─ Không → dùng MANUAL_COOKIE / FB_*_HARDCODED trong config.h
```

Xoá toàn bộ NVS: gõ Serial `/nvs-clear` → ESP32 tự reboot và quay lại dùng `config.h`.

---

## 14. Troubleshooting

| Triệu chứng | Nguyên nhân khả dĩ | Cách xử lý |
|---|---|---|
| `verifyCookie()` trả rỗng, bot login liên tục | Cookie chết hoặc `fb_dtsg`/`jazoest` hết hạn | Cập nhật cookie qua `/setup` hoặc chạy `push_cookie.py` |
| `❌ [GQL] HTTP <code>` khi poll | Cookie sai, bị Facebook chặn request, hoặc mất mạng | Kiểm tra WiFi, thử cookie mới, đổi `WEB_UA` nếu bị nghi ngờ là bot |
| `[GQL] Không thấy target thread` liên tục | `TARGET_THREAD_ID` sai, hoặc tài khoản chưa từng nhắn/nằm trong thread đó | Kiểm tra lại `thread_fbid` |
| Bot bỏ lỡ một vài tin nhắn gửi liên tiếp | Cơ chế polling chỉ lấy `last_message` của thread mỗi 2s | Giảm `GQL_POLL_INTERVAL_MS` (đánh đổi tốn pin/RAM/HTTP request hơn), hoặc dùng lại nhánh WS/MQTT |
| `https.begin` fail / bot reset (watchdog) | Hết RAM / heap fragmentation | Giảm `GEMINI_MAX_TOKENS`/`GROQ_MAX_TOKENS`, tắt `AUTO_REPLY`, theo dõi log `💾 [RAM]` |
| Gemini/Groq trả lỗi liên tục | API key sai/hết quota, sai tên model | Kiểm tra lại `GEMINI_API_KEY`/`GROQ_API_KEY`, thử đổi `GEMINI_MODEL`/`GROQ_MODEL` |
| Không truy cập được Web Portal | Sai IP, ESP32 chưa vào chế độ `/setup`, khác dải mạng | Xem lại static IP hardcode `192.168.1.11` trong `main.ino`, sửa cho khớp mạng của bạn |
| `push_cookie.py` báo `Không kết nối được tới ESP32` | ESP32 chưa bật Web Portal, khác WiFi, hoặc sai `ESP32_URL` | Gõ `/setup` trên ESP32 trước, kiểm tra cùng mạng LAN, sửa `ESP32_URL` cho đúng IP |
| `push_cookie.py` báo lỗi đăng nhập / cần 2FA | Facebook yêu cầu xác minh thêm | Cấu hình `OTP`/`pyotp` secret đúng, hoặc đăng nhập thủ công 1 lần trên trình duyệt trước |

Debug RAM: theo dõi tag `💾 [RAM]` trong Serial — các chỉ số `free`, `minFree`, `maxBlk`, `frag%` giúp phát hiện rò rỉ bộ nhớ / phân mảnh heap sớm.

---

## 15. Ghi chú kỹ thuật, hạn chế & rủi ro

### 15.1. Điểm mạnh kỹ thuật

- Parser GraphQL tự viết bằng string-matching thay vì `ArduinoJson` cho payload lớn → tiết kiệm đáng kể RAM.
- Hai service AI (Gemini/Groq) chuyển đổi runtime qua lệnh (`/ai` vs `/q`), tách biệt code trong `src/ai/`.
- Cookie có thể cập nhật động qua Web Portal hoặc `push_cookie.py`, không cần recompile/reflash để đổi tài khoản hoặc gia hạn phiên.
- Sẵn module WebSocket/MQTT tự viết cho hướng real-time, có thể tái sử dụng nếu muốn giảm độ trễ.

### 15.2. Hạn chế / rủi ro

- **Rủi ro tài khoản:** dùng cookie cá nhân để giả lập trình duyệt/app chính thức, vi phạm ToS của Meta → tài khoản có thể bị đăng xuất cưỡng bức, checkpoint, hoặc khoá.
- `client.setInsecure()` được dùng ở mọi nơi gọi HTTPS → **chấp nhận mọi chứng chỉ TLS**, không chống được tấn công MITM.
- Cookie/API key/mật khẩu hardcode trong `config.h` là rủi ro bảo mật nghiêm trọng nếu source code bị lộ (đã nêu ở mục 7).
- `handlePendingAI()` **block** vòng lặp chính khi gọi AI (vài giây) — bot không xử lý tin/lệnh khác trong lúc đó.
- Cơ chế polling GraphQL hiện tại **chỉ thấy tin nhắn cuối cùng** mỗi thread mỗi lần poll → có thể bỏ lỡ tin nếu nhiều người nhắn dồn dập.
- `AUTO_REPLY` mặc định tắt vì gọi AI cho **mọi** tin nhắn dễ tốn quota API và gây nghẽn/OOM.
- Chỉ hỗ trợ theo dõi **1 nhóm duy nhất** (`TARGET_THREAD_ID` hardcode trong `config.h`).
- Không hỗ trợ gửi/nhận media (ảnh, video, file) — chỉ xử lý văn bản.
- Toàn bộ cơ chế phụ thuộc cấu trúc JSON/HTML nội bộ của Facebook — Meta có thể thay đổi bất cứ lúc nào khiến code parse (đặc biệt phần string-matching trong `fb_graphql_listen.cpp`) ngừng hoạt động mà không báo trước.
- Static IP `192.168.1.11` hardcode trong `main.ino` — cần sửa tay nếu mạng của bạn có dải IP khác.

---

## 16. Giấy phép

Repo phát hành theo giấy phép **GNU General Public License v2.0** — xem chi tiết trong file [`LICENSE`](./LICENSE).
