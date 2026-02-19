#include "common.h"
#include <debug.h>
#include <fcntl.h>
#include <stdio.h>

// Logs to debug screen and debug console
void logString(const char *str, ...) {
  va_list args;
  va_start(args, str);

  vprintf(str, args);
  scr_vprintf(str, args);

  va_end(args);
}

// Maps DeviceType to string
char *modeToString(DeviceType mode) {
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
  case MODE_MMCE:
    return "MMCE";
  case MODE_HDL:
    return "HDL";
  default:
    return "Unknown";
  }
}

// Returns the start index of relative file path without device mountpoint or -1 if path is not supported/invalid
int getRelativePathIdx(char *path) {
  int idx = 0;
  while (path[0] != 0) {
    if (path[0] == ':' && (path[1] == '/' || path[1] == '\\')) {
      return idx + 1;
    }
    idx++;
    path++;
  }
  return -1;
}

// Returns device number index in path or -1 if path doesn't contain a device number
int getDeviceNumberIdx(char *path) {
  int idx = 0;
  while ((*path != 0) && (*path != ':')) {
    if (*path >= '0' && *path <= '9') {
      return idx;
    }
    idx++;
    path++;
  }
  return -1;
}

// Tests if file exists by opening it
int tryFile(char *filepath) {
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    return fd;
  }
  close(fd);
  return 0;
}
