#ifndef _DEVICES_H_
#define _DEVICES_H_

#include "common.h"
#include "target.h"

#define MAX_DEVICES 20

// Must scan the device entry for titles and add them to TargetList
typedef int (*titleScanFunc)(TargetList *result, struct DeviceMapEntry *device);
// Must sync the device
typedef void (*syncFunc)();

// Device map entry
struct DeviceMapEntry {
  char *mountpoint;               // Device mountpoint
  syncFunc sync;                  // Must sync the device
  titleScanFunc scan;             // Function used for scanning the device entry for titles. Can be NULL if device must be ignored during scanning. Might not be present in deviceModeMap
  struct DeviceMapEntry *metadev; // If set, cover art and options will be loaded from metadata device instead of this device. Set during initialization
  ModeType mode;                  // Device driver
  uint8_t index;                  // BDM internal device driver number, must be used for passing paths to Neutrino
};

// Contains all available devices.
// Device must be ignored if mode is MODE_ALL or MODE_NONE
extern struct DeviceMapEntry deviceModeMap[];

// Initializes device mode map and returns device count
int initDeviceMap();

// Delays for
void delay(int count);

// Uses MMCE devctl calls to switch memory card to given title ID
void mmceMountVMC(char *titleID);

//
// Device-specific scanning functions
//

// Scans given storage device for ISO files and appends valid launch candidates to TargetList
// Returns 0 if successful, non-zero if no targets were found or an error occurs
// Implemented in devices_iso.c
int findISO(TargetList *list, struct DeviceMapEntry *device);

// Scans given APA HDD for HDL partitions and appends valid launch candidates to TargetList
// Returns 0 if successful, non-zero if no targets were found or an error occurs
// Implemented in devices_hdl.c
int findHDLTargets(TargetList *result, struct DeviceMapEntry *device);

#endif
