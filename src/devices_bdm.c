// Implements support for BDM devices
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

//
// The following functions are based on code by AKuHAK
//

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
    deviceModeMap[deviceIdx].mountpoint = strdup(mountpoint);
    // Set scan function
    deviceModeMap[deviceIdx].scan = &findISO;

    deviceIdx++;
    deviceCount++;
  }

  return deviceCount;
}
