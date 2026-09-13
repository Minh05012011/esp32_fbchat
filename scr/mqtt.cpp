#include "mqtt.h"
#include "app_state.h"
#include "logger.h"
#include "utils.h"
#include "ws_client.h"
#include "fb_auth.h"
#include "fb_send.h"
#include "config.h"

#include <ArduinoJson.h>

// ---------- Packet builders ----------
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

static String mqttPingReq() { String p; p += (char)0xC0; p += (char)0x00; return p; }

// ---------- Outgoing MQTT ----------
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
  String pkt = mqttConnectPacket("mqttwsclient", user);
  wsSendFrame(0x2, (const uint8_t*)pkt.c_str(), pkt.length());
  Serial.printf("[MQTT] -> CONNECT (%u bytes)\n", pkt.length());
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

// ---------- Queue error ----------
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

// ---------- Incoming PUB ----------
void handleMqttPublish(const String& topic, const String& body) {
  Serial.printf("\n📨 [PUB] %s (%d bytes)\n", topic.c_str(), body.length());
  logRAM("trc parse PUB");

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

  if (doc.containsKey("syncToken"))
    g_syncToken = doc["syncToken"].as<String>();
  if (doc.containsKey("firstDeltaSeqId"))
    g_lastSeqId = doc["firstDeltaSeqId"].as<String>();
  if (doc.containsKey("lastIssuedSeqId"))
    g_lastSeqId = doc["lastIssuedSeqId"].as<String>();

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

#if AUTO_REPLY
    String reply = getRandomMessage();
    Serial.printf("   ↩️  Auto-reply: %s\n", reply.c_str());
    sendGroupMessage(threadFb, reply);
#endif
  }

  if (matched) {
    logRAM("sau handle msg");
  }

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

void processMqttBuffer() {
  while (mqttRxBuffer.length() >= 2) {
    uint8_t type = (uint8_t)mqttRxBuffer[0];
    uint8_t typeHi = type >> 4;

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
      case 2:
        Serial.println("✅ MQTT CONNACK");
        mqttConnected = true;
        retryCount = 0;
        logRAM("sau CONNACK");
        sendCreateQueue();
        break;
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
      case 13:
        break;
      default:
        Serial.printf("[MQTT] type=%d len=%u\n", typeHi, remLen);
    }
  }
}