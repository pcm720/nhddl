#include "config/neutrino_args.h"
#include "backends/backends.h"
#include "config/common.h"
#include "devices/utils.h"
#include "dprintf.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char globalOptionsPath[] = "/global.cnf";
int loadGlobalNeutrinoArguments(ArgumentList *result, struct BackendDevice *device) {
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

  char targetPath[PATH_MAX];
  buildConfigFilePath(targetPath, device->mountpoint, globalOptionsPath);
  int ret = loadArgumentList(result, device, targetPath);
  return ret;
}

// Loads ArgumentList from title-specific config file only (not global).
int loadTitleNeutrinoArguments(ArgumentList *result, Target *target) {
  struct BackendDevice *device = target->device;
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

  DPRINTF("Looking for title-specific config for %s (%s)\n", target->name, target->id);
  char targetPath[PATH_MAX + 1];
  buildConfigFilePath(targetPath, device->mountpoint, NULL);
  // Determine actual title options file from config directory contents
  DIR *directory = opendir(targetPath);
  if (directory == NULL) {
    DPRINTF("ERROR: Can't open %s\n", targetPath);
    return -ENOENT;
  }
  targetPath[0] = '\0';

  // Find title config in config directory
  struct dirent *entry;
  size_t nameLen = strlen(target->name);
  while ((entry = readdir(directory)) != NULL) {
    if (entry->d_type != DT_DIR) {
      // Find file that starts with ISO name (without the extension) and ends with .cnf
      size_t dlen = strlen(entry->d_name);
      const char *cnfExt = ".cnf";
      size_t cnfLen = strlen(cnfExt);
      if (nameLen <= dlen && !strncmp(entry->d_name, target->name, nameLen) && dlen >= cnfLen && !strcmp(entry->d_name + dlen - cnfLen, cnfExt)) {
        buildConfigFilePath(targetPath, device->mountpoint, entry->d_name);
        break;
      }
    }
  }
  closedir(directory);

  if (targetPath[0] == '\0') {
    DPRINTF("Title-specific config not found\n");
    return 0;
  }

  // Load arguments
  DPRINTF("Loading title-specific config from %s\n", targetPath);
  int ret = loadArgumentList(result, device, targetPath);
  if (ret) {
    DPRINTF("ERROR: Failed to load argument list: %d\n", ret);
  }

  return 0;
}

// Saves title Neutrino arguments to title-specific config file.
// Accepts only the title list; writes every argument in the list to the title .cnf.
// CNF format: one argument per line as -name=value or -name; disabled entries as #-name=value or #-name.
int saveTitleNeutrinoArguments(Target *target, ArgumentList *options) {
  struct BackendDevice *device = target->device;
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

  // Build file path
  char lineBuffer[PATH_MAX + 1];
  buildConfigFilePath(lineBuffer, device->mountpoint, target->name);
  strcat(lineBuffer, ".cnf");
  DPRINTF("Saving title-specific config to %s\n", lineBuffer);

  // Open file, truncating it
  int fd = open(lineBuffer, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    DPRINTF("ERROR: Failed to open file\n");
    return fd;
  }

  // Write each argument into the file in CNF format
  lineBuffer[0] = '\0'; // reuse buffer
  int len = 0;
  int ret = 0;
  Argument *tArg = options->first;
  while (tArg != NULL) {
    len = 0;
    int relIdx = tArg->value ? getRelativePathIdx(tArg->value) : -1;
    const char *valStr = (tArg->value && relIdx > 0) ? &tArg->value[relIdx] : (tArg->value ? tArg->value : "");
    len = sprintf(lineBuffer, "%s-%s%s%s\n", (tArg->isDisabled) ? "#" : "", tArg->arg, (valStr[0]) ? "=" : "", valStr);
    if (len > 0) {
      if ((ret = write(fd, lineBuffer, len)) != len) {
        DPRINTF("ERROR: Failed to write to file\n");
        goto out;
      }
    }
    tArg = tArg->next;
  }
out:
  close(fd);
  return ret;
}

// Saves global Neutrino arguments to global.cnf on device.
// Same CNF format as title save. Writes every argument in options.
int saveGlobalNeutrinoArguments(struct BackendDevice *device, ArgumentList *options) {
  if (device->metadev)
    device = device->metadev;

  char targetPath[PATH_MAX + 1];
  buildConfigFilePath(targetPath, device->mountpoint, NULL);

  struct stat st;
  if (stat(targetPath, &st) == -1) {
    DPRINTF("Creating config directory: %s\n", targetPath);
    mkdir(targetPath, 0777);
  }

  buildConfigFilePath(targetPath, device->mountpoint, globalOptionsPath);
  DPRINTF("Saving global config to %s\n", targetPath);

  int fd = open(targetPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    DPRINTF("ERROR: Failed to open file\n");
    return fd;
  }

  char lineBuffer[PATH_MAX + 1];
  lineBuffer[0] = '\0';
  int len = 0;
  int ret = 0;
  Argument *tArg = options->first;
  while (tArg != NULL) {
    len = 0;
    int relIdx = tArg->value ? getRelativePathIdx(tArg->value) : -1;
    const char *valStr = (tArg->value && relIdx > 0) ? &tArg->value[relIdx] : (tArg->value ? tArg->value : "");
    len = sprintf(lineBuffer, "%s-%s%s%s\n", (tArg->isDisabled) ? "#" : "", tArg->arg, (valStr[0]) ? "=" : "", valStr);
    if (len > 0) {
      if ((ret = write(fd, lineBuffer, len)) != len) {
        DPRINTF("ERROR: Failed to write to file\n");
        goto out_global;
      }
    }
    tArg = tArg->next;
  }
out_global:
  close(fd);
  return ret;
}

// Can be used to merge global and per-title Neutrino arguments for display or launch.
// src is base; dst wins on duplicate names.
ArgumentList *mergeNeutrinoArguments(ArgumentList *dst, ArgumentList *src) {
  ArgumentList *merged = calloc(sizeof(ArgumentList), 1);
  if (!merged) {
    displayFatalError("Failed to allocate memory for merged argument list\n");
    __builtin_trap();
  }
  Argument *cur = src->first;
  while (cur != NULL) {
    appendArgumentCopy(merged, cur);
    cur = cur->next;
  }
  mergeArgumentLists(merged, dst);
  return merged;
}
