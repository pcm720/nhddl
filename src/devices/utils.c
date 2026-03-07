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
  if (!strncmp(path, "mc", 2))
    return Device_Basic;
  if (!strncmp(path, "mmce", 4))
    return Device_MMCE;
  if (!strncmp(path, "hdd", 3))
    return Device_HDD;
  if (!strncmp(path, "udpfs", 5))
    return Device_UDPFS;
  if (!strncmp(path, "mass", 4))
    return Device_BDM;
  return Device_BDM;
}
