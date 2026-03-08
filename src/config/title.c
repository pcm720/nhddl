#include "config/title.h"
#include "backends/backends.h"
#include "devices/utils.h"
#include "dprintf.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char BASE_CONFIG_PATH[] = "/nhddl";
const size_t BASE_CONFIG_PATH_LEN = sizeof(BASE_CONFIG_PATH) / sizeof(char);

const char globalOptionsPath[] = "/global.cnf";
const char lastTitlePath[] = "/lastTitle.bin";

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
void buildConfigFilePath(char *targetPath, const char *targetMountpoint, const char *targetFileName) {
  strcpy(targetPath, targetMountpoint);
  strcat(targetPath, BASE_CONFIG_PATH); // Append base config path
  if (targetFileName != NULL) {
    // Append / to path if targetFileName doesn't have it already
    if (targetFileName[0] != '/')
      strcat(targetPath, "/");

    strcat(targetPath, targetFileName); // Append target file name
  }
}

// Gets last launched title path into titlePath
// Searches for the latest file across all mounted BDM devices
int getLastLaunchedTitle(char *titlePath) {
  DPRINTF("Reading last launched title\n");
  char targetPath[PATH_MAX];
  targetPath[0] = '\0';

  uint32_t maxTimestamp = 0;
  uint32_t timestamp = 0;
  size_t fsize = 0;
  for (int i = 0; i < getBackendDeviceCount(); i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (!dev || dev->type == Device_None || dev->mountpoint == NULL)
      break;
    if (dev->metadev)
      buildConfigFilePath(targetPath, dev->metadev->mountpoint, lastTitlePath);
    else
      buildConfigFilePath(targetPath, dev->mountpoint, lastTitlePath);

    int fd = open(targetPath, O_RDONLY);
    if (fd < 0) {
      DPRINTF("WARN: Failed to open last launched title file on device %s: %d\n", dev->mountpoint, fd);
      continue;
    }
    if (read(fd, &timestamp, sizeof(timestamp)) != sizeof(timestamp)) {
      DPRINTF("WARN: Failed to read last launched title file on device %s\n", dev->mountpoint);
      close(fd);
      continue;
    }
    if (timestamp < maxTimestamp) {
      close(fd);
      continue;
    }
    maxTimestamp = timestamp;
    fsize = lseek(fd, 0, SEEK_END) - sizeof(timestamp);
    lseek(fd, sizeof(timestamp), SEEK_SET);
    if (read(fd, titlePath, fsize) <= 0) {
      close(fd);
      DPRINTF("WARN: Failed to read last launched title\n");
      continue;
    }
    close(fd);
  }
  if (targetPath[0] == '\0')
    return -ENOENT;
  return 0;
}

// Writes last launched title path into lastTitle file on title mountpoint
int updateLastLaunchedTitle(struct BackendDevice *device, char *titlePath) {
  if (device->metadev)
    device = device->metadev;

  DPRINTF("Writing last launched title as %s\n", titlePath);
  char targetPath[PATH_MAX];
  buildConfigFilePath(targetPath, device->mountpoint, NULL);

  // Make sure config directory exists
  struct stat st;
  if (stat(targetPath, &st) == -1) {
    DPRINTF("Creating config directory: %s\n", targetPath);
    mkdir(targetPath, 0777);
  }

  // Append last title file path
  strcat(targetPath, lastTitlePath);

  // Open last launched title file and write the full title path into it
  int fd = open(targetPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    DPRINTF("ERROR: Failed to open last launched title file: %d\n", fd);
    return -ENOENT;
  }

  // Write timestamp
  uint32_t timestamp = getTimestamp();
  if (write(fd, &timestamp, sizeof(timestamp)) != sizeof(timestamp)) {
    DPRINTF("ERROR: Failed to write last launched title timestamp\n");
    close(fd);
    return -EIO;
  }

  // Write path without the mountpoint
  int mountpointLen = getRelativePathIdx(titlePath);
  if (mountpointLen < 0)
    mountpointLen = 0; // Write path as-is

  size_t writeLen = strlen(titlePath) + 1 - mountpointLen;
  if (write(fd, titlePath + mountpointLen, writeLen) != writeLen) {
    DPRINTF("ERROR: Failed to write last launched title\n");
    close(fd);
    return -EIO;
  }
  close(fd);
  return 0;
}

// Generates ArgumentList from global config file located at targetMounpoint (usually ISO full path)
int getGlobalLaunchArguments(ArgumentList *result, struct BackendDevice *device) {
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

  char targetPath[PATH_MAX];
  buildConfigFilePath(targetPath, device->mountpoint, globalOptionsPath);
  int ret = loadArgumentList(result, device, targetPath);
  Argument *curArg = result->first;
  while (curArg != NULL) {
    curArg->isGlobal = 1;
    curArg = curArg->next;
  }
  return ret;
}

// Generates ArgumentList from global and title-specific config file
int getTitleLaunchArguments(ArgumentList *result, Target *target) {
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

// Saves title launch arguments to title-specific config file.
// CNF format: one argument per line as -name=value or -name; disabled entries as #-name=value or #-name.
// Empty value means that the argument is empty, but still should be used without the value.
int updateTitleLaunchArguments(Target *target, ArgumentList *options) {
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
    // Skip enabled global arguments
    // Write disabled global arguments as disabled lines
    if (!tArg->isGlobal) {
      // Check if arg is a file path and trim mountpoint
      int relIdx = tArg->value ? getRelativePathIdx(tArg->value) : -1;
      const char *valStr = (tArg->value && relIdx > 0) ? &tArg->value[relIdx] : (tArg->value ? tArg->value : "");
      if (tArg->isDisabled) {
        len = sprintf(lineBuffer, "#-%s%s%s\n", tArg->arg, valStr[0] ? "=" : "", valStr);
      } else {
        len = sprintf(lineBuffer, "-%s%s%s\n", tArg->arg, valStr[0] ? "=" : "", valStr);
      }
    } else if (tArg->isDisabled) {
      len = sprintf(lineBuffer, "#-%s\n", tArg->arg);
    }
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

// Parses file into ArgumentList. Result may contain parsed arguments even if an error is returned.
// CNF format: one argument per line as -name=value or -name; # starts comments; # -name=value is disabled.
// Adds mountpoint with the deviceNumber to arguments values that start with \ or /
static int parseOptionsFile(ArgumentList *result, FILE *file, struct BackendDevice *device) {
  // Our lines will mostly consist of file paths, which aren't likely to exceed 300 characters due to 255 character limit in exFAT path component
  char lineBuffer[PATH_MAX + 1];
  lineBuffer[0] = '\0';
  int isDisabled = 0;
  char *valuePtr = NULL;
  char *argPtr = NULL;

  while (fgets(lineBuffer, PATH_MAX, file)) { // fgets returns NULL if EOF or an error occurs
    argPtr = lineBuffer;
    while (isspace((int)*argPtr))
      argPtr++; // Advance until the first non-whitespace character

    // Trim trailing whitespace and newline
    char *lineEnd = argPtr + strlen(argPtr);
    while (lineEnd > argPtr && (isspace((int)lineEnd[-1]) || lineEnd[-1] == '\r'))
      *--lineEnd = '\0';

    if (argPtr[0] == '\0') // Skip empty lines
      continue;

    // Ignore lines that don't start with - or #
    if (argPtr[0] != '-' && argPtr[0] != '#')
      continue;

    if (argPtr[0] == '#') {
      // Skip optional whitespace after #
      char *p = argPtr + 1;
      while (isspace((int)*p))
        p++;
      if (*p != '-') // Comment line, not a disabled argument
        continue;
      // Disabled argument: parse from the -
      argPtr = p;
      isDisabled = 1;
    } else {
      isDisabled = 0;
    }

    // argPtr now points to -name=value or -name
    if (argPtr[0] != '-')
      continue;
    argPtr++; // Skip leading -
    valuePtr = strchr(argPtr, '=');
    if (valuePtr) {
      *valuePtr = '\0';
      valuePtr++;
      // Trim value
      while (isspace((int)*valuePtr))
        valuePtr++;
      valuePtr[strcspn(valuePtr, "#\r\n")] = '\0';
      char *vEnd = valuePtr + strlen(valuePtr);
      while (vEnd > valuePtr && isspace((int)vEnd[-1]))
        *--vEnd = '\0';
    } else {
      valuePtr = (char *)"";
    }

    // Trim trailing whitespace from arg name
    char *argEnd = argPtr + strlen(argPtr);
    while (argEnd > argPtr && isspace((int)argEnd[-1]))
      *--argEnd = '\0';

    if (argPtr[0] == '\0') // Empty arg name
      continue;

    char *newValue = NULL;
    if (device && valuePtr[0] != '\0' && (valuePtr[0] == '/' || valuePtr[0] == '\\')) {
      // Add device mountpoint to argument value if path starts with \ or /
      newValue = calloc(sizeof(char), strlen(valuePtr) + 1 + strlen(device->mountpoint));
      strcpy(newValue, device->mountpoint);
      strcat(newValue, valuePtr);
    }

    Argument *arg = NULL;
    if (newValue) {
      arg = newArgument(argPtr, newValue);
      free(newValue);
    } else
      arg = newArgument(argPtr, valuePtr[0] != '\0' ? valuePtr : NULL);

    arg->isDisabled = isDisabled;
    appendArgument(result, arg);
  }
  if (ferror(file) || !feof(file)) {
    DPRINTF("ERROR: Failed to read config file\n");
    return -EIO;
  }

  return 0;
}

// Parses options file into ArgumentList
int loadArgumentList(ArgumentList *options, struct BackendDevice *device, char *filePath) {
  // Open options file
  FILE *file = fopen(filePath, "r");
  if (file == NULL) {
    DPRINTF("ERROR: Failed to open %s\n", filePath);
    return -ENOENT;
  }

  // Initialize ArgumentList
  options->total = 0;
  options->first = NULL;
  options->last = NULL;

  // Parse options file
  if (parseOptionsFile(options, file, device)) {
    fclose(file);
    freeArgumentList(options);
    return -EIO;
  }

  fclose(file);
  return 0;
}

// Loads both global and title launch arguments, returning pointer to a merged list
ArgumentList *loadLaunchArgumentLists(Target *target) {
  int res = 0;
  // Initialize global argument list
  ArgumentList *globalArguments = calloc(sizeof(ArgumentList), 1);
  if ((res = getGlobalLaunchArguments(globalArguments, target->device))) {
    DPRINTF("WARN: Failed to load global launch arguments: %d\n", res);
  }
  // Initialize title list and merge global into it
  ArgumentList *titleArguments = calloc(sizeof(ArgumentList), 1);
  if ((res = getTitleLaunchArguments(titleArguments, target))) {
    DPRINTF("WARN: Failed to load title arguments: %d\n", res);
  }

  if (titleArguments->total != 0) {
    // Merge lists
    mergeArgumentLists(titleArguments, globalArguments);
    freeArgumentList(globalArguments);
    return titleArguments;
  }
  // If there are no title arguments, use global arguments directly
  free(titleArguments);
  return globalArguments;
}
