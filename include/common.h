#ifndef _COMMON_H_
#define _COMMON_H_

#include <ps2sdkapi.h>

// Storage device base path. Initialized in main.c
extern const char STORAGE_BASE_PATH[];
// ELF base path. Initialized in main() during init.
extern char ELF_BASE_PATH[PATH_MAX + 1];

// Logs to screen and debug console
void logString(const char *str, ...);

#endif