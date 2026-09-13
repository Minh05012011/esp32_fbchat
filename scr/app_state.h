#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>

// --- Cookie / token ---
extern String g_uid;
extern String g_cookies;
extern String g_fbDtsg;
extern String g_jazoest;
extern String g_rev;

// --- Sync state ---
extern String g_syncSequenceId;
extern String g_syncToken;
extern String g_lastSeqId;
// --- Auto reboot ---
extern unsigned long g_lastRebootMs;
// --- WS / MQTT ---
extern WiFiClientSecure wsClient;
extern String    wsRxBuf;
extern String    mqttRxBuffer;
extern bool      mqttConnected;
extern uint16_t  nextPacketId;
extern unsigned long lastPing;
extern unsigned long lastReconnectMs;
extern unsigned long lastRamLogMs;
extern int retryCount;
extern int g_consecutiveConnFails;
// --- Counters ---
extern uint32_t g_msgReceived;
extern uint32_t g_msgSent;
extern uint32_t g_wsReconnects;
// --- AI pending ---
extern bool   g_aiPending;
extern String g_aiPrompt;
extern String g_aiThreadId;
extern String g_aiReplyToMsgId;
extern String g_aiService;   // "gemini" hoặc "groq"
// --- Serial buffer ---
extern String g_serialBuf;