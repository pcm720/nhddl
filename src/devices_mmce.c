// Implements support for MMCE devices
#include "common.h"
#include "devices.h"
#include "dprintf.h"
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
  char mcMountpoint[] = "mcX:";
  // Send GameID to both MMCE devices
  for (int i = '0'; i < '2'; i++) {
    mcMountpoint[2] = i;
    mmceMountpoint[4] = i;

    // Do not mount VMC if Neutrino is on the (virtual) memory card
    if (!strncmp(mcMountpoint, NEUTRINO_ELF_PATH, 3)) {
      DPRINTF("WARN: Refusing to mount VMC on %s\n", mmceMountpoint);
      continue;
    }

    // Ping MMCE first to make sure the device is present
    if (fileXioDevctl(mmceMountpoint, 0x1, NULL, 0, NULL, 0) < 0)
      continue;

    if ((fileXioDevctl(mmceMountpoint, 0x8, titleID, (strlen(titleID) + 1), NULL, 0)) < 0) {
      // Skip polling if devctl call has failed
      continue;
    }

    // Poll MMCE status until busy bit is clear
    for (int i = 0; i < 15; i++) {
      delay(2);
      if ((fileXioDevctl(mmceMountpoint, 0x2, NULL, 0, NULL, 0) & 1) == 0) {
        DPRINTF("Mounted VMC for %s on %s\n", titleID, mmceMountpoint);
        break;
      }
    }
  }
}
