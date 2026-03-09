#include "backends/backends.h"
#include "config/config.h"
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <string.h>

// Neutrino ELF path relative to CWD
static const char neutrinoCWDPath[] = "neutrino.elf";
// Neutrino ELF path relative to storage root
static char neutrinoRootPath[] = "/neutrino/neutrino.elf";

// Tests if file exists by opening it
int tryFile(const char *filepath) {
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    return fd;
  }
  close(fd);
  return 0;
}

// Attempts to locate neutrino.elf at current path or one of fallback paths
// Sets Neutrino path in config and returns 0 on success.
int findNeutrinoELF() {
  // If path is set in config and the file exists, use it first
  const char *configPath = getNeutrinoPath();
  if (configPath && !tryFile((char *)configPath))
    return 0;

  // Try CWD next
  char neutrinoPath[PATH_MAX] = {0};

  const char *cwd = getNHDDLRoot();
  if (cwd) {
    // If path is valid, try it
    snprintf(neutrinoPath, PATH_MAX, "%s%s", cwd, neutrinoCWDPath);
    if (!tryFile(neutrinoPath)) {
      setNeutrinoPath(neutrinoPath);
      return 0;
    }

    // Try the root of CWD device
    neutrinoPath[0] = '\0';
    char *cwdMount = strchr(cwd, ':');
    if (++cwdMount) {
      strncpy(neutrinoPath, cwd, cwdMount - cwd);
      neutrinoPath[cwdMount - cwd] = '\0';
      strcat(neutrinoPath, neutrinoRootPath);
      if (!tryFile(neutrinoPath)) {
        setNeutrinoPath(neutrinoPath);
        return 0;
      }
    }
  }

  // Try enabled backends
  struct BackendDevice *device;
  int n = getBackendDeviceCount();
  for (int i = 0; i < n; i++) {
    neutrinoPath[0] = '\0';
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (!dev || dev->type == Device_None)
      break;

    if (dev->metadev)
      device = dev->metadev;
    else
      device = dev;
    if (device->mountpoint) {
      snprintf(neutrinoPath, PATH_MAX, "%s%s", device->mountpoint, neutrinoRootPath);
      if (!tryFile(neutrinoPath)) {
        setNeutrinoPath(neutrinoPath);
        return 0;
      }
    }
  }

  return -ENOENT;
}

// Reads version.txt for located Neutrino
// Tries to locate Neutrino if Neutrino path is unknown.
// Returns empty string if the file could not be read
char *getNeutrinoVersion() {
  const char *neutrinoPath = getNeutrinoPath();
  if (!neutrinoPath && findNeutrinoELF())
    return strdup("not found");
  neutrinoPath = getNeutrinoPath();

  // Get full path to Neutrino directory
  const char *slashIdx = strrchr(neutrinoPath, '/');
  if (slashIdx == NULL)
    return strdup("");

  // Get the length of directory path
  int len = slashIdx - neutrinoPath;

  // Build path to version.txt
  char versionFilePath[PATH_MAX];
  strncpy(versionFilePath, neutrinoPath, len);
  versionFilePath[len] = '\0';
  strcat(versionFilePath, "/version.txt");

  // Open version.txt
  FILE *file = fopen(versionFilePath, "r");
  if (file == NULL)
    return strdup("");

  // Read the first line into versionFilePath, reusing it
  if (fgets(&versionFilePath[0], sizeof(versionFilePath) - 1, file) == NULL) {
    fclose(file);
    return strdup("");
  }

  fclose(file);

  // Trim newline
  len = strlen(versionFilePath);
  if (len > 0 && versionFilePath[len - 1] == '\n') {
    versionFilePath[len - 1] = '\0';
  }

  return strdup(versionFilePath);
}
