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

// Probes and builds canonical path for any given path and device type.
// Expects the path to be a file path. Caller must free the returned string after use.
char *probeCanonicalPath(const char *path, DeviceType type);

// Converts massN: path into canonical path by resolving the underlying driver and device number via fileXio.
// Returns NULL and sets *type to Device_None on failure. Caller must free the returned string after use.
char *guessCWDDevice(const char *cwd, DeviceType *type);

#endif
