#include "devices/utils.h"
#include "config/config.h"
#include "devices/devices.h"
#include "dprintf.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>
#include <usbhdfsd-common.h>

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
char *getDeviceString(DeviceType type) {
  switch (type) {
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
  if (isDeviceLoaded(Device_MX4SIO))
    return;
  if (!isDeviceLoaded(Device_MMCE) && loadDeviceModules(Device_MMCE))
    return;

  char mcMountpoint[] = "mcX:";
  static char mmceMountpoint[] = "mmceX:";
  for (int i = '0'; i < '2'; i++) {
    mcMountpoint[2] = i;
    mmceMountpoint[4] = i;

    const char *neutrinoPath = getNeutrinoPath();
    if (neutrinoPath && neutrinoPath[0] && !strncmp(mcMountpoint, neutrinoPath, 3)) {
      DPRINTF("devices: warning: Refusing to mount VMC on %s\n", mmceMountpoint);
      continue;
    }

    if (fileXioDevctl(mmceMountpoint, 0x1, NULL, 0, NULL, 0) < 0)
      continue;

    if (fileXioDevctl(mmceMountpoint, 0x8, titleID, (int)(strlen(titleID) + 1), NULL, 0) < 0)
      continue;

    for (int j = 0; j < 15; j++) {
      sleep(2);
      if (!(fileXioDevctl(mmceMountpoint, 0x2, NULL, 0, NULL, 0) & 1)) {
        DPRINTF("devices: mounted VMC for %s on %s\n", titleID, mmceMountpoint);
        break;
      }
    }
  }
}

// Maps BDM driver name (from ioctl) to our DeviceType. BDM uses internal names like "ata", "sdc", "usb".
static DeviceType mapBDMDriverName(char *driverName) {
  if (!strncmp(driverName, "ata", 3))
    return Device_HDD;
  if (!strncmp(driverName, "sdc", 3))
    return Device_MX4SIO;
  if (!strncmp(driverName, "usb", 3))
    return Device_USB;
  if (!strncmp(driverName, "sd", 2))
    return Device_iLink;
  return Device_None;
}

// Converts massN: path into canonical path by resolving the underlying driver and device number via fileXio.
// Returns NULL and sets *type to Device_None on failure. Caller must free the returned string after use.
char *guessCWDDevice(const char *cwd, DeviceType *type) {
  if (!cwd || !type)
    return NULL;
  *type = Device_None;

  size_t bufSize = strlen(cwd) + 10;
  char *buf = malloc(bufSize);
  if (!buf)
    return NULL;

  const char *colon = strchr(cwd, ':');
  if (!colon) {
    free(buf);
    return NULL;
  }
  size_t mountLen = (size_t)(colon - cwd + 1);
  if (mountLen >= bufSize) {
    free(buf);
    return NULL;
  }
  memcpy(buf, cwd, mountLen);
  buf[mountLen] = '\0';

  // fileXioDopen will fail if the mountpoint is invalid (e.g. not mass or not present).
  int fd = fileXioDopen(buf);
  if (fd < 0) {
    free(buf);
    return NULL;
  }
  char driverName[10] = {0};
  int deviceNumber = -1;
  // Resolve driver name and device number via ioctl
  fileXioIoctl2(fd, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, driverName, sizeof(driverName) - 1);
  fileXioIoctl2(fd, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, &deviceNumber, sizeof(deviceNumber));
  fileXioDclose(fd);
  driverName[sizeof(driverName) - 1] = '\0';

  // Map BDM driver name to our type and get canonical base mountpoint from supportedDevices.
  DeviceType deviceType = mapBDMDriverName(driverName);
  if (deviceType == Device_None) {
    free(buf);
    return NULL;
  }
  char baseMountpoint[16];
  if (getDeviceInfo(deviceType, baseMountpoint, sizeof(baseMountpoint)) <= 0) {
    free(buf);
    return NULL;
  }
  if (deviceNumber < 0)
    deviceNumber = 0;

  // Build result in buf
  const char *pathPart = cwd + mountLen;
  size_t pathPartLen = strlen(pathPart);
  int needSlash = (pathPartLen == 0 || pathPart[pathPartLen - 1] != '/');
  int n = snprintf(buf, bufSize, "%s%d:%s%s", baseMountpoint, deviceNumber, pathPart, needSlash ? "/" : "");
  if (n < 0 || (size_t)n >= bufSize) {
    free(buf);
    return NULL;
  }
  *type = deviceType;
  return buf;
}

// Probes device prefix (path up to first ':') with opendir in a loop. Uses getProbeDelay() for max attempts; if 0, uses 10.
// Returns 0 if opendir succeeds within the delay, non-zero on failure.
int probePathPrefix(char *path, int noDelay) {
  char *relPath = strchr(path, ':');
  if (!relPath)
    return -1;
  char saved = *(++relPath);
  relPath = '\0';

  for (int attempt = 0; attempt < getProbeDelay(); attempt++) {
    DIR *dir = opendir(path);
    if (dir) {
      closedir(dir);
      *relPath = saved;
      return 0;
    }
    if (noDelay)
      return -1;

    sleep(1);
  }
  *relPath = saved;
  return -1;
}

// Probes and builds canonical path for any given path and device type.
// Expects the path to be path to directory (e.g. CWD)
char *probeCanonicalPath(const char *path, DeviceType type) {
  char *relPath = strchr(path, ':');
  if (!relPath)
    return NULL;
  relPath++;

  size_t bufSize = strlen(relPath) + 10;
  char *buf = calloc(sizeof(char), bufSize);
  if (!buf)
    return NULL;

  // Extract relative path
  char mountpoint[10] = {0};
  int deviceCount = getDeviceInfo(type, buf, bufSize);
  if (!deviceCount) {
    free(buf);
    return NULL;
  }

  for (int i; i < deviceCount; i++) {
    snprintf(buf, bufSize, "%s%d:%s%s", mountpoint, i, relPath);
    // For the first device, probe with delay
    if (!probePathPrefix(buf, i))
      return buf;
  }
  free(buf);
  return NULL;
}
