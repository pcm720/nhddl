#include "backends/backends.h"
#include "backends/title_id.h"
#include "config/config.h"
#include "config/neutrino_args.h"
#include "devices/devices.h"
#include "devices/utils.h"
#include "dprintf.h"
#include "neutrino/neutrino.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Quickly forwards the image to Neutrino without loading the UI
int forwardBoot() {
  const char *image = getImage();
  if (!image || !image[0]) {
    displayFatalError("No image path\n");
    return -EINVAL;
  }

  // Get relative path to ISO and try to guess device type
  int relIdx = getRelativePathIdx((char *)image);
  DeviceType type = guessDeviceType((char *)image);
  if ((relIdx < 0) || (type == Device_None)) {
    displayFatalError("Invalid image path\n");
    return -EINVAL;
  }

  // Generate canonical path for initializing device backends
  char canonicalPath[PATH_MAX] = {0};
  if (type == Device_BDM) {
    // Default to exFAT partition on internal HDD for BDM
    getDeviceInfo(Device_ATA, canonicalPath, PATH_MAX);
    strcat(canonicalPath, "0:");
    strcat(canonicalPath, image + relIdx);
  } else
    strncpy(canonicalPath, image, PATH_MAX - 1);

  // Initialize device backend
  int res = initBackendForImage(canonicalPath);
  if (res < 0) {
    displayFatalError("Failed to init backend: %d\n", res);
    return res;
  }

  // Check if image exist
  res = open(canonicalPath, O_RDONLY);
  if (res < 0) {
    displayFatalError("Target image not found\n");
    return -ENOENT;
  }
  close(res);

  // Create target entity
  Target target = {
      .idx = 0,
      .id = getTitleID((char *)image),
      .device = getBackendDeviceForPath(canonicalPath),
      .path = (char *)(image + relIdx),
  };
  if (!target.device) {
    displayFatalError("Target device not found\n");
    free(target.id);
    return -ENODEV;
  }

  // Parse ISO name from path
  char *fileext = strrchr((char *)image, '.');
  if (fileext && (!strcmp(fileext, ".iso") || !strcmp(fileext, ".ISO"))) {
    char *isoName = strrchr((char *)image, '/');
    if (!isoName)
      isoName = (char *)image;
    else
      isoName++;
    int nameLength = (int)(fileext - isoName);
    target.name = calloc(sizeof(char), nameLength + 1);
    if (target.name)
      strncpy(target.name, isoName, nameLength);
  }

  // Load Neutrino arguments
  ArgumentList *globalArguments = calloc(sizeof(ArgumentList), 1);
  ArgumentList *titleArguments = calloc(sizeof(ArgumentList), 1);
  if (!globalArguments || !titleArguments) {
    displayFatalError("Failed to allocate memory for Neutrino arguments\n");
    __builtin_trap();
  }
  loadGlobalNeutrinoArguments(globalArguments, target.device);
  loadTitleNeutrinoArguments(titleArguments, &target);
  // Merge title into global (global is base); title wins on duplicate names.
  ArgumentList *arguments = mergeNeutrinoArguments(globalArguments, titleArguments);
  freeArgumentList(globalArguments);
  freeArgumentList(titleArguments);

  switch ((res = launchTarget(&target, arguments))) {
  case -ENOENT:
    displayFatalError("Neutrino not found\n");
    break;
  case -EINVAL:
    displayFatalError("Unsupported target device\n");
    break;
  }
  freeArgumentList(arguments);
  free(target.name);
  free(target.id);
  return res;
}
