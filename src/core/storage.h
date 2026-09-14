#pragma once
#include <Arduino.h>

struct FBConfig {
  String cookie;
  String dtsg;
  String jazoest;
  String rev;
};

bool storageLoad(FBConfig& out);
void storageSave(const FBConfig& in);
void storageClear();
void storagePrintInfo();