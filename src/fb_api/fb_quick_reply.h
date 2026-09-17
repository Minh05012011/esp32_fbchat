#pragma once
#include <Arduino.h>

// Một nút Quick Reply dạng text (loại duy nhất chạy ổn định mọi nền tảng)
struct QuickReplyBtn {
  String title;    // Chữ hiển thị trên nút
  String payload;  // Giá trị ẩn — FB không trả lại payload qua tin nhắn
                    // thường (chỉ có trong messenger_platform data), nên
                    // để nhận diện nút nào được bấm, ta so khớp theo TITLE
                    // (vì khi bấm, FB gửi lại 1 tin nhắn thường có body = title).
};

// Gửi tin nhắn kèm Quick Reply (nút bấm dạng text) vào 1 thread.
// buttons/count: danh sách nút. Trả về true nếu FB xác nhận đã gửi.
bool sendQuickReplyMessage(const String& threadId,
                            const String& body,
                            const QuickReplyBtn* buttons,
                            int count);