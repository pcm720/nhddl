#include "devices/utils.h"
#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include <errno.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usbhdfsd-common.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// Attempts to guess device type from path
DeviceType guessDeviceType(const char *path) {
  if (strncmp(path, "mc", 2) == 0)
    return Device_Basic;
  if (strncmp(path, "mmce", 4) == 0)
    return Device_MMCE;
  if (strncmp(path, "hdd", 3) == 0)
    return Device_HDD;
  if (strncmp(path, "udpfs", 5) == 0)
    return Device_UDPFS;
  if (strncmp(path, "mass", 4) == 0)
    return Device_BDM;
  return Device_BDM;
}

// Maps driver name to DeviceType
static DeviceType mapBDMDriverName(char *driverName) {
  if (!strncmp(driverName, "ata", 3))
    return Device_HDD;
  else if (!strncmp(driverName, "sdc", 3))
    return Device_MX4SIO;
  else if (!strncmp(driverName, "usb", 3))
    return Device_USB;
  else if (!strncmp(driverName, "sd", 2))
    return Device_iLink;
  return Device_None;
}

// Gets BDM device type via fileXio
DeviceType getBDMDeviceType(char *mountpoint) {
  char driverName[10] = {0};
  int fd = fileXioDopen(mountpoint);
  if (fd < 0)
    return Device_None;

  if (fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, driverName, sizeof(driverName) - 1) >= 0)
    // Null-terminate the string before mapping
    driverName[sizeof(driverName) - 1] = '\0';

  fileXioDclose(fd);
  return mapBDMDriverName(driverName);
}

// Gets BDM device number via fileXio
int getBDMDeviceNumber(char *mountpoint) {
  int deviceNumber = -1;
  int fd = fileXioDopen(mountpoint);
  if (fd < 0)
    return -ENODEV;

  fileXioIoctl2(fd, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, &deviceNumber, sizeof(deviceNumber));
  fileXioDclose(fd);
  return deviceNumber;
}
