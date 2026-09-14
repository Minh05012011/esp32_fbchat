#pragma once
#include <Arduino.h>

// ============================================================
//  Xử lý lệnh nhận từ group (qua GraphQL dispatch)
//    threadId  : thread_fbid của nhóm
//    actorId   : FB id người gửi
//    body      : nội dung tin nhắn (snippet)
//    mid       : message_id thật (nếu có) — hiện tại luôn "" vì
//                GraphQL không trả message_id, để dành cho tương lai
//    timestamp : timestamp_precise (ms)
// ============================================================
void handleGroupCommand(const String& threadId,
                        const String& actorId,
                        const String& body,
                        const String& mid,
                        long long timestamp);