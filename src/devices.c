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
int initHDLFS();
void delay(int count);

// List of modules to load
static SupportedBackends backends[] = {
    {.name = "MMCE", .initFunction = initMMCEDevices, .targetModes = MODE_ALL},
    {.name = "BDM", .initFunction = initBDMDevices, .targetModes = MODE_ATA | MODE_MX4SIO | MODE_UDPBD | MODE_USB | MODE_ILINK},
    {.name = "HDL", .initFunction = initHDLFS, .targetModes = MODE_HDL},
};
static char mmceMountpoint[] = "mmceX:";

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
      uiSplashLogString(LEVEL_ERROR, "Failed to initialize %s backend: %d\n", backends[i].name, res);
      continue;
    }
    deviceCount += res;
  }
  return deviceCount;
}

// Initializes map entries for MMCE devices
int initMMCEDevices(int newDeviceIdx) {
  DIR *directory;

  int deviceCount = 0;
  for (int i = 0; i < 2; i++) {
    mmceMountpoint[4] = i + '0';

    directory = opendir(mmceMountpoint);
    if (directory != NULL) {
      closedir(directory);
      uiSplashLogString(LEVEL_INFO_NODELAY, "Found device %s\n", mmceMountpoint);

      deviceModeMap[newDeviceIdx].mode = MODE_MMCE;
      deviceModeMap[newDeviceIdx].index = i;
      deviceModeMap[newDeviceIdx].mountpoint = calloc(strlen(mmceMountpoint) + 1, 1);
      strcpy(deviceModeMap[newDeviceIdx].mountpoint, mmceMountpoint);
      if (LAUNCHER_OPTIONS.mode & MODE_MMCE) // Set scan function only if MMCE is target mode
        deviceModeMap[newDeviceIdx].scan = &findISO;

      deviceCount++;
      newDeviceIdx++;
    }
  }
  return deviceCount;
}

// Maps driver name to ModeType
ModeType mapBDMDriverName(char *driverName) {
  if (!strncmp(driverName, "ata", 3))
    return MODE_ATA;
  else if (!strncmp(driverName, "sdc", 3))
    return MODE_MX4SIO;
  else if (!strncmp(driverName, "usb", 3))
    return MODE_USB;
  else if (!strncmp(driverName, "sd", 2))
    return MODE_ILINK;
  else if (!strncmp(driverName, "udp", 3))
    return MODE_UDPBD;
  else if (!strncmp(driverName, "hdlfs", 3))
    return MODE_HDL;
  return MODE_NONE;
}

// Uses MMCE devctl calls to switch memory card to given title ID
void mmceMountVMC(char *titleID) {
  // Send GameID to both MMCE devices
  for (int i = '0'; i < '2'; i++) {
    mmceMountpoint[4] = i;
    printf("Trying to mount VMC for %s on %s\n", titleID, mmceMountpoint);
    if (fileXioDevctl(mmceMountpoint, 0x8, titleID, (strlen(titleID) + 1), NULL, 0) < 0) {
      continue;
    }

    for (int i = 0; i < 15; i++) {
      delay(2);
      // Poll MMCE status until busy bit is clear
      if ((fileXioDevctl(mmceMountpoint, 0x2, NULL, 0, NULL, 0) & 1) == 0) {
        printf("VMC mounted\n");
        break;
      }
    }
  }
}

// Initializes map entries for APA-formatted HDDs with HDL partitions
int initHDLFS(int deviceIdx) {
  // Find the first available device for cover art and title options
  struct DeviceMapEntry *metadevice = NULL;
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (deviceModeMap[i].mode == MODE_NONE || deviceModeMap[i].mountpoint == NULL)
      break;

    if (!deviceModeMap[i].metadev) {
      metadevice = &deviceModeMap[i];
      break;
    }
  }

  char mountpoint[] = "hdd?:";
  DIR *directory;

  int deviceCount = 0;
  for (int i = 0; i < 2; i++) {
    deviceModeMap[deviceIdx].mode = MODE_NONE;
    mountpoint[3] = i + '0';

    // Wait for IOP to initialize device driver
    for (int attempts = 0; attempts < 2; attempts++) {
      delay(2);
      directory = opendir(mountpoint);
      if (directory != NULL) {
        closedir(directory);
        break;
      }
    }
    if (directory == NULL) {
      break;
    }

    // Set device mountpoint
    deviceModeMap[deviceIdx].mode = MODE_HDL;
    deviceModeMap[deviceIdx].mountpoint = calloc(strlen(mountpoint) + 1, 1);
    strcpy(deviceModeMap[deviceIdx].mountpoint, mountpoint);
    deviceModeMap[deviceIdx].metadev = metadevice;
    deviceModeMap[deviceIdx].index = i;
    // Set scan function
    deviceModeMap[deviceIdx].scan = &findHDLTargets;

    deviceIdx++;
    deviceCount++;
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

// Gets BDM driver name via fileXio
int getBDMDeviceDriver(char *mountpoint, struct DeviceMapEntry *entry) {
  int fd = fileXioDopen(mountpoint);
  if (fd < 0) {
    return -ENODEV;
  }

  char driverName[10];
  int deviceNumber;
  if (fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, driverName, sizeof(driverName) - 1) >= 0) {
    // Null-terminate the string before mapping
    driverName[sizeof(driverName) - 1] = '\0';
    entry->mode = mapBDMDriverName(driverName);
  }

  // Get device number
  if (fileXioIoctl2(fd, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, &deviceNumber, sizeof(deviceNumber)) >= 0)
    entry->index = deviceNumber;

  uiSplashLogString(LEVEL_INFO_NODELAY, "Found device %s%d (%s)\n", driverName, deviceNumber, mountpoint);
  fileXioDclose(fd);
  return 0;
}

// Initializes map entries for BDM devices
int initBDMDevices(int deviceIdx) {
  DIR *directory;
  char mountpoint[] = "massX:";

  int deviceCount = 0;
  int delayAttempts = 2;
  if (LAUNCHER_OPTIONS.mode & MODE_UDPBD) {
    // UDPBD needs considerably more time to init
    delayAttempts = 10;
  }
  for (int i = 0; i < 10; i++) {
    deviceModeMap[deviceIdx].mode = MODE_NONE;
    mountpoint[4] = i + '0';

    // Wait for IOP to initialize device driver
    for (int attempts = 0; attempts < delayAttempts; attempts++) {
      delay(2);
      directory = opendir(mountpoint);
      if (directory != NULL) {
        closedir(directory);
        break;
      }
    }
    if (directory == NULL) {
      // Since BDM devices are always mounted sequentially,
      // non-exiting mountpoint means that there will be no more devices
      break;
    }

    if (getBDMDeviceDriver(mountpoint, &deviceModeMap[deviceIdx]) < 0) {
      printf("ERROR: failed to get driver for device %s\n", mountpoint);
      return -EIO;
    }

    // Set device mountpoint
    deviceModeMap[deviceIdx].mountpoint = calloc(strlen(mountpoint) + 1, 1);
    strcpy(deviceModeMap[deviceIdx].mountpoint, mountpoint);
    // Set scan function
    deviceModeMap[deviceIdx].scan = &findISO;

    deviceIdx++;
    deviceCount++;
  }

  return deviceCount;
}
