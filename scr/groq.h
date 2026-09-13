#pragma once
#include <Arduino.h>

// Gọi Groq API (OpenAI-compatible), trả lời vào outReply.
// Trả về true nếu thành công (HTTP 200 + có content).
bool groqAsk(const String& prompt, String& outReply);

// In cấu hình Groq ra Serial
void groqPrintInfo();