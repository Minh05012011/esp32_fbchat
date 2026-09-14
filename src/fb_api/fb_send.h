#pragma once
#include <Arduino.h>

// Gửi 1 tin nhắn text vào group threadId
bool sendGroupMessage(const String& threadId, const String& body);