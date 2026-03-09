#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "devices/devices.h"
#include <gsKit.h>
#include <limits.h>
#include <ps2sdkapi.h>

// Supported video mode types
typedef enum {
  VMode_NONE = 0,
  VMode_NTSC = GS_MODE_NTSC,
  VMode_PAL = GS_MODE_PAL,
  VMode_480p = GS_MODE_DTV_480P,
  VMode_720p = GS_MODE_DTV_720P,
} VModeType;

// Launcher configuration. Use getters/setters instead of accessing fields directly.
typedef struct {
  VModeType vmode;
  DeviceType enabledDevices; // Bit mask of Device_* flags
  char ipAddr[16];
  int probeDelay;
  char rootPath[PATH_MAX + 1];
  char neutrinoPath[PATH_MAX + 1]; // Can be set via nhddl.cnf (-neutrino=<path>)
  // Forwarder mode-exclusive flags:
  char *image;  // Used along with the device argument to turn NHDDL into a simple Neutrino forwarder
  int noInit;   // If set, will skip module initializaton and rely on already loaded modules
  int fakeDEV9; // If set, will enable DEV9 faking
} Config;

// Getters
VModeType getVMode(void);
DeviceType getEnabledDevices(void);
const char *getIPAddress(void);
int getProbeDelayWithDefaults(void);
int getProbeDelay(void);
const Device *getBootDevice(void);
const char *getNHDDLRoot(void);
// Raw stored root (for internal use). When root is HDD, getNHDDLRoot() returns the I/O path (pfs?:/path) instead.
const char *getNHDDLRawRoot(void);
const char *getNeutrinoPath(void);
const char *getImage(void);
int getNoInit(void);
int getFakeDEV9(void);

// Setters
void setVMode(VModeType v);
void setEnabledDevices(DeviceType v);
void setIPAddress(const char *v);
void setProbeDelay(int v);
void setBootDevice(const Device *v);
void setNHDDLRoot(const char *v);
void setNeutrinoPath(const char *v);
void setImage(const char *v);
void setNoInit(int v);
void setFakeDEV9(int v);

#endif
