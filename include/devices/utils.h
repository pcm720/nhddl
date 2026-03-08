#ifndef _DEVICES_UTILS_H_
#define _DEVICES_UTILS_H_

#include "devices/devices.h"

// Attempts to guess device type from path
DeviceType guessDeviceType(const char *path);

// Uses MMCE devctl calls to switch memory card to given title ID
void mmceMountVMC(char *titleID);

#endif
