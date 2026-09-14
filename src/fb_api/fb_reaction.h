#pragma once
#include <Arduino.h>

// Thả / gỡ reaction vào 1 tin nhắn cụ thể
//   messageId      : messageID của tin nhắn cần reaction
//   emoji          : emoji (VD: "👍", "❤️", "😆") - UTF-8
//   removeReaction : false = ADD_REACTION (mặc định)
//                    true  = REMOVE_REACTION (gỡ)
// Trả về true nếu HTTP 200 (giống Python chỉ check raise_for_status).
bool fbReactMessage(const String& messageId,
                    const String& emoji,
                    bool removeReaction = false);