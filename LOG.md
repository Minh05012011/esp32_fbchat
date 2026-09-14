### 📂 Cấu trúc dự án & Mã nguồn

```text
esp32_fbchat/
└── src/
    ├── main.ino            # setup() + loop()
    ├── config.h            # Toàn bộ cấu hình (WiFi, tokens, target, UA, options)
    ├── app_state.h / .cpp  # Khai báo & định nghĩa biến toàn cục
    ├── logger.h / .cpp     # logRAM(), logRAMFull()
    ├── utils.h / .cpp      # urlEncode, parseHttpDate, gen*, jsonGetStr, MSG_POOL
    ├── cookie.h / .cpp     # extractCookieValue, parseCookieAndFill, extractCookiesFromLogin
    ├── fb_auth.h / .cpp    # syncTimeFromHTTP, verifyCookie, doLogin
    ├── fb_send.h / .cpp    # sendGroupMessage
    ├── ws_client.h / .cpp  # wsConnect, wsSendFrame, wsPoll
    ├── mqtt.h / .cpp       # MQTT builders, sendMqttConnect, sendCreateQueue, PUB handler
    └── serial_cmd.h / .cpp # printSerialHelp, handleSerialCommand, pollSerialInput
```

## Changelog — 13/09/2026

1. **Thêm reaction emoji cho tin nhắn** — sửa cmd serial để test
2. **Tích hợp Gemini AI** (`/ai <câu hỏi>`)
3. **Fix lỗi server đóng ngẫu nhiên (code 1000)** — random client ID mỗi lần reconnect
4. **Tích hợp Groq API** (`/q <câu hỏi>`)
5. **Auto reboot** định kỳ — chỉ khi rảnh, không cắt ngang lệnh
6. **Auto new cookie** — Setup Portal qua WebServer khi cookie hết hạn

---

## 🚀 Changelog — 14/09/2026

> Tái cấu trúc toàn bộ dự án theo kiến trúc phân lớp (layered architecture), tách rõ trách nhiệm từng module và bổ sung phương thức lắng nghe tin nhắn qua GraphQL polling.

### 📁 Cấu trúc thư mục mới

```text
ESP32_FB_Listener/
├── main.ino                       ← Entry point (setup/loop)
└── src/
    ├── core/                      ── NỀN TẢNG ──
    │   ├── app_state.cpp/.h       → Biến toàn cục
    │   ├── config.h               → Hằng số cấu hình
    │   ├── logger.cpp/.h          → Log RAM, heap, fragmentation
    │   ├── utils.cpp/.h           → URL encode, ID generators
    │   └── storage.cpp/.h         → NVS (Preferences) — lưu cookie
    │
    ├── fb_api/                    ── FACEBOOK HTTP ──
    │   ├── cookie.cpp/.h          → Parse & extract cookie
    │   ├── fb_auth.cpp/.h         → Verify cookie, login fallback
    │   ├── fb_send.cpp/.h         → Gửi tin nhắn
    │   └── fb_reaction.cpp/.h     → Thả / gỡ reaction
    │
    ├── net/                       ── REALTIME NETWORK ──
    │   ├── ws_client.cpp/.h       → WebSocket TLS
    │   ├── mqtt.cpp/.h            → MQTT-over-WS
    │   └── fb_graphql_listen.cpp/.h → GraphQL polling listener (MỚI)
    │
    ├── ai/                        ── DỊCH VỤ AI ──
    │   ├── gemini.cpp/.h          → Google Gemini API
    │   └── groq.cpp/.h            → Groq API
    │
    ├── commands/                  ── XỬ LÝ LỆNH (MỚI) ──
    │   ├── commands.cpp           → handleGroupCommand(), sendAck()
    │   └── commands.h             → Khai báo interface lệnh nhóm
    │
    └── ui/                        ── GIAO DIỆN ──
        ├── serial_cmd.cpp/.h      → Lệnh Serial
        └── setup_portal.cpp/.h    → WebServer cấu hình cookie
```

---

## 🧩 Chi tiết các module

### 1️⃣ Tầng CORE — Nền tảng

| File | Chức năng chính |
|---|---|
| `app_state` | Chứa toàn bộ biến toàn cục: `g_uid`, `g_cookies`, `g_fbDtsg`, `g_jazoest`, `g_rev`, `g_lastSeqId`, `g_syncToken`, `wsClient`, `mqttRxBuffer`, các counter (`g_msgReceived`, `g_msgSent`, `g_wsReconnects`), cờ `g_aiPending`, `g_aiService`. |
| `config.h` | Tập trung mọi hằng số: WiFi SSID/password, API key Gemini/Groq, `TARGET_THREAD_ID`, User-Agent, các cờ `AUTO_REPLY`, `LOG_RAM_PERIODIC`, cấu hình auto-reboot. |
| `logger` | `logRAM()` in 1 dòng gọn (free/minFree/maxBlk/frag), `logRAMFull()` in bảng đầy đủ kèm counters, uptime, CPU freq. |
| `utils` | `urlEncode()` chuẩn RFC 3986, `jsonGetStr()`, `parseHttpDate()` chuyển HTTP Date → `time_t`, `genThreadingId()`, `genClientId()`, `genSessionId()`, `getRandomMessage()` (pool 7 câu). |
| `storage` | Dùng `Preferences` (NVS) lưu struct `FBConfig` gồm cookie/dtsg/jazoest/rev. Load khi boot, save qua Setup Portal, clear qua serial. |

### 2️⃣ Tầng FB_API — Giao tiếp Facebook qua HTTP

| File | Chức năng |
|---|---|
| `cookie` | `parseCookieAndFill()` tách `c_user`, `datr`, `sb`, `xs`. `extractCookiesFromLogin()` bóc `session_cookies` từ response login. |
| `fb_auth` | `syncTimeFromHTTP()` đồng bộ giờ qua header `Date` của FB. `verifyCookie()` gọi `graphqlbatch` doc_id `3336396659757871` để lấy `sync_sequence_id` → xác nhận cookie sống. `doLogin()` fallback qua `b-graph.facebook.com` khi cookie chết. |
| `fb_send` | `sendGroupMessage(threadId, body)` POST `/messaging/send` với đầy đủ form FB yêu cầu: `fb_dtsg`, `jazoest`, `thread_fbid`, `body`, `offline_threading_id`, `threading_id`, `source=source:chat:web`… |
| `fb_reaction` | `fbReactMessage(mid, emoji, remove)` POST `/webgraphql/mutation/` doc_id `1491398900900362`, action `ADD_REACTION` / `REMOVE_REACTION`. Có hàm `toBase36()` sinh `__req` giống Python. |

### 3️⃣ Tầng NET — Realtime Network

| File | Chức năng |
|---|---|
| `ws_client` | Handshake WebSocket thủ công lên `edge-chat.facebook.com:443` (TLS + HTTP Upgrade). Encode/decode frame theo RFC 6455 (mask, extended length, ping/pong/close). Hàm `wsPoll()` trả về opcode + payload, tự xử lý ping và close. |
| `mqtt` | Đóng gói MQTT packet thủ công: `CONNECT`, `SUBSCRIBE`, `PUBLISH` (QoS1), `PUBACK`. Parser buffer MQTT nhận từ WS. Xử lý `CONNACK`, `SUBACK`, `PINGRESP`. Đăng ký queue qua `/messenger_sync_create_queue`, lấy diff qua `/messenger_sync_get_diffs`. Xử lý delta → trích `messageMetadata` → dispatch nội dung tin nhắn. |

### 4️⃣ Tầng AI

| File | Chức năng |
|---|---|
| `gemini` | `geminiAsk(prompt, reply)` gọi `generativelanguage.googleapis.com/v1beta/models/{model}:generateContent`. Body có `systemInstruction` định hình tính cách bot. `handlePendingAI()` chạy khi loop phát hiện `g_aiPending` — gửi keep-alive MQTT/WS trước khi block 3-5s gọi AI. |
| `groq` | `groqAsk()` gọi API OpenAI-compatible của Groq (`api.groq.com/openai/v1/chat/completions`), hỗ trợ model Llama-3.3-70B. Cấu trúc request theo chuẩn `messages: [system, user]`. |

### 5️⃣ Tầng UI

| File | Chức năng |
|---|---|
| `serial_cmd` | `pollSerialInput()` đọc từng byte (hỗ trợ UTF-8 emoji). `handleSerialCommand()` xử lý lệnh `/help`, `/info`, `/ai`, `/ai-info`, `/random`, `/ping`, `/reconnect`, `/clear`, `/cookie`, `/react`, `/unreact`, `/setup`, `/nvs-info`, `/nvs-clear`. Có bảng alias emoji (`like`→👍, `love`→❤️…). Gõ text không có `/` → gửi thẳng vào group. |
| `setup_portal` | WebServer port 80 phục vụ form HTML nhập cookie/dtsg/jazoest/rev, POST `/save` → lưu NVS → reboot. Chạy blocking khi cookie chết hoặc NVS trống. |

### 6️⃣ Tầng COMMANDS — Xử lý lệnh nhóm (MỚI)

| File | Chức năng |
|---|---|
| `commands.h` | Khai báo `handleGroupCommand(threadId, actorId, body, mid, timestamp)` — entry point xử lý mọi tin nhắn nhóm đến từ tầng dispatch (GraphQL/MQTT). Tham số `mid` để dành cho tương lai vì hiện GraphQL không trả `message_id` thật. |
| `commands.cpp` | **`sendAck()`** — helper phản hồi nhanh: nếu có `mid` thật (không bắt đầu bằng `"ts:"`) thì thả reaction ❤️ qua `fbReactMessage()`, thất bại thì fallback gửi text `"⚡ Đã nhận lệnh, đang xử lý..."`; không có `mid` thì gửi text luôn.<br>**`handleGroupCommand()`** — dispatch theo prefix: `/q <câu hỏi>` gọi Groq, `/ai <câu hỏi>` gọi Gemini (chặn trùng lệnh qua cờ `g_aiPending`, báo lỗi cú pháp nếu thiếu prompt); có nhánh `#if AUTO_REPLY` tự trả lời mọi tin bằng Gemini khi bật cờ trong `config.h`; tin không khớp lệnh nào → bỏ qua và log ra Serial.luu y  , Lưu ý: Tính năng này đang phát triển, hoạt động cực kỳ không ổn định nếu MQTT liên tục bị ngắt kết nối (disconnect). |

### 7️⃣ Entry point — `main.ino`

**`setup()`**
1. Khởi tạo Serial, random seed
2. Load NVS — nếu trống thì dùng `config.h`
3. Kết nối WiFi (Static IP `192.168.1.11` hoặc DHCP)
4. Sync time từ HTTP
5. Parse cookie → xác định `g_uid`
6. Verify cookie → nếu chết thì `doLogin()`, vẫn chết → Setup Portal
7. Kết nối WebSocket
8. Gửi MQTT `CONNECT`

**`loop()`**
- Auto reboot sau `AUTO_REBOOT_PERIOD_MS` (chỉ khi không busy)
- Ưu tiên xử lý AI pending
- Reconnect WS mỗi 5s khi mất, kiểm tra cookie sau 6 lần fail
- Poll frame WS → đẩy vào MQTT parser
- MQTT `PINGREQ` mỗi 9s, WS `PING` mỗi 25s

---

## 🆕 Tính năng bổ sung — GraphQL Listener

### 📁 Vị trí

```text
src/net/
├── ws_client.cpp/.h                 (cũ, giữ nguyên)
├── mqtt.cpp/.h                      (cũ, giữ nguyên)
└── fb_graphql_listen.cpp/.h         ← MỚI
```

### 🎯 Mục đích

Bổ sung phương thức nhận tin nhắn thay thế cho MQTT/WS — sử dụng **GraphQL polling** qua endpoint `/api/graphqlbatch/`.
Tuy nhiên, phương pháp này có độ trễ nhận tin nhắn khá cao, không bằng MQTT nhưng khá ổn định!
Cơ sở: Port từ file Python `__messageListenGraphQL.py` — dùng cùng `doc_id 3336396659757871`, cùng `query_params` (`limit`, `before:null`, `tags:["INBOX"]`, `includeDeliveryReceipts:false`, `includeSeqID:true`).

### 🧠 Nguyên lý hoạt động

```text
   ┌─────────────┐
   │   ESP32     │
   └──────┬──────┘
          │ POST /api/graphqlbatch/
          │ body: fb_dtsg, jazoest, __user, __req, __rev, queries
          ▼
   ┌─────────────────────┐
   │  Facebook GraphQL    │
   └──────┬───────────────┘
          │ JSON: {"o0":{"data":{"viewer":{"message_threads":
          │        {"nodes":[ {thread_key, last_message{...}}, ... ]}}}}
          ▼
   ┌─────────────────────┐
   │  String Parser        │ ← trích thread_fbid, timestamp, snippet…
   │  (không ArduinoJson)  │
   └──────┬────────────────┘
          │ so sánh với TARGET_THREAD_ID + baseline timestamp
          ▼
   ┌─────────────────────┐
   │  Callback              │ → in ra Serial (test) hoặc xử lý lệnh
   └─────────────────────┘
```

### ⚙️ Đặc điểm kỹ thuật

| Khía cạnh | Chi tiết |
|---|---|
| **Cơ chế** | Polling định kỳ (không realtime như MQTT/WS) |
| **Parser** | Thuần string (tránh chi phí RAM của ArduinoJson trên payload lớn) |
| **Trạng thái** | So sánh `timestamp` mới nhất với baseline đã lưu để tránh xử lý trùng |
| **Vai trò** | Dự phòng khi kênh MQTT/WS bị Facebook chặn hoặc ngắt bất thường |

---

## 🆕 Cập nhật — Tầng `commands/` (thêm sau 14/09/2026)

Tách toàn bộ logic xử lý lệnh nhóm ra khỏi tầng dispatch mạng, gom vào module riêng `src/commands/`.

| File | Nội dung |
|---|---|
| `commands.h` | Interface duy nhất `handleGroupCommand(...)` để các tầng `net/` (MQTT, GraphQL Listener) gọi vào khi có tin nhắn mới, không cần biết chi tiết xử lý bên trong. |
| `commands.cpp` | Cài đặt `sendAck()` (ack bằng reaction ❤️, fallback text) và `handleGroupCommand()` (dispatch `/q`, `/ai`, auto-reply Gemini). |

**Lợi ích:** tách rời tầng nhận dữ liệu (net) khỏi tầng xử lý nghiệp vụ (commands) → dễ mở rộng thêm lệnh mới mà không đụng vào `mqtt.cpp` hay `fb_graphql_listen.cpp`.
