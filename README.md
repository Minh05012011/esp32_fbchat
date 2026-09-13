# esp32_fbchat
esp32_fbchat/
└── src/
    ├── main.ino              ← setup() + loop()
    ├── config.h              ← toàn bộ cấu hình (WiFi, tokens, target, UA, options)
    ├── app_state.h / .cpp    ← khai báo & định nghĩa biến toàn cục
    ├── logger.h   / .cpp     ← logRAM(), logRAMFull()
    ├── utils.h    / .cpp     ← urlEncode, parseHttpDate, gen*, jsonGetStr, MSG_POOL
    ├── cookie.h   / .cpp     ← extractCookieValue, parseCookieAndFill, extractCookiesFromLogin
    ├── fb_auth.h  / .cpp     ← syncTimeFromHTTP, verifyCookie, doLogin
    ├── fb_send.h  / .cpp     ← sendGroupMessage
    ├── ws_client.h/ .cpp     ← wsConnect, wsSendFrame, wsPoll
    ├── mqtt.h     / .cpp     ← MQTT builders, sendMqttConnect, sendCreateQueue, PUB handler
    └── serial_cmd.h/ .cpp    ← printSerialHelp, handleSerialCommand, pollSerialInput
da co the lang nghe + send tin nhan co ban vao thread , van dang ptrien
