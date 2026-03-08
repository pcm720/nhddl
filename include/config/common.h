#ifndef _CONFIG_COMMON_H_
#define _CONFIG_COMMON_H_

#include <stdint.h>

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
void buildConfigFilePath(char *targetPath, const char *targetMountpoint, const char *targetFileName);

// Generates 32-bit timestamp from RTC. Will wrap around every 64th year
uint32_t getTimestamp(void);

#endif
