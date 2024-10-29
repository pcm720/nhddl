#ifndef _COMMON_H_
#define _COMMON_H_

#include <ps2sdkapi.h>

// Enum for supported modes
typedef enum {
  MODE_ATA,
  MODE_MX4SIO,
  MODE_UDPBD,
  MODE_USB
} ModeType;

// Launcher options
typedef struct {
  int is480pEnabled;
  ModeType mode;
  char udpbdIp[16];
} LauncherOptions;

// Storage device base path. Initialized in main.c
extern const char STORAGE_BASE_PATH[];
extern const size_t STORAGE_BASE_PATH_LEN;
// ELF base path. Initialized in main() during init.
extern char ELF_BASE_PATH[PATH_MAX + 1];
// Path to Neutrino ELF. Initialized in main() during init.
extern char NEUTRINO_ELF_PATH[PATH_MAX+1];
// Options
extern LauncherOptions LAUNCHER_OPTIONS;

// Logs to screen and debug console
void logString(const char *str, ...);

#endif
