# esp32_fbchat

Hệ thống giao tiếp Facebook Chat trên vi điều khiển ESP32 (đang trong quá trình phát triển).

> **Trạng thái hiện tại:** Đã có thể lắng nghe và gửi tin nhắn cơ bản vào thread.

---

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
## Changelog — 13/09/2026

1. **Thêm reaction emoji cho tin nhắn** — sửa cmd serial để test
2. **Tích hợp Gemini AI** (`/ai <câu hỏi>`)
3. **Fix lỗi server đóng ngẫu nhiên (code 1000)** — random client ID mỗi lần reconnect
4. **Tích hợp Groq API** (`/q <câu hỏi>`)
5. **Auto reboot** định kỳ — chỉ khi rảnh, không cắt ngang lệnh
6. **Auto new cookie** — Setup Portal qua WebServer khi cookie hết hạn
