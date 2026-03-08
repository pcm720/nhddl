#include "devices/utils.h"
#include "common.h"
#include "config/config.h"
#include "devices/devices.h"
#include "dprintf.h"
#include <errno.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <usbhdfsd-common.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// Attempts to guess device type from path
DeviceType guessDeviceType(const char *path) {
  if (!strncmp(path, "mc", 2))
    return Device_Basic;
  if (!strncmp(path, "mmce", 4))
    return Device_MMCE;
  if (!strncmp(path, "ata", 3))
    return Device_ATA;
  if (!strncmp(path, "hdd", 3))
    return Device_HDD;
  if (!strncmp(path, "udpfs", 5))
    return Device_UDPFS;
  if (!strncmp(path, "usb", 3))
    return Device_USB;
  if (!strncmp(path, "mx4sio", 6))
    return Device_MX4SIO;
  if (!strncmp(path, "ilink", 5))
    return Device_iLink;
  if (!strncmp(path, "mass", 4))
    return Device_BDM;
  return Device_None;
}

// Uses MMCE devctl calls to switch memory card to given title ID
void mmceMountVMC(char *titleID) {
  char mcMountpoint[] = "mcX:";
  static char mmceMountpoint[] = "mmceX:";
  for (int i = '0'; i < '2'; i++) {
    mcMountpoint[2] = i;
    mmceMountpoint[4] = i;

    const char *neutrinoPath = getNeutrinoPath();
    if (neutrinoPath && neutrinoPath[0] && !strncmp(mcMountpoint, neutrinoPath, 3)) {
      DPRINTF("WARN: Refusing to mount VMC on %s\n", mmceMountpoint);
      continue;
    }

    if (fileXioDevctl(mmceMountpoint, 0x1, NULL, 0, NULL, 0) < 0)
      continue;

    if (fileXioDevctl(mmceMountpoint, 0x8, titleID, (int)(strlen(titleID) + 1), NULL, 0) < 0)
      continue;

    for (int j = 0; j < 15; j++) {
      sleep(2);
      if ((fileXioDevctl(mmceMountpoint, 0x2, NULL, 0, NULL, 0) & 1) == 0) {
        DPRINTF("Mounted VMC for %s on %s\n", titleID, mmceMountpoint);
        break;
      }
    }
  }
}
