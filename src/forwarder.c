#include "backends/backends.h"
#include "config/config.h"
#include "config/neutrino_args.h"
#include "dprintf.h"
#include "neutrino/neutrino.h"
#include <stdlib.h>

// Quickly forwards the image to Neutrino without loading the UI
int forwardBoot() {
  const char *image = getImage();
  if (!image || !image[0]) {
    displayFatalError("No image path\n");
    return -EINVAL;
  }

  DPRINTF("forwarder: initializing target backend\n");
  Target *target = initializeTargetForImage(image);
  if (!target)
    return -1;

  // Load Neutrino arguments
  DPRINTF("forwarder: loading target arguments\n");
  ArgumentList *globalArguments = calloc(sizeof(ArgumentList), 1);
  ArgumentList *titleArguments = calloc(sizeof(ArgumentList), 1);
  if (!globalArguments || !titleArguments) {
    displayFatalError("Failed to allocate memory for Neutrino arguments\n");
    __builtin_trap();
  }
  loadGlobalNeutrinoArguments(globalArguments, target->device);
  loadTitleNeutrinoArguments(titleArguments, target);
  // Merge title into global (global is base); title wins on duplicate names.
  ArgumentList *arguments = mergeNeutrinoArguments(globalArguments, titleArguments);
  freeArgumentList(globalArguments);
  freeArgumentList(titleArguments);

  DPRINTF("forwarder: launching target\n");
  int res = launchTarget(target, arguments);
  switch (res) {
  case -ENOENT:
    displayFatalError("Neutrino not found\n");
    break;
  case -EINVAL:
    displayFatalError("Unsupported target device\n");
    break;
  default:
    displayFatalError("Failed to launch Neutrino: %d\n", res);
    break;
  }
  freeArgumentList(arguments);
  freeTarget(NULL, target);
  return res;
}
