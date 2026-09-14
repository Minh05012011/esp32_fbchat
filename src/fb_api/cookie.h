#pragma once
#include <Arduino.h>

// Trích 1 field trong cookie (hỗ trợ "; key=", ";key=", "key=" đầu chuỗi)
String extractCookieValue(const String& cookie, const String& key);

// Parse MANUAL_COOKIE → g_cookies, g_uid + verify
bool parseCookieAndFill(const String& rawCookie);

// Trích session_cookies từ JSON response login
String extractCookiesFromLogin(const String& body);