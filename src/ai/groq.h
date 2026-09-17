#pragma once
#include <Arduino.h>
#include "src/fb_api/fb_quick_reply.h" 
// Gọi Groq API (OpenAI-compatible), trả lời vào outReply.
// Trả về true nếu thành công (HTTP 200 + có content).
bool groqAsk(const String& prompt, String& outReply);
// Tách block [QR]...[/QR] khỏi reply của Groq.
// Return:
//   -1 = KHÔNG có marker [QR] trong reply
//    0 = CÓ marker nhưng parse fail (0 nút hợp lệ)
//   >0 = số nút parse được (đã ghi vào outBtns)
// outText = phần text đã cắt marker (đã trim).
//   Khi return -1 hoặc 0: outText = reply nguyên gốc.
// maxOut  = kích thước mảng outBtns (khuyến nghị MAX_QR_FROM_AI).
int groqExtractQr(const String& reply, String& outText,
                  QuickReplyBtn* outBtns, int maxOut);
// In cấu hình Groq ra Serial
void groqPrintInfo();