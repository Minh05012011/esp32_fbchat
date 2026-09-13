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
