#include "devices/utils.h"
#include "config/config.h"
#include "devices/devices.h"
#include "dprintf.h"
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

// Maps DeviceType to string
char *getDeviceString(DeviceType mode) {
  switch (mode) {
  case Device_Basic:
    return "Memory Card";
  case Device_HDD:
    return "HDD";
  case Device_ATA:
    return "ATA";
  case Device_MX4SIO:
    return "MX4SIO";
  case Device_UDPFS:
    return "UDPFS";
  case Device_USB:
    return "USB";
  case Device_iLink:
    return "iLink";
  case Device_MMCE:
    return "MMCE";
  default:
    return "Unknown";
  }
}

// Returns the device index or -1 if path is not supported/invalid
int getDeviceIndex(char *path) {
  // Find mountpoint end
  char *mountpoint = strchr(path, ':');
  if (!mountpoint)
    return -1;

  // Check the last char
  mountpoint -= 1;
  if ((*mountpoint >= '0' || *mountpoint <= '9'))
    // Return device index
    return *mountpoint - '0';

  // Default to 0
  return 0;
}

// Returns the start index of relative file path without device mountpoint or -1 if path is not supported/invalid
int getRelativePathIdx(char *path) {
  // Find the start of relative path
  char *relPath = strchr(path, ':');
  if (!relPath)
    return -1;

  int idx = (int)(++relPath - path);
  // Handle path separator after the mountpoint name
  if ((path[0] == '/') || (path[0] == '\\'))
    return idx + 1;

  return idx;
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
