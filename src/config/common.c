#include "config/title.h"
#include <libcdvd.h>
#include <stdint.h>

// Generates 32-bit timestamp from RTC.
// Will wrap around every 64th year
uint32_t getTimestamp(void) {
  // Initialize libcdvd to get timestamp
  if (sceCdInit(SCECdINoD)) {
    // Read clock
    sceCdCLOCK time;
    sceCdReadClock(&time);
    sceCdInit(SCECdEXIT);

    // Pack date into 32-bit timestamp
    // Y   26 M 22 D  17 H  12 M    6 S    0
    // 111111 1111 11111 11111 111111 111111
    uint32_t sum = ((uint32_t)btoi(time.year)) << 26 |        // Year
                   ((uint32_t)btoi(time.month) & 0xF) << 22 | // Month
                   ((uint32_t)btoi(time.day)) << 17 |         // Day
                   ((uint32_t)btoi(time.hour)) << 12 |        // Hour
                   ((uint32_t)btoi(time.minute)) << 6 |       // Minute
                   (btoi(time.second) & 0x3F);                // Second
    return sum;
  }
  return 0;
}
