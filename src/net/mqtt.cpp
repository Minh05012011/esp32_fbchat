#include "src/net/mqtt.h"
#include "src/core/app_state.h"
#include "src/core/logger.h"
#include "src/core/utils.h"
#include "src/net/ws_client.h"
#include "src/fb_api/fb_auth.h"
#include "src/fb_api/fb_send.h"
#include "src/core/config.h"
#include "src/ai/gemini.h"
#include "src/commands/commands.h"
#include <ArduinoJson.h>

// ============================================================
//  MQTT packet builders
// ============================================================
static void encodeRemLen(String& out, uint32_t len) {
  do {
    uint8_t b = len % 128; len /= 128;
    if (len > 0) b |= 0x80;
    out += (char)b;
  } while (len > 0);
}

static String mqttConnectPacket(const String& clientId, const String& username) {
  String vh;
  vh += (char)0x00; vh += (char)0x06; vh += "MQIsdp";
  vh += (char)0x03; vh += (char)0x82;
  vh += (char)0x00; vh += (char)0x0A;
  vh += (char)((clientId.length() >> 8) & 0xFF);
  vh += (char)(clientId.length() & 0xFF);
  vh += clientId;
  vh += (char)((username.length() >> 8) & 0xFF);
  vh += (char)(username.length() & 0xFF);
  vh += username;
  String pkt; pkt += (char)0x10;
  encodeRemLen(pkt, vh.length()); pkt += vh;
  return pkt;
}

static String mqttPublishPacket(const String& topic, const String& payload, uint16_t pid) {
  String vh;
  vh += (char)((topic.length() >> 8) & 0xFF);
  vh += (char)(topic.length() & 0xFF);
  vh += topic;
  vh += (char)((pid >> 8) & 0xFF);
  vh += (char)(pid & 0xFF);
  vh += payload;
  String pkt; pkt += (char)0x32;
  encodeRemLen(pkt, vh.length()); pkt += vh;
  return pkt;
}

static String mqttPubAck(uint16_t pid) {
  String p; p += (char)0x40; p += (char)0x02;
  p += (char)((pid >> 8) & 0xFF); p += (char)(pid & 0xFF);
  return p;
}

// SUBSCRIBE + QoS 1 (giống paho client.subscribe("/t_ms", 1))
static String mqttSubscribePacket(uint16_t pid, const String& topic, uint8_t qos) {
  String vh;
  vh += (char)((pid >> 8) & 0xFF);
  vh += (char)(pid & 0xFF);
  vh += (char)((topic.length() >> 8) & 0xFF);
  vh += (char)(topic.length() & 0xFF);
  vh += topic;
  vh += (char)(qos & 0x03);
  String pkt; pkt += (char)0x82;   // SUBSCRIBE (0x80 | 0x02 QoS1)
  encodeRemLen(pkt, vh.length()); pkt += vh;
  return pkt;
}

// ============================================================
//  SUBSCRIBE (dùng để test 1 topic bất kỳ, vd "/thread_typing")
// ============================================================
void sendMqttSubscribe(const String& topic, uint8_t qos) {
  if (!wsClient.connected()) {
    Serial.println("[MQTT] ⚠️ Chưa có WS, không thể SUBSCRIBE");
    return;
  }
  uint16_t pid = nextPacketId++;
  String pkt = mqttSubscribePacket(pid, topic, qos);
  wsSendFrame(0x2, (const uint8_t*)pkt.c_str(), pkt.length());
  Serial.printf("[MQTT] -> SUBSCRIBE topic=%s qos=%u pid=%u\n",
                topic.c_str(), qos, pid);
}

// ============================================================
//  Outgoing MQTT
// ============================================================
void sendMqttConnect() {
  String user = "{";
  user += "\"u\":\"" + g_uid + "\",";
  user += "\"s\":" + genSessionId() + ",";
  user += "\"chat_on\":true,";
  user += "\"fg\":false,";
  user += "\"d\":\"" + genClientId() + "\",";
  user += "\"ct\":\"websocket\",";
  user += "\"aid\":219994525426954,";
  user += "\"mqtt_sid\":\"\",";
  user += "\"cp\":3,\"ecp\":10,";
  user += "\"st\":\"/t_ms\",";
  user += "\"pm\":[],\"dc\":\"\",";
  user += "\"no_auto_fg\":true,";
  user += "\"gas\":null,";
  user += "\"pack\":[]}";

  // ✅ Client ID ngẫu nhiên mỗi lần connect → tránh xung đột phiên
  //    (thay cho "mqttwsclient" cố định)
  char clientId[24];
  snprintf(clientId, sizeof(clientId), "esp_%08x%08x",
           (unsigned)esp_random(), (unsigned)esp_random());

  String pkt = mqttConnectPacket(clientId, user);
  wsSendFrame(0x2, (const uint8_t*)pkt.c_str(), pkt.length());
  Serial.printf("[MQTT] -> CONNECT (clientId=%s, %u bytes)\n",
                clientId, pkt.length());
  logRAM("sau MQTT CONNECT sent");
}

void sendCreateQueue() {
  String q;
  if (g_syncToken.length() == 0) {
    q  = "{\"sync_api_version\":10,\"max_deltas_able_to_process\":1000,";
    q += "\"delta_batch_size\":500,\"encoding\":\"JSON\",";
    q += "\"entity_fbid\":\"" + g_uid + "\",";
    q += "\"orca_version\":\"1.2.0\",";
    q += "\"initial_titan_sequence_id\":\"" + g_lastSeqId + "\",";
    q += "\"device_params\":null}";
    String pkt = mqttPublishPacket("/messenger_sync_create_queue", q, nextPacketId++);
    wsSendFrame(0x2, (const uint8_t*)pkt.c_str(), pkt.length());
    Serial.printf("[MQTT] -> create_queue (seq=%s)\n", g_lastSeqId.c_str());
  } else {
    q  = "{\"sync_api_version\":10,\"max_deltas_able_to_process\":1000,";
    q += "\"delta_batch_size\":500,\"encoding\":\"JSON\",";
    q += "\"entity_fbid\":\"" + g_uid + "\",";
    q += "\"orca_version\":\"1.2.0\",";
    q += "\"last_seq_id\":\"" + g_lastSeqId + "\",";
    q += "\"sync_token\":\"" + g_syncToken + "\"}";
    String pkt = mqttPublishPacket("/messenger_sync_get_diffs", q, nextPacketId++);
    wsSendFrame(0x2, (const uint8_t*)pkt.c_str(), pkt.length());
    Serial.println("[MQTT] -> get_diffs");
  }
}

// ============================================================
//  Queue error
// ============================================================
void handleQueueError(const String& ecStr) {
  Serial.printf("⚠️ Queue error: %s\n", ecStr.c_str());
  logRAM("queue error");
  retryCount++;
  if (retryCount > 5) {
    Serial.println("❌ Retry > 5 → WS reconnect");
    wsClient.stop();
    return;
  }
  String newSeq = verifyCookie();
  if (newSeq.length() > 0) {
    g_lastSeqId = newSeq;
  } else {
    if (ecStr == "ERROR_QUEUE_UNDERFLOW") g_lastSeqId = "0";
  }
  g_syncToken = "";
  mqttConnected = false;
  int backoff = 2000 * retryCount;
  if (backoff > 15000) backoff = 15000;
  Serial.printf("   backoff=%dms | seq=%s\n", backoff, g_lastSeqId.c_str());
  delay(backoff);
  if (!wsClient.connected()) return;
  sendCreateQueue();
}

// ============================================================
//  Incoming PUB
// ============================================================
void handleMqttPublish(const String& topic, const String& body) {
  Serial.printf("\n📨 [PUB] %s (%d bytes)\n", topic.c_str(), body.length());
  logRAM("trc parse PUB");

  // ==========================================================
  //  🧪 TEST: /thread_typing — payload nhỏ, KHÔNG theo format
  //  "deltas" như /t_ms nên xử lý riêng, tách khỏi logic bên dưới.
  //  Schema thật (theo test suite của fbchat):
  //  { "sender_fbid": 1234, "state": 0/1, "type": "typ", "thread": "4321" }
  // ==========================================================
  if (topic == "/thread_typing") {
    Serial.printf("   ⌨️  RAW: %s\n", body.c_str());

#if ARDUINOJSON_VERSION_MAJOR >= 7
    JsonDocument typDoc;
#else
    DynamicJsonDocument typDoc(1024);
#endif
    DeserializationError typErr = deserializeJson(typDoc, body);
    if (!typErr) {
      String senderFbid = typDoc["sender_fbid"].as<String>(); // ID lớn -> lấy dạng chuỗi
      int    state      = typDoc["state"]  | -1;              // 1=đang gõ, 0=dừng gõ
      String evType     = typDoc["type"]   | "";
      String threadId   = typDoc["thread"] | "";

      bool isTyping = (state == 1);

      if (String(TARGET_THREAD_ID).length() > 0 &&
          threadId != String(TARGET_THREAD_ID)) {
        // Sự kiện không thuộc nhóm đang theo dõi -> bỏ qua
      } else {
        Serial.printf("   ⌨️  [%s] user=%s thread=%s -> %s\n",
                       evType.c_str(), senderFbid.c_str(), threadId.c_str(),
                       isTyping ? "ĐANG GÕ" : "ĐÃ DỪNG");
      }
    } else {
      Serial.printf("   ⚠️ Không parse được JSON typing: %s\n", typErr.c_str());
    }
    return; // không đi tiếp vào logic parse "/t_ms" bên dưới
  }

  if (body.length() > 60000) {
    Serial.println("   ⚠️ Body quá lớn, bỏ qua");
    return;
  }

  size_t docSize = body.length() * 4 + 4096;

#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(docSize);
  if (doc.capacity() == 0) {
    Serial.printf("   ❌ Alloc fail (%u bytes)\n", (unsigned)docSize);
    logRAM("alloc fail");
    return;
  }
#endif

  DeserializationError err = deserializeJson(doc, body,
    DeserializationOption::NestingLimit(20));
  if (err) {
    Serial.printf("   ❌ JSON err: %s (code=%d)\n", err.c_str(), (int)err.code());
    logRAM("json fail");
    return;
  }

  // --- Sync metadata ---
  if (doc.containsKey("syncToken"))
    g_syncToken = doc["syncToken"].as<String>();
  if (doc.containsKey("firstDeltaSeqId"))
    g_lastSeqId = doc["firstDeltaSeqId"].as<String>();
  if (doc.containsKey("lastIssuedSeqId"))
    g_lastSeqId = doc["lastIssuedSeqId"].as<String>();

  // --- Xử lý từng delta ---
  bool matched = false;
  for (JsonObject d : doc["deltas"].as<JsonArray>()) {
    JsonObject meta = d["messageMetadata"];
    if (meta.isNull()) continue;

    JsonObject tk = meta["threadKey"];
    String threadFb = tk["threadFbId"] | "";

    if (threadFb.length() == 0) continue;
    if (String(TARGET_THREAD_ID).length() > 0 &&
        threadFb != String(TARGET_THREAD_ID)) continue;

    String actor = meta["actorFbId"] | "";
    if (actor == g_uid) {
      Serial.println("   ⏭️  Tin của bot");
      continue;
    }

    matched = true;

    const char* text  = d["body"] | "";
    const char* msgId = meta["messageId"] | "";
    long long   ts    = meta["timestamp"] | 0;

    Serial.println("   ╔════════════════════════════════════");
    Serial.printf ("   ║ 📌 GROUP : %s\n", threadFb.c_str());
    Serial.printf ("   ║ 👤 FROM  : %s\n", actor.c_str());
    Serial.printf ("   ║ 💬 BODY  : %s\n", text);
    Serial.printf ("   ║ 🆔 MSG ID: %s\n", msgId);
    Serial.printf ("   ║ 🕒 TS    : %lld\n", ts);
    Serial.println("   ╚════════════════════════════════════");

    g_msgReceived++;

    // ==========================================================
    //  LỆNH /ai — Đóng WS, xử lý ở loop() (handlePendingAI)
    // ==========================================================
    String bodyStr = String(text);
    bodyStr.trim();
          // ==========================================================
    //  LỆNH /q  —  Groq (giống /ai nhưng service = groq)
    // ==========================================================
    if (bodyStr == "/q" || bodyStr.startsWith("/q ")) {
      if (g_aiPending) {
        Serial.println("   ⏳ Đang xử lý câu trước, bỏ qua");
        sendGroupMessage(threadFb, "⏳ Đang xử lý câu trước, đợi chút nhé!");
        continue;
      }

      String prompt = (bodyStr.length() > 2) ? bodyStr.substring(3) : "";
      prompt.trim();

      if (prompt.length() == 0) {
        Serial.println("   ❓ /q thiếu prompt → reply cú pháp");
        sendGroupMessage(threadFb, "❓ Cú pháp: /q <câu hỏi>");
        continue;
      }

Serial.println("   📤 sendAck() — thả tim hoặc gửi text...");
sendAck(threadFb, String(msgId));

      Serial.println("   ⚡ [/q] Lưu context (service=groq)");

      g_aiPending      = true;
      g_aiService      = "groq";        // ← khác /ai
      g_aiPrompt       = prompt;
      g_aiThreadId     = threadFb;
      g_aiReplyToMsgId = String(msgId);

      Serial.printf("   💾 RAM trước khi gọi Groq: free=%u\n", ESP.getFreeHeap());
      logRAM("sau khi set /q pending");

      return;
    }
        if (bodyStr == "/ai" || bodyStr.startsWith("/ai ")) {
      // Chống spam: nếu đang xử lý câu trước → từ chối
      if (g_aiPending) {
        Serial.println("   ⏳ Đang xử lý câu trước, bỏ qua");
        sendGroupMessage(threadFb, "⏳ Đang xử lý câu trước, đợi chút nhé!");
        continue;
      }

      String prompt = (bodyStr.length() > 3) ? bodyStr.substring(4) : "";
      prompt.trim();

      if (prompt.length() == 0) {
        Serial.println("   ❓ /ai thiếu prompt → reply cú pháp");
        sendGroupMessage(threadFb, "❓ Cú pháp: /ai <câu hỏi>");
        continue;
      }

      // Ack qua HTTP (WS vẫn đang mở — không ảnh hưởng)
Serial.println("   📤 sendAck() — thả tim hoặc gửi text...");
sendAck(threadFb, String(msgId));

      // Set pending — KHÔNG đóng WS
      Serial.println("   🧠 [/ai] Lưu context (WS giữ nguyên)");

      g_aiPending      = true;
      g_aiService      = "gemini";      // ← THÊM DÒNG NÀY
      g_aiPrompt       = prompt;
      g_aiThreadId     = threadFb;
      g_aiReplyToMsgId = String(msgId);

      // KHÔNG gọi wsClient.stop() nữa
      // KHÔNG clear mqttRxBuffer

      Serial.printf("   💾 RAM trước khi gọi Gemini: free=%u\n", ESP.getFreeHeap());
      logRAM("sau khi set /ai pending");

      return;
    }

    // ==========================================================
    //  AUTO_REPLY — trả lời mọi tin nhắn bằng Gemini
    //  ⚠️ Nhánh này gọi Gemini khi WS vẫn mở → 2 TLS session chồng
    //  → RAM căng. Chỉ bật khi chấp nhận rủi ro OOM.
    //  Cách an toàn: dùng /ai (giải phóng WS trước).
    // ==========================================================
#if AUTO_REPLY
    String userMsg = String(text);
    if (userMsg.length() < 2) {
      Serial.println("   ⏭️  Tin quá ngắn, bỏ qua");
    } else {
      if (userMsg.length() > 300) userMsg = userMsg.substring(0, 300);

      Serial.println("   🤖 Đang gọi Gemini...");
      String reply;
      if (geminiAsk(userMsg, reply)) {
        Serial.printf("   ↩️  Reply: %s\n", reply.c_str());
        sendGroupMessage(threadFb, reply);
      } else {
        Serial.println("   ⚠️ Gemini fail, thử câu dự phòng");
        sendGroupMessage(threadFb, getRandomMessage());
      }
    }
#endif
  }

  if (matched) {
    logRAM("sau handle msg");
  }

  // --- Queue error ---
  if (doc.containsKey("errorCode")) {
    String ecStr = "";
    int ecInt = 0;
    if (doc["errorCode"].is<const char*>()) {
      ecStr = doc["errorCode"].as<String>();
    } else {
      ecInt = doc["errorCode"].as<int>();
    }
    Serial.printf("⚠️ errorCode str='%s' int=%d\n", ecStr.c_str(), ecInt);
    if (ecStr.length() > 0) {
      handleQueueError(ecStr);
    } else if (ecInt != 0) {
      handleQueueError(ecInt == 100 ? "ERROR_QUEUE_OVERFLOW" : "UNKNOWN");
    }
  }
}

// ============================================================
//  MQTT buffer parser
// ============================================================
void processMqttBuffer() {
  while (mqttRxBuffer.length() >= 2) {
    uint8_t type = (uint8_t)mqttRxBuffer[0];
    uint8_t typeHi = type >> 4;

    // ---- Decode Remaining Length ----
    uint32_t remLen = 0, mult = 1;
    int pos = 1; uint8_t b;
    do {
      if (pos >= (int)mqttRxBuffer.length()) return;
      b = (uint8_t)mqttRxBuffer[pos++];
      remLen += (b & 0x7F) * mult;
      mult *= 128;
      if (mult > 128UL*128*128) { mqttRxBuffer = ""; return; }
    } while (b & 0x80);

    if ((int)mqttRxBuffer.length() < pos + (int)remLen) return;

    String payload = mqttRxBuffer.substring(pos, pos + remLen);
    mqttRxBuffer.remove(0, pos + remLen);

    switch (typeHi) {
      // ==========================================================
      //  CONNACK (type 2)
      //  Payload: [session_present (1B)] [return_code (1B)]
      // ==========================================================
      case 2: {
        uint8_t sp = 0;
        uint8_t rc = 0xFF;
        if (payload.length() >= 2) {
          sp = (uint8_t)payload[0];
          rc = (uint8_t)payload[1];
        }
        Serial.printf("✅ MQTT CONNACK | rc=%u | session_present=%u\n", rc, sp);

        if (rc != 0) {
          Serial.printf("   ❌ CONNACK rejected (rc=%u) → đóng WS để reconnect sạch\n", rc);
          mqttConnected = false;

          // rc=4 (bad user/pass) hoặc rc=5 (not authorized)
          // → gần như chắc chắn cookie chết → đẩy nhanh counter
          if (rc == 4 || rc == 5) {
            g_consecutiveConnFails += 3;
            Serial.printf("   ⚠️ rc=%u → nghi cookie chết (failCount=%d)\n",
                          rc, g_consecutiveConnFails);
          } else {
            g_consecutiveConnFails++;
          }

          wsClient.stop();
          break;
        }

        mqttConnected = true;
        retryCount = 0;
        g_consecutiveConnFails = 0;
        logRAM("sau CONNACK");

        // ⚠️ /t_ms đã được tự động subscribe qua field "st" trong CONNECT
        // payload, không cần SUBSCRIBE riêng cho topic này.
        Serial.println("[MQTT] bỏ qua SUBSCRIBE /t_ms (đã auto qua 'st')");

        // 🧪 TEST: subscribe thêm /thread_typing để xem sự kiện "đang gõ"
        // QoS 0 là đủ, vì đây chỉ là thông báo tạm thời, không cần ACK.
        sendMqttSubscribe("/thread_typing", 0);

        // Delay nhẹ như Python (Python fetch seq_id qua HTTP mất ~1s)
        delay(300);

        sendCreateQueue();
        break;
      }

      // ==========================================================
      //  PUBLISH (type 3)
      // ==========================================================
      case 3: {
        if (payload.length() < 2) break;
        int qos = (type >> 1) & 0x03;
        int tLen = ((uint8_t)payload[0] << 8) | (uint8_t)payload[1];
        if ((int)payload.length() < 2 + tLen) break;
        String topic = payload.substring(2, 2 + tLen);
        int idx = 2 + tLen;
        uint16_t pid = 0;
        if (qos > 0) {
          if ((int)payload.length() < idx + 2) break;
          pid = ((uint8_t)payload[idx] << 8) | (uint8_t)payload[idx+1];
          idx += 2;
        }
        handleMqttPublish(topic, payload.substring(idx));
        if (qos == 1) {
          String ack = mqttPubAck(pid);
          wsSendFrame(0x2, (const uint8_t*)ack.c_str(), ack.length());
        }
        break;
      }

      // ==========================================================
      //  PUBACK (type 4)
      // ==========================================================
      case 4:
        // không cần xử lý, chỉ log nhẹ nếu muốn
        break;

      // ==========================================================
      //  SUBACK (type 9)  — payload: [pid(2B)] [granted_qos(1B)]
      // ==========================================================
      case 9: {
        if (payload.length() >= 3) {
          uint8_t granted = (uint8_t)payload[2];
          if (granted == 0x80) {
            Serial.println("[MQTT] SUBACK | ❌ FAILED (0x80)");
          } else {
            Serial.printf("[MQTT] SUBACK | granted_qos=%u\n", granted);
          }
        } else {
          Serial.println("[MQTT] SUBACK (short payload)");
        }
        break;
      }

      // ==========================================================
      //  PINGRESP (type 13)
      // ==========================================================
      case 13:
        // PINGRESP — không cần làm gì
        break;

      default:
        Serial.printf("[MQTT] type=%d len=%u\n", typeHi, remLen);
    }
  }
}