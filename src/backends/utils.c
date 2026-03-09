// Backend helpers
#include "backends/backends.h"
#include "backends/cache.h"
#include "backends/target.h"
#include <string.h>

void scanBackendDevice(struct BackendDevice *device) {
  if (device && device->scan) {
    device->scan(device);
    loadLastLaunchedIndex(device);
  }
}

void updateTargetFlagsAndPersist(struct BackendDevice *device, Target *target, uint32_t flags) {
  if (!device || !target)
    return;
  target->flags = flags;
  if (device->titles)
    storeTitleIDCache(device->titles, device);
}

void freeBackendDeviceTitles(struct BackendDevice *device) {
  if (!device)
    return;
  if (device->titles) {
    freeTargetList(device->titles);
    device->titles = NULL;
  }
}

struct BackendDevice *getBackendDeviceForPath(const char *path) {
  if (!path)
    return NULL;
  int n = getBackendDeviceCount();
  for (int i = 0; i < n; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (dev && dev->mountpoint && !strncmp(path, dev->mountpoint, strlen(dev->mountpoint)))
      return dev;
  }
  return NULL;
}

// Copies the mountpoint prefix of path (up to and including the first ':').
// Returns 0 on success. If no ':' is found, copies the whole path. Returns -1 if path is NULL or bufSize is 0.
int getMountpointFromPath(const char *path, char *buf, size_t bufSize) {
  if (!path || !buf || bufSize == 0)
    return -1;
  const char *colon = strchr(path, ':');
  size_t len = colon ? (size_t)(colon - path + 1) : strlen(path);
  if (len >= bufSize)
    len = bufSize - 1;
  memcpy(buf, path, len);
  buf[len] = '\0';
  return 0;
}
