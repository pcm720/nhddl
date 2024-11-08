#ifndef _DEVICES_H_
#define _DEVICES_H_

#include "common.h"

#define MAX_MASS_DEVICES 10

#define MASS_PLACEHOLDER "massX:"
#define MASS_PLACEHOLDER_LEN sizeof(MASS_PLACEHOLDER)/sizeof(char)

// Device map entry
typedef struct {
  ModeType mode;
  int index;
} DeviceMapEntry;

// Maps BDM device index (massX:) to supported mode.
// Device must be ignored if mode is MODE_ALL
extern DeviceMapEntry deviceModeMap[];

// Initializes device mode map and returns device count
int initDeviceMap();

#endif