# 📱 FB Chat Bot trên ESP32 — README chi tiết

Bot Facebook Messenger chạy trên ESP32: đọc tin nhắn nhóm qua MQTT-over-WebSocket, trả lời tự động bằng Gemini hoặc Groq, hỗ trợ thả reaction, cập nhật cookie qua Web Portal, và debug qua Serial.

---

## 📑 Mục lục

1. [Tổng quan](#1-tổng-quan)
2. [Tính năng](#2-tính-năng)
3. [Kiến trúc thư mục](#3-kiến-trúc-thư-mục)
4. [Luồng hoạt động](#4-luồng-hoạt-động)
5. [Yêu cầu phần cứng & môi trường](#5-yêu-cầu-phần-cứng--môi-trường)
6. [Cài đặt & Build](#6-cài-đặt--build)
7. [Cấu hình config.h](#7-cấu-hình-configh)
8. [Lệnh trong group chat](#8-lệnh-trong-group-chat)
9. [Lệnh qua Serial Monitor](#9-lệnh-qua-serial-monitor)
10. [Web Setup Portal](#10-web-setup-portal)
11. [Cơ chế hoạt động chi tiết](#11-cơ-chế-hoạt-động-chi-tiết)
12. [Lưu trữ NVS](#12-lưu-trữ-nvs)
13. [Troubleshooting](#13-troubleshooting)
14. [Ghi chú kỹ thuật & hạn chế](#14-ghi-chú-kỹ-thuật--hạn-chế)

---

## 1. Tổng quan

Đây là firmware ESP32 (Arduino framework) hoạt động như một bot Facebook Messenger:

- Kết nối tới Facebook edge-chat qua **WebSocket**.
- Nói chuyện với Facebook bằng giao thức **MQTT 3.1** được nhúng trong WebSocket frame (giống cách Messenger Web hoạt động).
- Nhận tin nhắn nhóm theo thời gian thực từ topic `/t_ms`.
- Phản hồi bằng AI (**Gemini** / **Groq**) khi có lệnh `/ai` hoặc `/q`.
- Có thể thả reaction, gửi tin nhắn văn bản, cập nhật cookie động qua web.

**Điểm mạnh:**

- Không cần server trung gian — chạy hoàn toàn trên MCU.
- Không dùng thư viện `PubSubClient` hay `WebSocketsClient` nặng — code tự viết để tiết kiệm RAM.
- Có cơ chế keep-alive trước khi gọi API AI (tránh WS bị FB đóng trong lúc gọi HTTPS).
- Cookie tự động nạp từ NVS (ưu tiên) hoặc `config.h` (fallback).

---

## 2. Tính năng

| Nhóm | Tính năng |
|---|---|
| **Messaging** | Gửi tin nhắn text vào group, trả lời tự động, fallback message khi AI lỗi |
| **AI** | Tích hợp Gemini (`gemini-3.6-flash`) và Groq (`openai/gpt-oss-120b`) qua HTTPS |
| **Reaction** | Thả / gỡ reaction với emoji (dùng `webgraphql/mutation`, doc_id `1491398900900362`) |
| **Auth** | Parse cookie thủ công, verify qua `graphqlbatch`, login dự phòng qua `b-graph.facebook.com` |
| **Persistence** | Lưu cookie/dtsg/jazoest/rev vào NVS (`Preferences`) |
| **Web UI** | Portal tại `http://<esp32-ip>/` để cập nhật cookie không cần flash lại |
| **Serial CLI** | ~15 lệnh debug: `/info`, `/react`, `/ai`, `/setup`, `/nvs-clear`… |
| **Logging** | Log RAM có tag, phát hiện fragmentation, log periodic |
| **Auto-reboot** | Có sẵn cơ chế (đang tắt bằng macro) |
| **Fallback listener** | `fb_graphql_listen.cpp` — polling GraphQL nếu MQTT/WS chết |

---

## 3. Kiến trúc thư mục

```text
src/
├── core/
│   ├── app_state.{h,cpp}     # Biến global (cookie, uid, WS, AI pending…)
│   ├── config.h              # Toàn bộ macro cấu hình (WiFi, API key, FB)
│   ├── logger.{h,cpp}        # logRAM / logRAMFull
│   ├── storage.{h,cpp}       # NVS load/save/clear (namespace "fbcfg")
│   └── utils.{h,cpp}         # urlEncode, jsonGetStr, genThreadingId…
│
├── fb_api/
│   ├── cookie.{h,cpp}        # extractCookieValue, parseCookieAndFill
│   ├── fb_auth.{h,cpp}       # syncTimeFromHTTP, verifyCookie, doLogin
│   ├── fb_send.{h,cpp}       # sendGroupMessage (/messaging/send)
│   └── fb_reaction.{h,cpp}   # fbReactMessage (webgraphql/mutation)
│
├── net/
│   ├── ws_client.{h,cpp}     # WebSocket handshake + frame encode/decode
│   ├── mqtt.{h,cpp}          # MQTT packet build/parse nhúng trong WS
│   └── fb_graphql_listen.{h,cpp}  # Listener dự phòng (polling)
│
├── ai/
│   ├── gemini.{h,cpp}        # geminiAsk + handlePendingAI
│   └── groq.{h,cpp}          # groqAsk (OpenAI-compatible)
│
├── commands/
│   └── commands.{h,cpp}      # dispatch lệnh nhóm (/ai, /q)
│
└── ui/
    ├── serial_cmd.{h,cpp}    # CLI qua Serial Monitor
    └── setup_portal.{h,cpp}  # WebServer tại port 80
```

---

## 4. Luồng hoạt động

### 4.1. Khởi động (`setup()`)

```text
BOOT
 └─▶ WiFi.begin(SSID, PASSWORD)
     └─▶ storageLoad()  ──► nếu có cookie trong NVS → dùng
                          └─► không có → dùng MANUAL_COOKIE trong config.h
         └─▶ parseCookieAndFill() ──► g_uid, g_cookies
             └─▶ syncTimeFromHTTP() ──► settimeofday (để genThreadingId hợp lệ)
                 └─▶ verifyCookie() ──► g_lastSeqId (sync_sequence_id)
                     └─▶ wsConnect(sid) ──► handshake WS tới edge-chat.facebook.com
                         └─▶ sendMqttConnect() ──► MQTT CONNECT nhúng trong WS
```

### 4.2. Vòng lặp chính (`loop()`)

```text
loop()
 ├─▶ pollSerialInput()      # đọc lệnh Serial
 ├─▶ wsPoll()               # đọc WS frame → đẩy vào mqttRxBuffer
 ├─▶ processMqttBuffer()    # parse MQTT packet
 │    ├─ CONNACK            → sendCreateQueue()
 │    ├─ PUBLISH /t_ms      → handleMqttPublish()
 │    ├─ SUBACK / PINGRESP  → ignore
 │    └─ CLOSE              → wsClient.stop()
 ├─▶ handlePendingAI()      # nếu có /ai hoặc /q chờ xử lý
 ├─▶ keep-alive: PING MQTT + PING WS định kỳ
 └─▶ auto-reconnect nếu WS chết
```

### 4.3. Xử lý tin nhắn đến

```text
PUB /t_ms
 └─▶ deserializeJson()
     ├─ syncToken / firstDeltaSeqId / lastIssuedSeqId → cập nhật state
     └─ duyệt deltas[]
         ├─ bỏ qua nếu không đúng TARGET_THREAD_ID
         ├─ bỏ qua nếu actorFbId == g_uid (tin của bot)
         └─ nếu body bắt đầu bằng /ai hoặc /q:
             ├─ gửi ACK "Đã nhận lệnh..."
             ├─ g_aiPending = true
             ├─ g_aiService = "gemini" | "groq"
             └─ return (loop gọi handlePendingAI ở vòng sau)
```

### 4.4. Gọi AI (`handlePendingAI`)

```text
handlePendingAI()
 ├─ Copy g_aiPrompt / thread / service ra biến local
 ├─ Clear g_aiPending
 ├─ Pre-ping: MQTT PINGREQ + WS PING (giữ kết nối sống)
 ├─ geminiAsk() hoặc groqAsk()   ← HTTPS, block 3-5s
 ├─ Kiểm tra WS sau khi gọi
 └─ sendGroupMessage(thread, reply)
     └─ fallback "❌ Bot đang bận, thử lại sau nhé!" nếu AI fail
```

---

## 5. Yêu cầu phần cứng & môi trường

- **ESP32** (bất kỳ dòng nào có WiFi + PSRAM optional, khuyến nghị ESP32-WROOM-32 hoặc ESP32-S3).
- **Framework:** Arduino ESP32 core (đã test với ESP32 Arduino ≥ 2.0.x).
- **Thư viện:**
  - `ArduinoJson` (≥ 6.x, hỗ trợ cả API v7)
  - `WiFiClientSecure`, `HTTPClient`, `Preferences`, `WebServer`, `WiFi` (đi kèm core)
- Mạng **WiFi 2.4GHz** (ESP32 không hỗ trợ 5GHz).
- Tài khoản Facebook có cookie hợp lệ + `fb_dtsg`, `jazoest`, `rev` tương ứng.

---

## 6. Cài đặt & Build

### 6.1. Cấu trúc `platformio.ini` gợi ý

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200
lib_deps =
    bblanchon/ArduinoJson @ ^6.21.3
build_flags =
    -DCORE_DEBUG_LEVEL=0
```

### 6.2. Build & Upload


### 6.3. Lần đầu chạy

1. Sửa `config.h`:
   - `WIFI_SSID`, `WIFI_PASSWORD`
   - `MANUAL_COOKIE` (cookie FB mới nhất)
   - `FB_DTSG_HARDCODED`, `FB_JAZOEST_HARDCODED`, `FB_REV_HARDCODED`
   - `GEMINI_API_KEY` hoặc `GROQ_API_KEY`
   - `TARGET_THREAD_ID` (id nhóm cần bot)
2. Flash firmware.
3. Xem Serial Monitor để đảm bảo:
   ```text
   ✅ MQTT CONNACK | rc=0
   [MQTT] -> create_queue (seq=...)
   ```
4. Gửi tin `/ai hello` vào nhóm → bot trả lời.

---

## 7. Cấu hình `config.h`

```cpp
// ===== WiFi =====
#define WIFI_SSID     "..."
#define WIFI_PASSWORD "..."

// ===== Gemini =====
#define GEMINI_API_KEY     "..."
#define GEMINI_MODEL       "gemini-3.6-flash"
#define GEMINI_MAX_TOKENS  256
#define GEMINI_TIMEOUT_MS  30000

// ===== Groq =====
#define GROQ_API_KEY  "..."
#define GROQ_MODEL    "openai/gpt-oss-120b"
#define GROQ_MAX_TOKENS 513
#define GROQ_TIMEOUT_MS 30000

// Prompt hệ thống cho bot
#define GEMINI_SYSTEM_PROMPT "Bạn là trợ lý ảo vui vẻ..."
#define GROQ_SYSTEM_PROMPT   "Bạn là trợ lý ảo vui vẻ..."

// ===== Auto reboot (đang tắt) =====
#define AUTO_REBOOT_ENABLE     0
#define AUTO_REBOOT_PERIOD_MS  100000UL

// ===== FB tokens (fallback khi NVS trống) =====
#define FB_DTSG_HARDCODED    "..."
#define FB_JAZOEST_HARDCODED "..."
#define FB_REV_HARDCODED     "..."

// ===== Cookie thủ công =====
#define MANUAL_COOKIE "datr=...; c_user=...; xs=...;"

// ===== Login fallback (b-graph) =====
#define FB_USERNAME "..."
#define FB_PASSWORD "..."

// ===== Thread đích =====
#define TARGET_THREAD_ID "..."

// ===== Options =====
#define AUTO_REPLY        0   // 1 = trả lời MỌI tin (tốn RAM!)
#define DEBUG_RAW         0
#define LOG_RAM_PERIODIC  1
#define RAM_LOG_PERIOD_MS 60000
```

> ⚠️ **Lưu ý bảo mật:** Không commit `config.h` có cookie / API key / mật khẩu thật lên Git public. Thay các giá trị nhạy cảm bằng biến môi trường hoặc file cấu hình không được theo dõi (`.gitignore`) trước khi chia sẻ mã nguồn.

---

## 8. Lệnh trong group chat

| Lệnh | Mô tả | Service |
|---|---|---|
| `/ai <câu hỏi>` | Hỏi đáp với Gemini | Google Gemini |
| `/q <câu hỏi>` | Hỏi đáp với Groq | Groq (Llama / GPT-OSS) |
| `<text>` (không phải lệnh) | Bỏ qua (trừ khi `AUTO_REPLY=1`) | — |

**Quy trình khi có lệnh:**

1. Bot gửi ACK (thả tim nếu có `mid` thật, hoặc text "⚡ Đã nhận lệnh…").
2. Bot gọi AI tương ứng.
3. Bot gửi kết quả vào group.
4. Nếu AI fail → bot gửi "❌ Bot đang bận, thử lại sau nhé!".

**Chống spam:** nếu `g_aiPending == true` thì lệnh mới bị từ chối với câu "⏳ Đang xử lý câu trước, đợi chút nhé!".

---

## 9. Lệnh qua Serial Monitor

Mở Serial Monitor @ `115200` bps, gõ lệnh + Enter:

| Lệnh | Chức năng |
|---|---|
| `/help` | In menu trợ giúp |
| `/info` | In RAM + trạng thái WS/MQTT/UID/seq |
| `/random` | Gửi 1 câu ngẫu nhiên vào group |
| `/ping` | Test bot còn sống |
| `/reconnect` | Buộc WS ngắt & reconnect |
| `/clear` | Xoá buffer serial |
| `/cookie` | In cookie hiện tại (debug) |
| `/react <mid> <emoji>` | Thả reaction (emoji có thể dán trực tiếp hoặc dùng alias: `like`, `love`, `haha`, `wow`, `sad`, `angry`, `fire`, `care`) |
| `/unreact <mid>` | Gỡ reaction |
| `/ai <câu hỏi>` | Gọi Gemini trực tiếp (không qua group) |
| `/ai-info` | In cấu hình Gemini |
| `/setup` | Bật Web Portal (blocking, để cập nhật cookie) |
| `/nvs-info` | In cấu hình NVS |
| `/nvs-clear` | Xoá NVS → reboot về `config.h` |

Gõ text bất kỳ (không có `/`) → gửi vào `TARGET_THREAD_ID`.

---

## 10. Web Setup Portal

Chạy bằng lệnh Serial `/setup`. Portal mở trên port 80:

- `GET /` → form nhập cookie
- `GET /new_cookie` → giống `/`
- `POST /save` → lưu cookie vào NVS → reboot sau 2s
- Không route khác → 404

**Trường nhập:**

- `cookie` (bắt buộc, ≥ 20 ký tự)
- `dtsg`, `jazoest`, `rev` (để trống → giữ giá trị hardcode trong `config.h`)

**Cách dùng:**

1. Gõ `/setup` vào Serial.
2. Xem IP in ra, mở browser: `http://192.168.x.x/`.
3. Paste cookie mới → Save → ESP32 reboot → dùng cookie mới.

Ví dụ POST tự động (thay giá trị thật của bạn trước khi dùng):

```python
import requests
requests.post("http://192.168.1.100/save", data={
    "cookie": "<cookie_moi>",
    "dtsg": "<fb_dtsg>",
    "jazoest": "<jazoest>",
    "rev": "<rev>",
})
```

---

## 11. Cơ chế hoạt động chi tiết

### 11.1. WebSocket handshake (`ws_client.cpp`)

Gửi HTTP GET nâng cấp lên WebSocket tới:

```text
GET /chat?region=eag&sid=<sid> HTTP/1.1
Host: edge-chat.facebook.com
Upgrade: websocket
Sec-WebSocket-Protocol: mqtt
Cookie: <g_cookies>
User-Agent: Mozilla/5.0 (Linux; Android 9; SM-G973U…)
```

Frame WS được tự encode/decode (không qua thư viện):

- Client → Server: luôn masked (opcode `0x2` = binary, `0x9` = ping).
- Server → Client: unmasked.
- Xử lý tự động PING (`0x9`) → trả PONG (`0xA`).

### 11.2. MQTT nhúng trong WS

| Bước | Packet | Mô tả |
|---|---|---|
| 1 | `CONNECT` (0x10) | Client ID `esp_XXXXXXXX`, username = JSON chứa `u`, `s`, `d`, `chat_on`, `st:/t_ms`… |
| 2 | `CONNACK` (0x20) | Nếu `rc=0` → OK. Nếu `rc=4/5` → tăng `g_consecutiveConnFails`, đóng WS |
| 3 | `PUBLISH` (0x32) | Topic `/messenger_sync_create_queue` hoặc `/messenger_sync_get_diffs` |
| 4 | `SUBACK` (0x90) | (hiện tại bỏ qua — FB tự subscribe `st:/t_ms`) |
| 5 | `PUBLISH` đến | Topic `/t_ms` chứa delta JSON |
| 6 | `PUBACK` (0x40) | ACK cho QoS1 |

### 11.3. Sync token & sequence

- Lần đầu: `g_syncToken = ""` → gửi `create_queue` với `initial_titan_sequence_id = g_lastSeqId`.
- Các lần sau: `g_syncToken != ""` → gửi `get_diffs` với `last_seq_id` + `sync_token`.
- Khi `errorCode == ERROR_QUEUE_UNDERFLOW` → reset `g_lastSeqId = "0"`, gọi `verifyCookie()` lấy seq mới.

### 11.4. Parse delta

Mỗi delta có dạng:

```json
{
  "deltas": [{
    "messageMetadata": {
      "actorFbId": "...",
      "threadKey": {"threadFbId": "..."},
      "messageId": "mid.$...",
      "timestamp": 1789282859620
    },
    "body": "/ai hello"
  }],
  "firstDeltaSeqId": "123",
  "syncToken": "AQ..."
}
```

Bot lọc: `threadFbId == TARGET_THREAD_ID` và `actorFbId != g_uid`.

### 11.5. Gọi AI — chiến lược keep-alive

Vì HTTPS tới Gemini/Groq block luồng 3–5 giây, bot phải:

1. Gửi MQTT `PINGREQ` (`0xC0 0x00`) qua WS.
2. Gửi WS `PING` (opcode `0x9`).
3. Gọi HTTPS.
4. Sau khi trả lời, kiểm tra `wsClient.connected()` — nếu chết thì reset state.

### 11.6. Dedup tin nhắn

Trong `fb_graphql_listen.cpp` có ring buffer 100 phần tử để tránh xử lý trùng tin nhắn. Trong `mqtt.cpp` không dedup vì MQTT đã đảm bảo QoS1 + sequence ID.

---

## 12. Lưu trữ NVS

Namespace: `fbcfg` trong `Preferences`.

| Key | Ý nghĩa |
|---|---|
| `cookie` | Chuỗi cookie FB |
| `dtsg` | `fb_dtsg` |
| `jazoest` | `jazoest` |
| `rev` | `__rev` |

**Priority khi load:**

```text
NVS có cookie ≥ 20 ký tự? ──Yes──▶ dùng NVS
                          ──No───▶ dùng MANUAL_COOKIE trong config.h
```

Xoá NVS: gõ `/nvs-clear` → tự reboot.

---

## 13. Troubleshooting

| Triệu chứng | Nguyên nhân | Cách xử lý |
|---|---|---|
| `CONNACK rc=4/5` | Cookie chết hoặc `fb_dtsg` hết hạn | Cập nhật cookie qua `/setup` |
| WS Handshake FAIL | Cookie sai, UA bị block, hoặc IP bị chặn | Đổi UA, đổi WiFi, thử cookie mới |
| `ERROR_QUEUE_UNDERFLOW` | `g_lastSeqId` lệch | Bot tự gọi `verifyCookie()` lấy seq mới |
| `https.begin` FAIL | Hết RAM | Giảm `GEMINI_MAX_TOKENS`, tắt `AUTO_REPLY` |
| Gemini fail liên tục | API key hết quota / sai model | Kiểm tra key, đổi model trong `config.h` |
| Bot không thấy tin nhắn | `TARGET_THREAD_ID` sai | Lấy đúng `thread_fbid` của nhóm |
| Task watchdog reset | Loop block quá lâu | Kiểm tra HTTPS timeout, giảm số lần gọi |
| Free heap < 40KB | Fragmentation cao | Restart định kỳ, giảm buffer |

Debug RAM: xem tag `💾 [RAM]` trong Serial — theo dõi `free`, `minFree`, `maxBlk`, `frag%`.

---

## 14. Ghi chú kỹ thuật & hạn chế

### 14.1. Điểm mạnh kỹ thuật

- Tự viết WebSocket + MQTT → tiết kiệm ~30KB RAM so với thư viện.
- Hai service AI chuyển đổi runtime qua `g_aiService`.
- Cookie động qua portal → không cần recompile.
- Fallback listener (`fb_graphql_listen.cpp`) cho trường hợp WS không dùng được.

### 14.2. Hạn chế / rủi ro

- `client.setInsecure()` → chấp nhận mọi chứng chỉ TLS, không MITM-proof.
- Cookie hardcode trong `config.h` là rủi ro bảo mật nếu source leak.
- `AUTO_REPLY` mặc định tắt vì gọi Gemini song song với WS dễ OOM.
- `handlePendingAI()` block — bot không nhận tin khác trong lúc gọi AI (3–5s).
- Không có dedup trong MQTT — dựa vào `messageId` + sequence để FB không gửi lại.
- Chỉ hỗ trợ 1 group (`TARGET_THREAD_ID` — hardcode).
- Không hỗ trợ media (ảnh/video/file) — chỉ text.
- Phụ thuộc vào cấu trúc JSON của FB — nếu FB đổi schema, code parse sẽ lỗi.
