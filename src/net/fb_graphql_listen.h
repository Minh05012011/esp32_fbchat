#pragma once
#include <Arduino.h>

// ============================================================
//  GraphQL Polling Listener — thay MQTT/WS để nhận tin
//  Port từ __messageListenGraphQL.py
// ============================================================

typedef void (*FbMessageCallback)(
    const String& threadId,
    const String& actorId,
    const String& body,
    const String& messageId,
    long long timestamp
);

void fbGraphQLSetCallback(FbMessageCallback cb);
void fbGraphQLSetLimit(int limit);

bool fbGraphQLFetchInbox(String& outRaw);   // debug
bool fbGraphQLPollOnce();                   // 1 lần poll
void fbGraphQLListen(unsigned long pollMs); // blocking loop

void fbGraphQLResetBaseline();
unsigned long fbGraphQLGetPollCount();
unsigned long fbGraphQLGetNewMsgCount();
long long     fbGraphQLGetBaseline();