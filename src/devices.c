#include "devices.h"
#include "common.h"
#include <errno.h>
#include <kernel.h>
#include <stdio.h>
#include <string.h>
#include <usbhdfsd-common.h>

// Used to get BDM driver name
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// Maps mass device index to supported mode.
// Device must be ignored if mode is MODE_ALL
DeviceMapEntry deviceModeMap[MAX_MASS_DEVICES] = {};

// Maps driver name to ModeType
ModeType mapDriverName(char *driverName) {
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
  return MODE_ALL;
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
int getDeviceDriver(char *mountpoint, DeviceMapEntry *entry) {
  int fd = fileXioDopen(mountpoint);
  if (fd < 0) {
    return -ENODEV;
  }

  char driverName[10];
  int deviceNumber;
  if (fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, driverName, sizeof(driverName) - 1) >= 0) {
    // Null-terminate the string before mapping
    driverName[sizeof(driverName) - 1] = '\0';
    entry->mode = mapDriverName(driverName);
  }

  // Get device number
  if (fileXioIoctl2(fd, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, &deviceNumber, sizeof(deviceNumber)) >= 0)
    entry->index = deviceNumber;

  printf("Found device %s%d\n", driverName, deviceNumber);
  fileXioDclose(fd);
  return 0;
}

// Initializes device mode map and returns device count
int initDeviceMap() {
  DIR *directory;
  char mountpoint[] = MASS_PLACEHOLDER;

  int deviceCount = 0;
  int delayAttempts = 2;
  if ((LAUNCHER_OPTIONS.mode & MODE_UDPBD)) {
    // UDPBD needs considerably more time to init
    delayAttempts = 10;
  }
  for (int i = 0; i < MAX_MASS_DEVICES; i++) {
    deviceModeMap[i].mode = MODE_ALL;
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

    if (getDeviceDriver(mountpoint, &deviceModeMap[i]) < 0) {
      printf("ERROR: failed to get driver for device %s\n", mountpoint);
      return -EIO;
    }
    deviceCount++;
  }
  return deviceCount;
}
