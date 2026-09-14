#pragma once
#include <Arduino.h>

// Gửi câu hỏi tới Gemini, trả về câu trả lời.
//   prompt  : câu hỏi / tin nhắn người dùng
//   outReply: chuỗi trả lời (rỗng nếu fail)
// Trả về true nếu thành công.
bool geminiAsk(const String& prompt, String& outReply);
void handlePendingAI(); 
// In thông tin model ra Serial (debug)
void geminiPrintInfo();