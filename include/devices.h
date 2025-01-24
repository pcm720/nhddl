#ifndef _DEVICES_H_
#define _DEVICES_H_

#include "common.h"
#include "target.h"

#define MAX_DEVICES 12

// Must scan the device entry for titles and add them to TargetList
typedef int (*titleScanFunc)(TargetList *result, struct DeviceMapEntry *device);

// Device map entry
struct DeviceMapEntry {
  char *mountpoint;   // Device mountpoint
  ModeType mode;      // Device driver
  uint8_t index;      // BDM internal device driver number, must be used for passing paths to Neutrino
  titleScanFunc scan; // Function used for scanning the device entry for titles. Can be NULL if device must be ignored during scanning
};

// Contains all available devices.
// Device must be ignored if mode is MODE_ALL or MODE_NONE
extern struct DeviceMapEntry deviceModeMap[];

// Initializes device mode map and returns device count
int initDeviceMap();

// Uses MMCE devctl calls to switch memory card to given title ID
void mmceMountVMC(char *titleID);

#endif
