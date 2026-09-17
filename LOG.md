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
esp32_fbchat/
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
---

## 🆕 Changelog — 17/09/2026

### 1️⃣ README.md viết lại theo hướng "giới thiệu chung"

Do project đang trong giai đoạn nâng cấp liên tục, `README.md` được viết lại để **chỉ giới thiệu mục đích/tính năng tổng quan**, không còn liệt kê cấu trúc thư mục hay vị trí file cụ thể (tránh bị lỗi thời). Phần chi tiết kỹ thuật/cấu trúc/cập nhật tiếp tục được ghi nhận trong file này (`LOG.md`).

### 2️⃣ `src/net/mqtt.h` / `mqtt.cpp` — Subscribe `/thread_typing`

- Thêm hàm `sendMqttSubscribe(topic, qos)` — gửi gói MQTT `SUBSCRIBE` thủ công cho 1 topic bất kỳ (trước đây chỉ có `/t_ms` được auto-subscribe qua field `"st"` trong `CONNECT`, hàm `mqttSubscribePacket()` build sẵn nhưng chưa từng được gọi).
- Gọi `sendMqttSubscribe("/thread_typing", 0)` ngay sau khi `CONNACK` thành công (`rc=0`), song song với `/t_ms`.
- Thêm nhánh xử lý riêng trong `handleMqttPublish()` cho topic `/thread_typing` — parse đúng schema thật (theo test suite của thư viện `fbchat`):
```json
  { "sender_fbid": 1234, "state": 0, "type": "typ", "thread": "4321" }
```
  `state`: 1 = đang gõ, 0 = đã dừng gõ. Log ra: `[type] user=<sender_fbid> thread=<thread> -> ĐANG GÕ/ĐÃ DỪNG`, chỉ in nếu `thread` khớp `TARGET_THREAD_ID` (hoặc `TARGET_THREAD_ID` rỗng).

### 3️⃣ Fix lỗi WS/MQTT tự rớt liên tục (nguyên nhân đã xác định — không còn cần GraphQL polling để né lỗi này)

Nguyên nhân: gói `MQTT CONNECT` (trong `mqtt.cpp`) khai báo **Keep Alive = 10 giây**, nhưng bản test trước đó chỉ gửi `PINGREQ` mỗi 45 giây (gấp 4.5 lần ngưỡng 1.5×keepalive = 15s theo spec MQTT) → Facebook broker chủ động đóng kết nối (`Close code=1000 reason='Bye'`), không phải lỗi mạng hay lỗi parser.

**2 fix áp dụng trong `main.ino`:**
- `PING_INTERVAL_MS`: `45000` → `8000` (an toàn dưới ngưỡng 15s), sau đó chỉnh tiếp xuống `4000` (mục 6) để có thêm biên an toàn.
- Trong `connectWsAndMqtt()`: reset `g_syncToken = ""` mỗi lần reconnect — vì `clientId` random lại mỗi lần connect nên session MQTT/queue cũ trên Facebook đã mất; nếu không reset, `sendCreateQueue()` sẽ gọi `get_diffs` bằng `sync_token` cũ → luôn dính `ERROR_QUEUE_NOT_FOUND` ngay sau reconnect.

### 4️⃣ `main.ino` — chuyển từ GraphQL polling sang WS + MQTT (test build)

Viết lại `setup()`/`loop()` để dùng `ws_client.h` + `mqtt.h` thay cho `fb_graphql_listen.h`, **giữ nguyên 100%** phần NVS (`storageLoad`/`FBConfig`), Setup Portal (`runSetupPortal`), verify cookie / login fallback, static IP, auto-reboot, serial command. Thêm:
- `connectWsAndMqtt()` — mở WS tới `edge-chat.facebook.com:443`, gửi `MQTT CONNECT` (bên trong tự động subscribe `/t_ms` + `/thread_typing`), áp cả 2 fix disconnect ở mục 3.
- Auto-reconnect WS trong `loop()` nếu mất kết nối (retry mỗi 3s).
- `sendMqttPing()` gửi `PINGREQ` thủ công theo `PING_INTERVAL_MS`.

### 5️⃣ Tính năng mới — Quick Reply (nút bấm trong Messenger)

Mục tiêu dài hạn: dùng Quick Reply để làm nút bật/tắt thiết bị (vd. bóng đèn qua relay GPIO) — **phần điều khiển GPIO thực tế chưa làm, để dành thêm sau**. Hiện tại mới dừng ở tầng gửi/test nút.

| File | Vai trò |
|---|---|
| `src/fb_api/fb_quick_reply.h/.cpp` (MỚI) | `sendQuickReplyMessage(threadId, body, buttons[], count)` — POST `/messaging/send/` kèm field `platform_xmd` chứa JSON `{"quick_replies":[{"content_type":"text","title":..,"payload":..}]}` (build bằng ArduinoJson). Cấu trúc form dựa theo `sendGroupMessage()` sẵn có, port từ `__sendQuickReply.py`. Chỉ hỗ trợ nút loại `"text"` (loại duy nhất ổn định trên cả Web lẫn Mobile). |
| `src/commands/qr_serial.h/.cpp` (MỚI) | `handleQrSerialCommand(threadId, spec, contentSend)` — parse spec dạng `"Title1:Payload1;Title2:Payload2"` (giống `parse_qr_spec()` bên Python: không có `:` thì `payload = title`), rồi gọi `sendQuickReplyMessage()`. |
| `src/ui/serial_cmd.cpp` | Thêm lệnh `/qr <title>:<payload>;...` — VD: `/qr hello:hello`, `/qr Bật đèn:ON;Tắt đèn:OFF`. Cập nhật `printSerialHelp()`. |
| `src/core/config.h` | Thêm sẵn `LIGHT_RELAY_PIN` (default GPIO2) và `LIGHT_RELAY_ACTIVE_LOW` — **để dành cho tính năng bật/tắt đèn qua GPIO, chưa được dùng ở đâu trong code**. |

**Lưu ý khi test:** nhận diện nút nào được bấm hiện tại phải dựa vào **title** của nút (vì khi user bấm, FB gửi lại 1 tin nhắn thường có `body = title`, không có field `payload` trong luồng nhận tin nhắn qua `/t_ms` hiện tại) — payload trong request gửi đi vẫn được set đúng theo Facebook API, nhưng tầng nhận (`handleMqttPublish`) chưa parse lại được payload này.

### 6️⃣ Tính năng mới — AI (Groq) tự tạo Quick Reply qua marker `[QR]...[/QR]`

Mở rộng thêm cho tính năng Quick Reply ở mục 5: giờ đây **Groq có thể tự quyết định chèn nút bấm** vào câu trả lời khi user yêu cầu (VD: "cho tôi vài lựa chọn", "tạo menu"), không cần gõ `/qr` thủ công nữa.

| File | Thay đổi |
|---|---|
| `src/core/config.h` | Thêm `MAX_QR_FROM_AI` (6) và `MAX_QR_TITLE_LEN` (20 — giới hạn độ dài title theo FB). `GROQ_SYSTEM_PROMPT` được viết lại chi tiết: chỉ chèn `[QR]Title1:Title1;Title2:Title2[/QR]` khi user yêu cầu rõ ràng bằng từ khóa ("lựa chọn nhanh", "tạo nút", "menu", ...), tối đa 5 nút, `payload` luôn = `title` (đảm bảo so khớp ổn định), block QR đặt ở đầu câu trả lời rồi xuống dòng viết text giải thích. |
| `src/ai/groq.h/.cpp` | Thêm `groqExtractQr(reply, outText, outBtns, maxOut)` — tách block `[QR]...[/QR]` ra khỏi reply thô của Groq, parse spec `Title:Payload;...` (stricter hơn `qr_serial`: bỏ qua nút thiếu `:` hoặc title rỗng, tự truncate title quá `MAX_QR_TITLE_LEN`, ép `payload = title`). Trả về `-1` nếu reply không có marker, `0` nếu có marker nhưng parse fail, `n>0` nếu có `n` nút hợp lệ. |
| `src/ai/gemini.cpp` (`handlePendingAI`) | Khi `service == "groq"`: gọi `groqExtractQr()` trên reply. Nếu có nút hợp lệ → gửi 1 tin duy nhất kèm nút qua `sendQuickReplyMessage()` (fallback gửi text thường nếu gửi QR thất bại). Nếu có marker `[QR]` nhưng parse fail → **tự động retry gọi Groq thêm 1 lần** trước khi bỏ cuộc và báo lỗi. Nếu không có marker → gửi text bình thường như cũ. Nhánh Gemini không đổi (luôn gửi text). |
| `main.ino` | `PING_INTERVAL_MS` chỉnh từ `8000` xuống **`4000`** (dự phòng thêm biên an toàn so với ngưỡng 15s của keepalive=10s). |

**Lưu ý:** tính năng này phụ thuộc Groq trả đúng format `[QR]...[/QR]` theo prompt đã định — nếu Groq "quên" đóng `[/QR]` hoặc trả sai cú pháp, code đã có 1 lớp retry nhưng vẫn có thể rơi vào trường hợp báo lỗi `"❌ Có lỗi khi tạo lựa chọn, thử lại sau nhé!"` nếu retry cũng fail.Ngoài ra, tính năng trả lời nhanh (Quick Reply/button) hiện chỉ hoạt động trên phiên bản Messenger Web.
