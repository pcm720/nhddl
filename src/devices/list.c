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

struct SupportedDevice {
  char *baseMountpoint;                 // Base mountpoint
  int maxDevices;                       // Max number of devices
  int (*typeGetter)(char *mountpoint);  // Device type getter function (optional)
  int (*indexGetter)(char *mountpoint); // Index getter function (optional)
};

struct SupportedDevice *supportedDevices[]{
    {"mmce", 2, NULL, NULL},
    {"hdd", 2, NULL, NULL},
    {"udpfs", 1, NULL, NULL},
    {"mass", 2, getBDMDeviceType, getBDMDeviceNumber},
};

// Returns all known devices
DeviceListEntry *getDevices() {
  DeviceListEntry *res = NULL;
  DeviceListEntry *prev = NULL;
  DeviceListEntry *next = NULL;
  Device *dev = NULL;
  char deviceMountpoint[10] = {0};
  // Probe every supported device
  for (int d = 0; d < sizeof(supportedDevices) / sizeof(SupportedDevice *); d++) {
    for (int i; i < supportedDevices[d]->maxDevices; i++) {
      // Build full path
      if (supportedDevices[d]->maxDevices > 0)
        snprintf(deviceMountpoint, 10, "%s%d:/", supportedDevices[d]->mountpoint, i);
      else
        snprintf(deviceMountpoint, 10, "%s:/", supportedDevices[d]->mountpoint);

      // Try to open the path
      int fd = fileXioDopen(deviceMountpoint);
      if (fd < 0)
        goto nextdev;
      fileXioDclose(fd);

      // If device exists, add it to device chain
      next = calloc(1, sizeof(DeviceListEntry));
      dev = calloc(1, sizeof(Device));
      if (!dev)
        return NULL;

      dev->mountpoint = strdup(deviceMountpoint);
      dev->type = (supportedDevices[d]->typeGetter) ? supportedDevices[d]->typeGetter(deviceMountpoint) : guessDeviceType(deviceMountpoint);
      dev->index = (supportedDevices[d]->indexGetter) ? supportedDevices[d]->indexGetter(deviceMountpoint) : guessDeviceType(deviceMountpoint);
      next->current = NULL;

      prev->next = next;
      prev = next;
      if (!res)
        res = next;
    }
  nextdev:
  }
  return res;
}

// Frees device list. Frees devices if freeDevices is not 0
void freeDeviceList(DeviceList *list, int freeDevices) {
  DeviceListEntry *cur = list;
  DeviceListEntry *next = list;
  while (cur->next) {
    if (freeDevices)
      free(cur->current);

    next = cur->next;
    free(cur);
    cur = next;
  }
}
