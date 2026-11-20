#ifndef _CONFIG_COMMON_H_
#define _CONFIG_COMMON_H_

#include <stddef.h>
#include <stdint.h>

// Join base (e.g. "host:", "mass0:", "pfs0:/") with rel ("theme/font.ttf" or "/nhddl").
// If base is host:, strip a leading '/' on rel so the result stays CWD-relative.
// Insert '/' between base and rel only when base does not already end in ':' or '/'.
void joinDevicePath(char *out, size_t outSize, const char *base, const char *rel);

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
void buildConfigFilePath(char *targetPath, const char *targetMountpoint, const char *targetFileName);

// Generates 32-bit timestamp from RTC. Will wrap around every 64th year
uint32_t getTimestamp(void);

#endif
