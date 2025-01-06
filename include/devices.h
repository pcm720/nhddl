#ifndef _DEVICES_H_
#define _DEVICES_H_

#include "common.h"

#define MAX_DEVICES 12

// Device map entry
typedef struct {
  char *mountpoint;  // Device mountpoint
  ModeType mode;     // Device driver
  uint8_t index;     // BDM internal device driver number, must be used for passing paths to Neutrino
  uint8_t doNotScan; // If not zero, device must be ignored during ISO scanning
} DeviceMapEntry;

// Contains all available devices.
// Device must be ignored if mode is MODE_ALL or MODE_NONE
extern DeviceMapEntry deviceModeMap[];

// Initializes device mode map and returns device count
int initDeviceMap();

// Uses MMCE devctl calls to switch memory card to given title ID
void mmceMountVMC(char *titleID);

#endif
