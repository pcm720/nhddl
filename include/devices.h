#ifndef _DEVICES_H_
#define _DEVICES_H_

#include "common.h"

#define MAX_MASS_DEVICES 10

#define MASS_PLACEHOLDER "massX:"

// Device map entry
typedef struct {
  ModeType mode;
  int index;
} DeviceMapEntry;

// Maps BDM device index (massX:) to supported mode.
// Device must be ignored if mode is MODE_ALL
extern DeviceMapEntry deviceModeMap[];

// Initializes device mode map
int initDeviceMap();

#endif