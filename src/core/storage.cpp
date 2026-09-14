#include "src/core/storage.h"
#include <Preferences.h>

static const char* NVS_NS = "fbcfg";

bool storageLoad(FBConfig& out) {
  Preferences p;
  if (!p.begin(NVS_NS, /*readonly=*/true)) {
    Serial.println("📦 [NVS] Không mở được namespace");
    return false;
  }
  out.cookie  = p.getString("cookie",  "");
  out.dtsg    = p.getString("dtsg",    "");
  out.jazoest = p.getString("jazoest", "");
  out.rev     = p.getString("rev",     "");
  p.end();

  return (out.cookie.length() >= 20);
}

void storageSave(const FBConfig& in) {
  Preferences p;
  if (!p.begin(NVS_NS, /*readonly=*/false)) {
    Serial.println("📦 [NVS] Không ghi được namespace");
    return;
  }
  p.putString("cookie",  in.cookie);
  p.putString("dtsg",    in.dtsg);
  p.putString("jazoest", in.jazoest);
  p.putString("rev",     in.rev);
  p.end();
  Serial.println("💾 [NVS] Đã lưu config");
}

void storageClear() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.clear();
  p.end();
  Serial.println("🗑️ [NVS] Đã xóa config");
}

void storagePrintInfo() {
  FBConfig cfg;
  bool ok = storageLoad(cfg);
  Serial.println("╔══════════ NVS FB CONFIG ══════════");
  if (!ok) {
    Serial.println("║ (trống)");
  } else {
    Serial.printf("║ Cookie len : %d\n", cfg.cookie.length());
    Serial.printf("║ Cookie pre : %.60s...\n", cfg.cookie.c_str());
    Serial.printf("║ dtsg       : %.30s...\n", cfg.dtsg.c_str());
    Serial.printf("║ jazoest    : %s\n", cfg.jazoest.c_str());
    Serial.printf("║ rev        : %s\n", cfg.rev.c_str());
  }
  Serial.println("╚═══════════════════════════════════");
}