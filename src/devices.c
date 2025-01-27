#include "devices.h"
#include "common.h"
#include "gui.h"
#include <errno.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usbhdfsd-common.h>

// Used to get BDM driver name and make devctl calls
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// Function used to initialize device map entry.
// Must initialize DeviceMapEntries in deviceModeMap and return number of found devices or negative error number.
// newDeviceIdx is the first free index in deviceModeMap array
typedef int (*backendInitFunc)(int newDeviceIdx);

typedef struct {
  char *name;
  ModeType targetModes;
  backendInitFunc initFunction;
} SupportedBackends;

int initBDMDevices();
int initMMCEDevices();
int initHDL();
void delay(int count);

// List of modules to load
static SupportedBackends backends[] = {
    {.name = "MMCE", .initFunction = initMMCEDevices, .targetModes = MODE_MMCE},
    {.name = "BDM", .initFunction = initBDMDevices, .targetModes = MODE_ATA | MODE_MX4SIO | MODE_UDPBD | MODE_USB | MODE_ILINK},
    {.name = "HDL", .initFunction = initHDL, .targetModes = MODE_HDL},
};

// Contains all available devices.
// Device must be ignored if mode is MODE_ALL or MODE_NONE
struct DeviceMapEntry deviceModeMap[MAX_DEVICES] = {};

// Initializes device mode map and returns device count
int initDeviceMap() {
  int deviceCount = 0;
  int res = 0;
  for (int i = 0; i < sizeof(backends) / sizeof(SupportedBackends); i++) {
    if (!(backends[i].targetModes & LAUNCHER_OPTIONS.mode)) {
      // Skip initializing unneeded backends
      continue;
    }

    uiSplashLogString(LEVEL_INFO_NODELAY, "Initializing %s backend\n", backends[i].name);
    if ((res = backends[i].initFunction(deviceCount)) < 0) {
      printf("ERROR: Failed to initialize %s backend: %d\n", backends[i].name, res);
      continue;
    }
    deviceCount += res;
  }
  return deviceCount;
}

//
// The following functions are based on code by AKuHAK
//

void delay(int count) {
  int ret;
  for (int i = 0; i < count; i++) {
    ret = 0x01000000;
    while (ret--)
      asm("nop\nnop\nnop\nnop");
  }
}
