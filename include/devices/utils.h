#ifndef _DEVICES_UTILS_H_
#define _DEVICES_UTILS_H_

#include "devices/devices.h"

// Attempts to guess device type from path
DeviceType guessDeviceType(const char *path);

// Gets BDM device type via fileXio
DeviceType getBDMDeviceType(char *mountpoint);

// Gets BDM device number via fileXio
int getBDMDeviceNumber(char *mountpoint);

#endif
