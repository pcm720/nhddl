// Implements support for MMCE devices
#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "ui/ui.h"
#include <errno.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char udpfsMountpoint[] = "udpfs0:";

// Initializes map entries for MMCE devices
int initUDPFSDevices(int newDeviceIdx) {
  DIR *directory;

  int deviceCount = 0;

  // Wait for IOP to initialize device driver
  for (int attempts = 0; attempts < 20; attempts++) {
    sleep(2);
    directory = opendir(udpfsMountpoint);
    if (directory != NULL) {
      closedir(directory);
      uiSplashLogString(LEVEL_INFO_NODELAY, "Found device %s\n", udpfsMountpoint);

      deviceModeMap[newDeviceIdx].mode = MODE_UDPFS;
      deviceModeMap[newDeviceIdx].index = 0;
      deviceModeMap[newDeviceIdx].mountpoint = strdup(udpfsMountpoint);
      deviceModeMap[newDeviceIdx].scan = &findISO;

      deviceCount++;
      newDeviceIdx++;
      return deviceCount;
    }
  }
  return 0;
}
