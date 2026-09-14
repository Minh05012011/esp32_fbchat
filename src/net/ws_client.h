#pragma once
#include <Arduino.h>

bool wsConnect(const String& sid);
bool wsSendFrame(uint8_t opcode, const uint8_t* data, size_t len);
uint8_t wsPoll(String& outPayload);   // trả về opcode (0 = chưa có gì)