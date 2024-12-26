#ifndef _DEVICES_H_
#define _DEVICES_H_

#include "common.h"

#define MAX_DEVICES 12

// Device map entry
typedef struct {
  char *mountpoint;
  ModeType mode;
  int index;
} DeviceMapEntry;

// Maps BDM device index (massX:) to supported mode.
// Device must be ignored if mode is MODE_ALL or MODE_NONE
extern DeviceMapEntry deviceModeMap[];

// Initializes device mode map and returns device count
int initDeviceMap();

#endif