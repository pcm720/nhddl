// Implements support for MMCE devices
#include "common.h"
#include "devices.h"
#include "gui.h"
#include <errno.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Used to make devctl calls
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

static char mmceMountpoint[] = "mmceX:";

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
      deviceModeMap[newDeviceIdx].mountpoint = strdup(mmceMountpoint);
      deviceModeMap[newDeviceIdx].scan = &findISO;

      deviceCount++;
      newDeviceIdx++;
    }
  }
  return deviceCount;
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
