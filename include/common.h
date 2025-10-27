#ifndef _COMMON_H_
#define _COMMON_H_

#include <gsKit.h>
#include <ps2sdkapi.h>

// Enum for supported modes
typedef enum {
  MODE_NONE = 0,
  MODE_ATA = (1 << 0),
  MODE_MX4SIO = (1 << 1),
  MODE_UDPBD = (1 << 2),
  MODE_USB = (1 << 3),
  MODE_ILINK = (1 << 4),
  MODE_MMCE = (1 << 5),
  MODE_HDL = (1 << 6),
  MODE_BDM = MODE_ATA | MODE_MX4SIO | MODE_UDPBD | MODE_USB | MODE_ILINK | MODE_HDL, // Internal mode, used to avoid loading BDM modules
  MODE_ALL = MODE_ATA | MODE_MX4SIO | MODE_UDPBD | MODE_USB | MODE_ILINK | MODE_MMCE | MODE_HDL,
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
  char *image; // Used along with the mode argument to turn NHDDL into a simple Neutrino forwarder
} LauncherOptions;

// Path to Neutrino ELF. Initialized in main() during init.
extern char NEUTRINO_ELF_PATH[PATH_MAX + 1];
// Options
extern LauncherOptions LAUNCHER_OPTIONS;

// Logs to debug screen and debug console
void logString(const char *str, ...);
// Maps ModeType to string
char *modeToString(ModeType mode);
// Returns the start index of relative file path without device mountpoint or -1 if path is not supported/invalid
int getRelativePathIdx(char *path);
// Returns device number index in path or -1 if path doesn't contain a device number
int getDeviceNumberIdx(char *path);

#endif
