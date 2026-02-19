#include "common.h"
#include "backends/backends.h"
#include "dprintf.h"
#include "neutrino.h"
#include "devices/devices.h"
#include "options.h"
#include "backends/title_id.h"
#include <stdlib.h>
#include <string.h>

// Quickly forwards the image to Neutrino without loading the UI
int forwardBoot() {
  int res;
  // Forward to Neutrino without loading the UI
  if (!LAUNCHER_OPTIONS.noInit)
    res = initModules(INIT_TYPE_FULL);
  else
    res = initModules(INIT_TYPE_NOINIT);
  if (res) {
    DPRINTF("Failed to init modules: %d\n", res);
    return res;
  }

  int deviceCount = initDeviceMap();
  if (deviceCount <= 0) {
    DPRINTF("Failed to init devices: %d\n", deviceCount);
    return -ENODEV;
  }

  if ((res = tryFile(LAUNCHER_OPTIONS.image)) < 0) {
    DPRINTF("Target image not found: %d\n", res);
    return -ENOENT;
  }

  if (findNeutrinoELF(NULL, INIT_TYPE_FULL)) {
    DPRINTF("Failed to find Neutrino\n");
    return -ENOENT;
  }

  Target target = {
      .idx = 0,
      .id = getTitleID(LAUNCHER_OPTIONS.image),
      .fullPath = LAUNCHER_OPTIONS.image,
  };

  char *fileext = strrchr(LAUNCHER_OPTIONS.image, '.');
  if ((fileext != NULL) && (!strcmp(fileext, ".iso") || !strcmp(fileext, ".ISO"))) {
    // Get file name without the extension
    char *isoName = strrchr(LAUNCHER_OPTIONS.image, '/');
    if (!isoName)
      isoName = LAUNCHER_OPTIONS.image;
    else
      isoName++;

    int nameLength = (int)(fileext - isoName);
    target.name = calloc(sizeof(char), nameLength + 1);
    strncpy(target.name, isoName, nameLength);
  }

  for (int i = 0; i < deviceCount; i++)
    if (strstr(LAUNCHER_OPTIONS.image, deviceModeMap[i].mountpoint)) {
      target.device = &deviceModeMap[i];
      break;
    }

  if (!target.device) {
    DPRINTF("Target device not found\n");
    return -ENODEV;
  }

  // Run the image
  launchTitle(&target, loadLaunchArgumentLists(&target));
  return -ENOENT;
}
