#pragma once
#include <Arduino.h>
#include <time.h>

String urlEncode(const String& s);
String jsonGetStr(const String& src, const String& key);
time_t parseHttpDate(const String& s);
String genThreadingId();
String getRandomMessage();
String genClientId();
String genSessionId();