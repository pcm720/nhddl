#include "config/common.h"
#include <libcdvd.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static const char BASE_CONFIG_PATH[] = "/nhddl";

void joinDevicePath(char *out, size_t outSize, const char *base, const char *rel) {
  if (!out || outSize == 0)
    return;
  out[0] = '\0';
  if (!base || !rel)
    return;

  int isHost = !strncmp(base, "host", 4);
  // With dsnet, host0:/ is an absolute path, host0:foo is relative to CWD
  if (isHost && rel[0] == '/')
    rel++;

  size_t blen = strlen(base);
  int baseEndsWithSlash = blen > 0 && base[blen - 1] == '/';
  int baseEndsWithColon = blen > 0 && base[blen - 1] == ':';
  int needsSlash = 0;
  if (rel[0] != '\0' && !baseEndsWithSlash && rel[0] != '/') {
    if (baseEndsWithColon)
      needsSlash = !isHost;
    else
      needsSlash = 1;
  }
  if (needsSlash)
    snprintf(out, outSize, "%s/%s", base, rel);
  else
    snprintf(out, outSize, "%s%s", base, rel);
}

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
void buildConfigFilePath(char *targetPath, const char *targetMountpoint, const char *targetFileName) {
  if (!targetPath || !targetMountpoint) {
    if (targetPath)
      targetPath[0] = '\0';
    return;
  }
  if (targetFileName == NULL) {
    joinDevicePath(targetPath, (size_t)PATH_MAX, targetMountpoint, BASE_CONFIG_PATH);
    return;
  }
  char suffix[PATH_MAX];
  if (targetFileName[0] != '/')
    snprintf(suffix, sizeof(suffix), "%s/%s", BASE_CONFIG_PATH, targetFileName);
  else
    snprintf(suffix, sizeof(suffix), "%s%s", BASE_CONFIG_PATH, targetFileName);
  joinDevicePath(targetPath, (size_t)PATH_MAX, targetMountpoint, suffix);
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
