// Backend initialization: initBackend, initBackendForImage, and generic device probe.
#include "backends/internal.h"
#include "config/config.h"
#include "devices/devices.h"
#include "devices/utils.h"
#include "dprintf.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int initHDL(struct BackendDevice *slot);

// Probes devices of the given type, fills slots with findISO scan. Returns number of slots filled.
static int initDevice(struct BackendDevice *slot, DeviceType type) {
  char path[12];
  int maxDevices = getDeviceInfo(type, path, sizeof(path));
  if (maxDevices <= 0)
    return 0;

  size_t baseLen = strlen(path);
  int deviceCount = 0;
  int maxAttempts = getProbeDelay() > 0 ? getProbeDelay() : 1;

  for (int i = 0; i < maxDevices; i++) {
    path[baseLen] = '0' + (char)i;
    path[baseLen + 1] = ':';
    path[baseLen + 2] = '\0';
    DIR *dir = NULL;
    for (int attempt = 0; attempt < maxAttempts; attempt++) {
      dir = opendir(path);
      if (dir)
        break;
      sleep(1);
    }
    if (!dir)
      continue;
    closedir(dir);

    slot->type = type;
    slot->index = (uint8_t)i;
    slot->mountpoint = strdup(path);
    slot->scan = &findISO;
    slot->sync = NULL;
    slot->cleanup = NULL;
    slot->metadev = NULL;
    slot->titles = NULL;
    slot->lastLaunchedTitleIdx = -1;
    DPRINTF("backends/init: found device %s\n", slot->mountpoint);
    slot++;
    deviceCount++;
  }
  return deviceCount;
}

// Initializes backend for the given image path. Only used by forwardBoot.
// If noinit: add a single device from the path (no modules, no IOP reboot). Otherwise init the device type and return.
// Returns number of devices added, or negative on error.
int initBackendForImage(const char *image) {
  if (!image || !image[0])
    return -EINVAL;

  if (getNoInit()) {
    const char *colon = strchr(image, ':');
    if (!colon)
      return -EINVAL;
    size_t prefixLen = (size_t)(colon - image + 1);
    if (prefixLen > 11)
      return -EINVAL;
    char prefix[12];
    memcpy(prefix, image, prefixLen);
    prefix[prefixLen] = '\0';

    DIR *dir = opendir(prefix);
    if (!dir)
      return -EIO;
    closedir(dir);

    int nextSlot = getBackendDeviceCount();
    if (nextSlot >= MAX_DEVICES)
      return -ENOMEM;

    struct BackendDevice *slot = &backendDevices[nextSlot];
    slot->mountpoint = strdup(prefix);
    if (!slot->mountpoint)
      return -ENOMEM;
    slot->type = guessDeviceType(prefix);
    if (slot->type == Device_None || slot->type == Device_BDM)
      slot->type = Device_ATA;
    slot->index = 0;
    slot->scan = &findISO;
    slot->sync = NULL;
    slot->cleanup = NULL;
    slot->metadev = NULL;
    slot->titles = NULL;
    slot->lastLaunchedTitleIdx = -1;
    return 1;
  }

  DeviceType t = guessDeviceType((char *)image);
  if (t == Device_None || t == Device_BDM)
    t = Device_HDD;
  return initBackend(t);
}

int initBackend(DeviceType device) {
  if (device == Device_None)
    return 0;

  int listInvalidated = 0;
  DeviceType conflict = getConflictingDeviceTypes(device);
  if (conflict != Device_None) {
    // Refuse to init backend if our root is on conflicting device
    const char *root = getNHDDLRawRoot();
    if (root && root[0] && (guessDeviceType(root) & conflict))
      return -EINVAL;

    int n = getBackendDeviceCount();
    for (int i = 0; i < n; i++) {
      if (backendDevices[i].type & conflict) {
        cleanupAllBackends();
        removeConflictingBackends(conflict);
        listInvalidated = 1;
        break;
      }
    }
  }
  if (loadDeviceModules(device))
    return -EIO;
  if (listInvalidated)
    rescanAllBackendDevices();

  int nextSlot = getBackendDeviceCount();
  if (nextSlot >= MAX_DEVICES)
    return listInvalidated ? (0 | LIST_INVALIDATED) : 0;

  struct BackendDevice *slot = &backendDevices[nextSlot];
  int added = 0;

  if (device == Device_HDD) {
    int n = initHDL(slot);
    if (n > 0)
      added = n;
  } else {
    added = initDevice(slot, device);
  }

  if (added < 0)
    return added;
  return added | (listInvalidated ? LIST_INVALIDATED : 0);
}
