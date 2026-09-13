#include "ws_client.h"
#include "app_state.h"
#include "logger.h"
#include "config.h"

String makeWsKey() {
  uint8_t raw[16];
  for (int i = 0; i < 16; i++) raw[i] = esp_random() & 0xFF;
  static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  for (int i = 0; i < 16; i += 3) {
    uint32_t b = raw[i] << 16;
    if (i + 1 < 16) b |= raw[i + 1] << 8;
    if (i + 2 < 16) b |= raw[i + 2];
    out += tbl[(b >> 18) & 0x3F];
    out += tbl[(b >> 12) & 0x3F];
    out += (i + 1 < 16) ? tbl[(b >> 6) & 0x3F] : '=';
    out += (i + 2 < 16) ? tbl[b & 0x3F] : '=';
  }
  return out;
}

bool wsConnect(const String& sid) {
  Serial.println("[WS] connect edge-chat...");
  logRAM("trc WS connect");
  wsClient.setInsecure();
  wsClient.setTimeout(30);

  if (!wsClient.connect("edge-chat.facebook.com", 443)) {
    Serial.println("[WS] ❌ TCP fail");
    logRAM("WS TCP fail");
    return false;
  }
  Serial.println("[WS] ✅ TCP+TLS OK");
  delay(200);

  String key = makeWsKey();
  String req = "GET /chat?region=eag&sid=" + sid + " HTTP/1.1\r\n";
  req += "Host: edge-chat.facebook.com\r\n";
  req += "Upgrade: websocket\r\n";
  req += "Connection: Upgrade\r\n";
  req += "Sec-WebSocket-Key: " + key + "\r\n";
  req += "Sec-WebSocket-Version: 13\r\n";
  req += "Sec-WebSocket-Protocol: mqtt\r\n";
  req += "Cookie: " + g_cookies + "\r\n";
  req += "Origin: https://www.facebook.com\r\n";
  req += "User-Agent: " + String(WEB_UA) + "\r\n";
  req += "Referer: https://www.facebook.com/\r\n";
  req += "\r\n";

  wsClient.write((const uint8_t*)req.c_str(), req.length());
  wsClient.flush();

  String resp;
  resp.reserve(512);
  unsigned long t0 = millis();
  while (millis() - t0 < 15000) {
    if (!wsClient.connected() && !wsClient.available()) break;
    while (wsClient.available()) {
      resp += (char)wsClient.read();
      if (resp.endsWith("\r\n\r\n")) goto hsDone;
    }
    delay(10);
  }
  hsDone:
  if (resp.length() == 0) { wsClient.stop(); logRAM("WS no resp"); return false; }
  int fl = resp.indexOf("\r\n");
  Serial.print("[WS] Status: ");
  Serial.println(resp.substring(0, fl));

  if (resp.indexOf(" 101 ") < 0 && resp.indexOf(" 101\r") < 0) {
    Serial.println("[WS] ❌ Handshake FAIL");
    wsClient.stop();
    logRAM("WS handshake fail");
    return false;
  }
  Serial.println("[WS] ✅ 101 OK");
  wsRxBuf = "";
  logRAM("sau WS connect");
  return true;
}

bool wsSendFrame(uint8_t opcode, const uint8_t* data, size_t len) {
  if (!wsClient.connected()) return false;
  uint8_t hdr[14];
  int hdrLen = 2;
  hdr[0] = 0x80 | (opcode & 0x0F);
  if (len < 126) {
    hdr[1] = 0x80 | (uint8_t)len;
  } else if (len < 65536) {
    hdr[1] = 0x80 | 126;
    hdr[2] = (len >> 8) & 0xFF;
    hdr[3] = len & 0xFF;
    hdrLen = 4;
  } else {
    hdr[1] = 0x80 | 127;
    for (int i = 0; i < 8; i++) hdr[2 + i] = (len >> (56 - i*8)) & 0xFF;
    hdrLen = 10;
  }
  uint8_t mask[4] = {
    (uint8_t)(esp_random() & 0xFF), (uint8_t)(esp_random() & 0xFF),
    (uint8_t)(esp_random() & 0xFF), (uint8_t)(esp_random() & 0xFF)
  };
  memcpy(hdr + hdrLen, mask, 4);
  hdrLen += 4;
  wsClient.write(hdr, hdrLen);
  uint8_t buf[128];
  for (size_t i = 0; i < len; i += sizeof(buf)) {
    size_t n = min((size_t)sizeof(buf), len - i);
    for (size_t j = 0; j < n; j++) buf[j] = data[i+j] ^ mask[(i+j) & 3];
    wsClient.write(buf, n);
  }
  return true;
}

uint8_t wsPoll(String& outPayload) {
  int avail = wsClient.available();
  if (avail > 0) {
    uint8_t tmp[256];
    while (avail > 0) {
      int n = wsClient.read(tmp, min(avail, (int)sizeof(tmp)));
      if (n <= 0) break;
      for (int i = 0; i < n; i++) wsRxBuf += (char)tmp[i];
      avail -= n;
    }
  }
  if (wsRxBuf.length() < 2) return 0;
  uint8_t b0 = (uint8_t)wsRxBuf[0];
  uint8_t b1 = (uint8_t)wsRxBuf[1];
  uint8_t opcode = b0 & 0x0F;
  bool masked = b1 & 0x80;
  uint64_t plen = b1 & 0x7F;
  int pos = 2;
  if (plen == 126) {
    if (wsRxBuf.length() < 4) return 0;
    plen = ((uint8_t)wsRxBuf[2] << 8) | (uint8_t)wsRxBuf[3];
    pos = 4;
  } else if (plen == 127) {
    if (wsRxBuf.length() < 10) return 0;
    plen = 0;
    for (int i = 0; i < 8; i++) plen = (plen << 8) | (uint8_t)wsRxBuf[2 + i];
    pos = 10;
  }
  uint8_t mask[4] = {0,0,0,0};
  if (masked) {
    if (wsRxBuf.length() < pos + 4) return 0;
    memcpy(mask, wsRxBuf.c_str() + pos, 4);
    pos += 4;
  }
  if ((uint64_t)wsRxBuf.length() < pos + plen) return 0;

  outPayload = "";
  outPayload.reserve(plen + 1);
  for (size_t i = 0; i < plen; i++) {
    char c = wsRxBuf[pos + i];
    if (masked) c ^= mask[i & 3];
    outPayload += c;
  }
  wsRxBuf.remove(0, pos + plen);
  if (opcode == 0x9) { wsSendFrame(0xA, (const uint8_t*)outPayload.c_str(), outPayload.length()); return 0; }
  if (opcode == 0xA) return 0;
  if (opcode == 0x8) { wsClient.stop(); return 0x8; }
  return opcode;
}