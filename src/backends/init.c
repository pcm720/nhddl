// Backend initialization functions
#include "backends/internal.h"
#include "backends/title_id.h"
#include "config/config.h"
#include "devices/devices.h"
#include "devices/hdd.h"
#include "devices/utils.h"
#include "dprintf.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int initHDL(struct BackendDevice *slot);
Target *scanPartition(char *deviceMountpoint, char *partitionName, uint32_t startSector);

// Probes devices of the given type, fills slots with findISO scan. Returns number of slots filled.
static int initDevice(struct BackendDevice *slot, DeviceType type, int probeAttempts) {
  char path[12];
  int maxDevices = getDeviceInfo(type, path, sizeof(path));
  if (maxDevices <= 0)
    return 0;

  size_t baseLen = strlen(path);
  int deviceCount = 0;

  for (int i = 0; i < maxDevices; i++) {
    if (i > 0)
      probeAttempts = 1;

    path[baseLen] = '0' + (char)i;
    path[baseLen + 1] = ':';
    path[baseLen + 2] = '\0';
    DIR *dir = NULL;
    for (int attempt = 0; attempt < probeAttempts; attempt++) {
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

// Initializes backend for the given device type (loads device modules, removes conflicting backends, runs backend init).
// device must be a single type (e.g. Device_MMCE or Device_HDD), not a mask. Call once per type.
// Returns number of devices added, or negative on error.
// If skipDeviceInit is 1, will skip initializing device modules
// If the device list was invalidated (e.g. conflict reinit), the high bit is set so UI can reload views (e.g. return value & LIST_INVALIDATED).
int initBackend(DeviceType device, int skipDeviceInit) {
  if (device == Device_None)
    return 0;

  int listInvalidated = 0;
  if (!skipDeviceInit) {
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
  }

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
    added = initDevice(slot, device, (skipDeviceInit) ? 1 : getProbeDelay());
  }

  if (added < 0)
    return added;
  return added | (listInvalidated ? LIST_INVALIDATED : 0);
}

// Initializes device, backend and target for the given image path for forwarder boot and returns target.
// Returns NULL on error.
Target *initializeTargetForImage(const char *image) {
  // Get relative path to ISO and try to guess device type
  int relIdx = getRelativePathIdx((char *)image);
  DeviceType type = guessDeviceType((char *)image);
  if ((relIdx < 0) || (type == Device_None)) {
    displayFatalError("Invalid image path\n");
    return NULL;
  }

  // Generate canonical path for initializing device backends
  char canonicalPath[PATH_MAX] = {0};
  if (type == Device_BDM) {
    // Default to exFAT partition on internal HDD for BDM
    type = Device_ATA;
    getDeviceInfo(Device_ATA, canonicalPath, PATH_MAX);
    strcat(canonicalPath, "0:");
    strcat(canonicalPath, image + relIdx);
  } else
    strncpy(canonicalPath, image, PATH_MAX - 1);

  // Initialize device backend
  DPRINTF("devices/init: initializing target device\n");
  int res = initBackend(type, getNoInit());
  if (res < 0) {
    displayFatalError("Failed to init %s backend: %d\n", getDeviceString(type), res);
    return NULL;
  }

  DPRINTF("devices/init: initializing target\n");
  Target *target = NULL;
  if (type == Device_HDD) {
    // Partition-backed target
    DPRINTF("devices/init: getting partition LBA and scanning HDL header\n");
    uint32_t lba = getHDDPartitionLBA(image);
    if (lba != -1)
      target = scanPartition((char *)image, (char *)(image + 5), lba);
  } else {
    // File-backed target
    res = open(canonicalPath, O_RDONLY);
    if (res >= 0) {
      close(res);

      // Create target entity
      DPRINTF("devices/init: initializing target data\n");
      target = calloc(sizeof(Target), 1);
      target->id = getTitleID((char *)image);
      target->path = strdup((char *)(image + relIdx));

      // Parse ISO name from path
      char *fileext = strrchr((char *)image, '.');
      if (fileext && (!strcmp(fileext, ".iso") || !strcmp(fileext, ".ISO"))) {
        char *isoName = strrchr((char *)image, '/');
        if (!isoName)
          isoName = (char *)image;
        else
          isoName++;
        int nameLength = (int)(fileext - isoName);
        target->name = calloc(sizeof(char), nameLength + 1);
        if (target->name)
          strncpy(target->name, isoName, nameLength);
      }
    }
  }
  if (!target) {
    displayFatalError("Target image not found\n");
    return NULL;
  }
  target->device = getBackendDeviceForPath(canonicalPath);
  if (!target->device) {
    displayFatalError("Target device not found\n");
    freeTarget(NULL, target);
    return NULL;
  }

  return target;
}
