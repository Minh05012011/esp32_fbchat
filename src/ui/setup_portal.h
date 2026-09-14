#pragma once
#include <Arduino.h>

// Bật WebServer trên WiFi hiện tại, chờ POST /save.
// Khi nhận cookie → lưu NVS → reboot.
// BLOCKING — không bao giờ return bình thường.
void runSetupPortal();