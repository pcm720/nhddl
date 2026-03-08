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
  const char *baseMountpoint; // Base mountpoint (e.g. "ata", "usb")
  int maxDevices;             // Max number of devices
  DeviceType type;            // Device type
};

static struct SupportedDevice supportedDevices[] = {
    {"mmce", 2, Device_MMCE},     // MMCE
    {"udpfs", 1, Device_UDPFS},   // UDPFS
    {"ata", 2, Device_ATA},       // exFAT on internal HDD
    {"hdd", 2, Device_HDD},       // APA on internal HDD
    {"usb", 2, Device_USB},       // USB
    {"mx4sio", 1, Device_MX4SIO}, // MX4SIO
    {"ilink", 1, Device_iLink},   // iLink
};

#define SUPPORTED_DEVICE_COUNT (sizeof(supportedDevices) / sizeof(supportedDevices[0]))

int getDeviceInfo(DeviceType type, char *baseMountpoint, size_t baseMountpointSize) {
  if (!baseMountpoint || baseMountpointSize == 0)
    return 0;
  for (size_t i = 0; i < SUPPORTED_DEVICE_COUNT; i++) {
    if ((supportedDevices[i].type & type) == 0)
      continue;
    size_t len = strlen(supportedDevices[i].baseMountpoint) + 1;
    if (len > baseMountpointSize)
      len = baseMountpointSize;
    memcpy(baseMountpoint, supportedDevices[i].baseMountpoint, len);
    baseMountpoint[baseMountpointSize - 1] = '\0';
    return supportedDevices[i].maxDevices;
  }
  return 0;
}
