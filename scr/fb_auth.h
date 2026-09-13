#pragma once
#include <Arduino.h>

// Sync system time từ HTTP Date header
void syncTimeFromHTTP();

// Verify cookie bằng graphqlbatch → trả về sync_sequence_id (rỗng nếu fail)
String verifyCookie();

// Login dự phòng qua b-graph → cập nhật g_cookies, g_uid
bool doLogin();