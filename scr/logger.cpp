#include "logger.h"
#include <Arduino.h>
#include "app_state.h"

void logRAM(const char* tag) {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t maxBlk   = ESP.getMaxAllocHeap();
  uint8_t  frag     = 0;
  if (freeHeap > 0) {
    frag = (uint8_t)(100 - ((uint64_t)maxBlk * 100ULL / freeHeap));
  }
  Serial.printf("💾 [RAM] %-22s | free=%u | minFree=%u | maxBlk=%u | frag=%u%%",
                tag,
                (unsigned)freeHeap,
                (unsigned)ESP.getMinFreeHeap(),
                (unsigned)maxBlk,
                (unsigned)frag);
  Serial.println();
}

void logRAMFull(const char* tag) {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t maxBlk   = ESP.getMaxAllocHeap();
  uint8_t  frag     = 0;
  if (freeHeap > 0) {
    frag = (uint8_t)(100 - ((uint64_t)maxBlk * 100ULL / freeHeap));
  }
  Serial.println("╔═══════════════════ RAM REPORT ═══════════════════");
  Serial.printf ("║ Tag          : %s\n", tag);
  Serial.printf ("║ Heap free    : %u bytes\n", (unsigned)freeHeap);
  Serial.printf ("║ Heap total   : %u bytes\n", (unsigned)ESP.getHeapSize());
  Serial.printf ("║ Heap minFree : %u bytes\n", (unsigned)ESP.getMinFreeHeap());
  Serial.printf ("║ Max alloc blk: %u bytes\n", (unsigned)maxBlk);
  Serial.printf ("║ Fragmentation: %u %%\n", (unsigned)frag);
  Serial.printf ("║ Sketch size  : %u bytes\n", (unsigned)ESP.getSketchSize());
  Serial.printf ("║ Free sketch  : %u bytes\n", (unsigned)ESP.getFreeSketchSpace());
  Serial.printf ("║ CPU freq     : %u MHz\n", (unsigned)getCpuFrequencyMhz());
  Serial.printf ("║ Uptime       : %lu s\n", millis() / 1000);
  Serial.printf ("║ Msg received : %u\n", g_msgReceived);
  Serial.printf ("║ Msg sent     : %u\n", g_msgSent);
  Serial.printf ("║ WS reconnect : %u\n", g_wsReconnects);
  Serial.println("╚══════════════════════════════════════════════════");
}