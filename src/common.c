#include "common.h"
#include <debug.h>
#include <stdio.h>

// Logs to debug screen and debug console
void logString(const char *str, ...) {
  va_list args;
  va_start(args, str);

  vprintf(str, args);
  scr_vprintf(str, args);

  va_end(args);
}

// Maps ModeType to string
char *modeToString(ModeType mode) {
  switch (mode) {
  case MODE_ATA:
    return "ATA";
  case MODE_MX4SIO:
    return "MX4SIO";
  case MODE_UDPBD:
    return "UDPBD";
  case MODE_USB:
    return "USB";
  case MODE_ILINK:
    return "iLink";
  default:
    return "Unknown";
  }
}
