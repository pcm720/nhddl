#include "backends/backends.h"
#include "backends/cache.h"
#include "config/config.h"
#include "devices/devices.h"
#include "devices/utils.h"
#include "dprintf.h"
#include "ui/ui.h"
#include <dirent.h>
#include <errno.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Contains all available backend devices. Device must be ignored if type is Device_None
struct BackendDevice backendDevices[MAX_DEVICES];

// Device types that can be enabled in config (one bit each). Order determines iteration.
static const DeviceType enabledDeviceTypeBits[] = {
  Device_MMCE,
  Device_ATA,
  Device_HDD,
  Device_USB,
  Device_UDPFS,
  Device_MX4SIO,
};
static const int numEnabledDeviceTypeBits =
    (int)(sizeof(enabledDeviceTypeBits) / sizeof(enabledDeviceTypeBits[0]));

int getBackendDeviceCount(void) {
  int i = 0;
  while (i < MAX_DEVICES && backendDevices[i].type != Device_None)
    i++;
  return i;
}

struct BackendDevice *getBackendDeviceAt(int index) {
  int n = getBackendDeviceCount();
  if (index < 0 || index >= n)
    return NULL;
  return &backendDevices[index];
}

TargetList *getBackendDeviceTitles(struct BackendDevice *device) { return device ? device->titles : NULL; }

int getBackendDeviceCountByType(DeviceType type) {
  int n = 0;
  for (int i = 0; i < MAX_DEVICES && backendDevices[i].type != Device_None; i++)
    if (backendDevices[i].type == type)
      n++;
  return n;
}

struct BackendDevice *getBackendDeviceOfType(DeviceType type, int index) {
  int cur = 0;
  for (int i = 0; i < MAX_DEVICES && backendDevices[i].type != Device_None; i++) {
    if (backendDevices[i].type != type)
      continue;
    if (cur == index)
      return &backendDevices[i];
    cur++;
  }
  return NULL;
}

// Remove backends whose type is in the conflict mask; compact array so no holes
void removeConflictingBackends(DeviceType conflictMask) {
  int write = 0;
  for (int read = 0; read < MAX_DEVICES; read++) {
    if (backendDevices[read].type == Device_None)
      break;
    if (conflictMask & backendDevices[read].type) {
      freeBackendDeviceTitles(&backendDevices[read]);
      backendDevices[read].type = Device_None;
      backendDevices[read].mountpoint = NULL;
      backendDevices[read].scan = NULL;
      backendDevices[read].sync = NULL;
      backendDevices[read].cleanup = NULL;
      backendDevices[read].metadev = NULL;
      backendDevices[read].lastLaunchedTitleIdx = -1;
      continue;
    }
    if (write != read) {
      backendDevices[write] = backendDevices[read];
      backendDevices[read].type = Device_None;
      backendDevices[read].mountpoint = NULL;
      backendDevices[read].scan = NULL;
      backendDevices[read].sync = NULL;
      backendDevices[read].cleanup = NULL;
      backendDevices[read].metadev = NULL;
      backendDevices[read].lastLaunchedTitleIdx = -1;
    }
    write++;
  }
  for (int i = write; i < MAX_DEVICES; i++)
    backendDevices[i].type = Device_None;
}

void freeAllBackendTitles(void) {
  for (int i = 0; i < MAX_DEVICES && backendDevices[i].type != Device_None; i++)
    freeBackendDeviceTitles(&backendDevices[i]);
}

// Runs cleanup on all backend devices (unmount PFS, etc.). Call before launch, exit, or IOP reboot.
void cleanupAllBackends(void) {
  for (int i = 0; i < MAX_DEVICES && backendDevices[i].type != Device_None; i++) {
    if (backendDevices[i].sync)
      backendDevices[i].sync(&backendDevices[i]);
    if (backendDevices[i].cleanup)
      backendDevices[i].cleanup(&backendDevices[i]);
  }
}

// Rescan all backend devices (e.g. after conflict reinit)
void rescanAllBackendDevices(void) {
  for (int i = 0; i < MAX_DEVICES && backendDevices[i].type != Device_None; i++)
    if (backendDevices[i].scan)
      backendDevices[i].scan(&backendDevices[i]);
}

// Fills out[] with each DeviceType bit set in getEnabledDevices(), up to maxCount.
// Returns the number of types written. UI uses this to know which device types to init/offer.
int getEnabledDeviceTypesArray(DeviceType *out, int maxCount) {
  if (!out || maxCount <= 0)
    return 0;
  DeviceType mask = getEnabledDevices();
  int n = 0;
  for (int i = 0; i < numEnabledDeviceTypeBits && n < maxCount; i++) {
    if (mask & enabledDeviceTypeBits[i])
      out[n++] = enabledDeviceTypeBits[i];
  }
  return n;
}
