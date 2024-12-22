#ifndef _COMMON_H_
#define _COMMON_H_

#include <ps2sdkapi.h>
#include <gsKit.h>

// Enum for supported modes
typedef enum {
  MODE_NONE = 0,
  MODE_ATA = (1 << 0),
  MODE_MX4SIO = (1 << 1),
  MODE_UDPBD = (1 << 2),
  MODE_USB = (1 << 3),
  MODE_ILINK = (1 << 4),
  MODE_ALL = MODE_ATA | MODE_MX4SIO | MODE_UDPBD | MODE_USB | MODE_ILINK,
} ModeType;

// Supported video mode types
typedef enum {
  VMODE_NONE = 0,
  VMODE_NTSC = GS_MODE_NTSC,
  VMODE_PAL = GS_MODE_PAL,
  VMODE_480P = GS_MODE_DTV_480P,
} VModeType;

// Launcher options
typedef struct {
  VModeType vmode;
  ModeType mode;
  char udpbdIp[16];
} LauncherOptions;

// Path to Neutrino ELF. Initialized in main() during init.
extern char NEUTRINO_ELF_PATH[PATH_MAX + 1];
// Options
extern LauncherOptions LAUNCHER_OPTIONS;

// Logs to debug screen and debug console
void logString(const char *str, ...);
// Maps ModeType to string
char *modeToString(ModeType mode);

#endif
