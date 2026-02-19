#ifndef _COMMON_H_
#define _COMMON_H_

#include <gsKit.h>
#include <ps2sdkapi.h>

// Supported video mode types
typedef enum {
  VMode_NONE = 0,
  VMode_NTSC = GS_MODE_NTSC,
  VMode_PAL = GS_MODE_PAL,
  VMode_480p = GS_MODE_DTV_480P,
} VModeType;

// Launcher options
typedef struct {
  VModeType vmode;
  DeviceType mode;
  char udpbdIp[16];
  char *image; // Used along with the mode argument to turn NHDDL into a simple Neutrino forwarder
  int noInit;
} LauncherOptions;

// Path to Neutrino ELF. Initialized in main() during init.
extern char NEUTRINO_ELF_PATH[PATH_MAX + 1];
// Options
extern LauncherOptions LAUNCHER_OPTIONS;

// Logs to debug screen and debug console
void logString(const char *str, ...);
// Maps DeviceType to string
char *modeToString(DeviceType mode);
// Returns the start index of relative file path without device mountpoint or -1 if path is not supported/invalid
int getRelativePathIdx(char *path);
// Returns device number index in path or -1 if path doesn't contain a device number
int getDeviceNumberIdx(char *path);
// Tests if file exists by opening it
int tryFile(char *filepath);

#endif
