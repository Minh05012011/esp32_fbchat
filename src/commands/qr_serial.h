#pragma once
#include <Arduino.h>

// Parse spec dạng "Title1:Payload1;Title2:Payload2;..." (giống parse_qr_spec
// bên Python) rồi gửi vào threadId qua sendQuickReplyMessage().
// Nếu 1 item không có dấu ':' → payload = title (giống bản Python).
// contentSend: nội dung text hiển thị kèm nút (mặc định "Chọn một tùy chọn:"
// nếu truyền rỗng).
void handleQrSerialCommand(const String& threadId,
                            const String& spec,
                            const String& contentSend);