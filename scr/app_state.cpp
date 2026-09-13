#include "app_state.h"

String g_uid;
String g_cookies;
String g_fbDtsg;
String g_jazoest;
String g_rev;
// --- Auto reboot ---
unsigned long g_lastRebootMs = 0;
String g_syncSequenceId = "0";
String g_syncToken      = "";
String g_lastSeqId      = "0";
String g_aiService = "gemini";   // default
WiFiClientSecure wsClient;
String    wsRxBuf;
String    mqttRxBuffer;
bool      mqttConnected = false;
uint16_t  nextPacketId  = 1;
unsigned long lastPing = 0;
unsigned long lastReconnectMs = 0;
unsigned long lastRamLogMs = 0;
int retryCount = 0;
int g_consecutiveConnFails = 0;
uint32_t g_msgReceived = 0;
uint32_t g_msgSent = 0;
uint32_t g_wsReconnects = 0;
// --- AI pending ---
bool   g_aiPending = false;
String g_aiPrompt;
String g_aiThreadId;
String g_aiReplyToMsgId;
String g_serialBuf;