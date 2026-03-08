#ifndef _DEVICES_UTILS_H_
#define _DEVICES_UTILS_H_

#include "devices/devices.h"

// Attempts to guess device type from path
DeviceType guessDeviceType(const char *path);

// Maps DeviceType to string
char *getDeviceString(DeviceType mode);

// Returns the device index or -1 if path is not supported/invalid
int getDeviceIndex(char *path);

// Returns the start index of relative file path without device mountpoint or -1 if path is not supported/invalid
int getRelativePathIdx(char *path);

// Uses MMCE devctl calls to switch memory card to given title ID
void mmceMountVMC(char *titleID);

#endif
