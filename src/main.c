#include "backends/backends.h"
#include "backends/target.h"
#include "backends/title_id.h"
#include "config/arguments.h"
#include "config/config.h"
#include "config/nhddl.h"
#include "devices/devices.h"
#include "devices/hdd.h"
#include "devices/utils.h"
#include "dprintf.h"
#include "forwarder.h"
#include "neutrino/neutrino.h"
#include "ui/ui.h"
#include <ctype.h>
#include <debug.h>
#include <fcntl.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Attempts to detect root device and load device drivers required for accessing CWD
// Always reboots IOP. Returns 0 on success.
int resolveRootDevice(char *argv0);

void uiMain() {
  displayFatalError("main: UI not yet implemented\n");
  __builtin_trap();
}

int main(int argc, char *argv[]) {
  DPRINTF("*************\nNHDDL %s\nA Neutrino launcher by pcm720\n*************\n", GIT_VERSION);

  for (int i = 0; i < argc; i++)
    DPRINTF("argv[%d] = %s\n", i, argv[i]);

  // Parse arguments
  if ((argc > 0 && argv[0][0] == '-') || (argc > 1 && argv[1][0] == '-'))
    parseArgv(argc, argv);

  int res;
  if (getImage() && getEnabledDevices()) {
    // Forward ISO to Neutrino
    res = forwardBoot();
    DPRINTF("main: error: failed to forward to Neutrino: %d\n", res);
    goto fail;
  }

  // Initialize UI
  DPRINTF("main: resolving NHDDL root\n");
  res = resolveRootDevice(argv[0]);
  if (res == -ENXIO) {
    displayFatalError("Failed to initialize modules\n");
    goto fail;
  }
  if (!res)
    // Load options file
    loadOptions();

  DPRINTF("main: starting UI\n");
  uiMain();

fail:
  cleanupRootMount();
  cleanupAllBackends();
  sleep(10);
  return 1;
}

// Attempts to detect root device and load device drivers required for accessing CWD
// Always reboots IOP when argv0 doesn't point to host. Returns 0 on success, -ENXIO on critical error.
int resolveRootDevice(char *argv0) {
  int res = 0;
  if (!argv0) {
    res = -EINVAL;
    goto reboot;
  }

  // Get the last slash location and truncate argv0 at the last slash to get CWD
  char cwd[PATH_MAX] = {0};
  strncpy(cwd, argv0, PATH_MAX);
  char *temp = strrchr(cwd, '/');
  if (!temp) {
    res = -EINVAL;
    goto reboot;
  }
  *(++temp) = '\0';

  if (!strncmp(cwd, "host", 4)) {
    DPRINTF("main: using host path\n");
    // For host: paths, just set the root path and return
    setNHDDLRoot(cwd);
    return 0;
  }

  DeviceType device = guessDeviceType(cwd);
  if (device == Device_None) {
    DPRINTF("main: couldn't guess the device from CWD\n");
    res = -ENODEV;
    goto reboot;
  }

  if (device != Device_BDM) {
    setNHDDLRoot(cwd);
    DPRINTF("main: guessed root device from CWD: %s\n", getDeviceString(device));
    goto reboot;
  }

  // Generic BDM paths (mass?:) need more involved probing
  // Try accessing argv0 first
  DIR *dir = opendir(cwd);
  if (dir) {
    // CWD is available
    closedir(dir);

    // Set device root, trying to guess CWD for BDM
    temp = guessCWDDevice(cwd, &device);
    if (temp) {
      setNHDDLRoot(temp);
      free(temp);

      DPRINTF("main: guessed root device from CWD: %s\n", getDeviceString(device));
      goto reboot;
    }
  }

  DPRINTF("main: CWD is unavailable, rebooting IOP and iterating over supported devices\n");
  if (rebootIOP())
    return -ENXIO;

  DeviceType probeTargets[] = {Device_USB, Device_ATA, Device_MX4SIO};
  for (int i = 0; i < sizeof(probeTargets) / sizeof(DeviceType); i++) {
    DPRINTF("main: probing %s\n", getDeviceString(probeTargets[i]));
    loadDeviceModules(probeTargets[i]);
    // Wait for drivers to init and check if path exists
    if ((temp = probeCanonicalPath(argv0, probeTargets[i]))) {
      char *dirPath = strrchr(temp, '/');
      if (dirPath)
        // Truncate path at directory to get CWD
        *(++dirPath) = '\0';

      setNHDDLRoot(temp);
      free(temp);
      DPRINTF("main: guessed root device from CWD: %s\n", getDeviceString(probeTargets[i]));
      return 0;
    }
    free(temp);
  }

  return -1;
reboot:
  if (rebootIOP())
    return -ENXIO;

  return res;
}
