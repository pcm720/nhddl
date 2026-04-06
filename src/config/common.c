#include "config/common.h"
#include <libcdvd.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static const char BASE_CONFIG_PATH[] = "/nhddl";

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
void buildConfigFilePath(char *targetPath, const char *targetMountpoint, const char *targetFileName) {
  if (!targetPath || !targetMountpoint) {
    if (targetPath)
      targetPath[0] = '\0';
    return;
  }
  if (targetFileName == NULL) {
    snprintf(targetPath, (size_t)PATH_MAX, "%s%s", targetMountpoint, BASE_CONFIG_PATH);
    return;
  }
  if (targetFileName[0] != '/')
    snprintf(targetPath, (size_t)PATH_MAX, "%s%s/%s", targetMountpoint, BASE_CONFIG_PATH, targetFileName);
  else
    snprintf(targetPath, (size_t)PATH_MAX, "%s%s%s", targetMountpoint, BASE_CONFIG_PATH, targetFileName);
}

// Generates 32-bit timestamp from RTC. Will wrap around every 64th year
uint32_t getTimestamp(void) {
  if (sceCdInit(SCECdINoD)) {
    sceCdCLOCK time;
    sceCdReadClock(&time);
    sceCdInit(SCECdEXIT);

    uint32_t sum = ((uint32_t)btoi(time.year)) << 26 |
                   ((uint32_t)btoi(time.month) & 0xF) << 22 |
                   ((uint32_t)btoi(time.day)) << 17 |
                   ((uint32_t)btoi(time.hour)) << 12 |
                   ((uint32_t)btoi(time.minute)) << 6 |
                   (btoi(time.second) & 0x3F);
    return sum;
  }
  return 0;
}
