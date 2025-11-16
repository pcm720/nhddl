#include "options.h"
#include "common.h"
#include "devices.h"
#include "dprintf.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libcdvd.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int loadArgumentList(ArgumentList *options, struct DeviceMapEntry *device, char *filePath);
int parseOptionsFile(ArgumentList *result, FILE *file, struct DeviceMapEntry *device);
void appendArgument(ArgumentList *target, Argument *arg);
uint32_t getTimestamp();

const char BASE_CONFIG_PATH[] = "/nhddl";
const size_t BASE_CONFIG_PATH_LEN = sizeof(BASE_CONFIG_PATH) / sizeof(char);

const char globalOptionsPath[] = "/global.yaml";
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
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (deviceModeMap[i].mode == MODE_NONE || deviceModeMap[i].mountpoint == NULL) {
      break;
    }

    if (deviceModeMap[i].metadev) // Fallback to metadata device if set
      buildConfigFilePath(targetPath, deviceModeMap[i].metadev->mountpoint, lastTitlePath);
    else
      buildConfigFilePath(targetPath, deviceModeMap[i].mountpoint, lastTitlePath);

    // Open last launched title file and read it
    int fd = open(targetPath, O_RDONLY);
    if (fd < 0) {
      DPRINTF("WARN: Failed to open last launched title file on device %s: %d\n", deviceModeMap[i].mountpoint, fd);
      continue;
    }

    // Read file timestamp (first 4 bytes)
    if (read(fd, &timestamp, sizeof(timestamp)) != sizeof(timestamp)) {
      DPRINTF("WARN: Failed to read last launched title file on device %s\n", deviceModeMap[i].mountpoint);
      close(fd);
      continue;
    }
    // Read the rest of the file only if it's newer
    if (timestamp < maxTimestamp) {
      close(fd);
      continue;
    }
    maxTimestamp = timestamp;

    // Get title path size
    fsize = lseek(fd, 0, SEEK_END) - sizeof(timestamp);
    lseek(fd, sizeof(timestamp), SEEK_SET);
    // Read file contents into titlePath
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
int updateLastLaunchedTitle(struct DeviceMapEntry *device, char *titlePath) {
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

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
int getGlobalLaunchArguments(ArgumentList *result, struct DeviceMapEntry *device) {
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
  struct DeviceMapEntry *device = target->device;
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
  while ((entry = readdir(directory)) != NULL) {
    if (entry->d_type != DT_DIR) {
      // Find file that starts with ISO name (without the extension)
      if (!strncmp(entry->d_name, target->name, strlen(target->name))) {
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
// '$' before the argument name is used as 'disabled' flag.
// Empty value means that the argument is empty, but still should be used without the value.
int updateTitleLaunchArguments(Target *target, ArgumentList *options) {
  struct DeviceMapEntry *device = target->device;
  if (device->metadev) { // Fallback to metadata device if set
    device = device->metadev;
  }

  // Build file path
  char lineBuffer[PATH_MAX + 1];
  buildConfigFilePath(lineBuffer, device->mountpoint, target->name);
  strcat(lineBuffer, ".yaml");
  DPRINTF("Saving title-specific config to %s\n", lineBuffer);

  // Open file, truncating it
  int fd = open(lineBuffer, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    DPRINTF("ERROR: Failed to open file\n");
    return fd;
  }

  // Write each argument into the file
  lineBuffer[0] = '\0'; // reuse buffer
  int len = 0;
  int ret = 0;
  Argument *tArg = options->first;
  while (tArg != NULL) {
    len = 0;
    // Skip enabled global arguments
    // Write disabled global arguments as disabled empty arguments
    if (!tArg->isGlobal) {
      // Check if arg is a file path and trim mountpoint
      len = getRelativePathIdx(tArg->value);
      if (len > 0)
        len = sprintf(lineBuffer, "%s%s: %s\n", (tArg->isDisabled) ? "$" : "", tArg->arg, &tArg->value[len]);
      else
        len = sprintf(lineBuffer, "%s%s: %s\n", (tArg->isDisabled) ? "$" : "", tArg->arg, tArg->value);
    } else if (tArg->isDisabled) {
      len = sprintf(lineBuffer, "$%s:\n", tArg->arg);
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

// Parses options file into ArgumentList
int loadArgumentList(ArgumentList *options, struct DeviceMapEntry *device, char *filePath) {
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

// Parses file into ArgumentList. Result may contain parsed arguments even if an error is returned.
// Adds mountpoint with the deviceNumber to arguments values that start with \ or /
int parseOptionsFile(ArgumentList *result, FILE *file, struct DeviceMapEntry *device) {
  // Our lines will mostly consist of file paths, which aren't likely to exceed 300 characters due to 255 character limit in exFAT path component
  char lineBuffer[PATH_MAX + 1];
  lineBuffer[0] = '\0';
  int isDisabled = 0;
  char *valuePtr = NULL;
  char *argPtr = NULL;

  while (fgets(lineBuffer, PATH_MAX, file)) { // fgets reutrns NULL if EOF or an error occurs
    argPtr = lineBuffer;
    while (isspace((int)*argPtr))
      argPtr++; // Advance argument until the first non-whitespace character

    if (argPtr[0] == '#') // Ignore commented lines
      continue;

    // Find the start of the value
    valuePtr = strchr(lineBuffer, ':');
    if (!valuePtr)
      continue;

    // Terminate the string argPtr points to at the argument name
    *valuePtr = '\0';

    // Trim whitespace and terminate the value
    do {
      valuePtr++;
    } while (isspace((int)*valuePtr));
    valuePtr[strcspn(valuePtr, "#\r\n")] = '\0'; // Terminate the value at the line end or comment token

    // Trim whitespace at the end of argument and value strings
    char *tempPtr = argPtr + strlen(argPtr) - 1;
    while (isspace((int)*tempPtr)) {
      *tempPtr = '\0';
      tempPtr--;
    }
    tempPtr = valuePtr + strlen(valuePtr);
    while (isspace((int)*tempPtr)) {
      *tempPtr = '\0';
      tempPtr--;
    }

    char *newValue = NULL;
    if (!device && (valuePtr[0] == '/' || valuePtr[0] == '\\')) {
      // Add device mountpoint to argument value if path starts with \ or /
      char *newValue = calloc(sizeof(char), strlen(valuePtr) + 1 + strlen(device->mountpoint));
      // Replace current mountpoint with device number.
      strcpy(newValue, device->mountpoint);
      strcat(newValue, valuePtr);
    }

    if (argPtr[0] == '$') {
      argPtr++;
      isDisabled = 1;
    } else
      isDisabled = 0;

    Argument *arg = NULL;
    if (newValue) {
      arg = newArgument(argPtr, newValue);
      free(newValue);
    } else
      arg = newArgument(argPtr, valuePtr);

    arg->isDisabled = isDisabled;
    appendArgument(result, arg);
  }
  if (ferror(file) || !feof(file)) {
    DPRINTF("ERROR: Failed to read config file\n");
    return -EIO;
  }

  return 0;
}

// Completely frees Argument and returns pointer to a previous argument in the list
Argument *freeArgument(Argument *arg) {
  Argument *prev = NULL;
  if (arg->arg)
    free(arg->arg);
  if (arg->value)
    free(arg->value);
  if (arg->prev)
    prev = arg->prev;

  free(arg);
  return prev;
}

// Completely frees ArgumentList. Passed pointer will not be valid after this function executes
void freeArgumentList(ArgumentList *result) {
  Argument *tArg = result->last;
  while (tArg != NULL) {
    tArg = freeArgument(tArg);
  }
  result->first = NULL;
  result->last = NULL;
  result->total = 0;
  free(result);
}

// Makes and returns a deep copy of src without prev/next pointers.
Argument *copyArgument(Argument *src) {
  // Do a deep copy for argument and value
  Argument *copy = calloc(sizeof(Argument), 1);
  copy->isGlobal = src->isGlobal;
  copy->isDisabled = src->isDisabled;
  if (src->arg)
    copy->arg = strdup(src->arg);
  if (src->value)
    copy->value = strdup(src->value);
  return copy;
}

// Replaces argument and value in dst, freeing arg and value.
// Keeps next and prev pointers.
void replaceArgument(Argument *dst, Argument *src) {
  // Do a deep copy for argument and value
  if (dst->arg)
    free(dst->arg);
  if (dst->value)
    free(dst->value);
  dst->isGlobal = src->isGlobal;
  dst->isDisabled = src->isDisabled;
  if (src->arg)
    dst->arg = strdup(src->arg);
  if (src->value)
    dst->value = strdup(src->value);
}

// Creates new Argument with passed argName and value.
// Copies both argName and value
Argument *newArgument(const char *argName, char *value) {
  Argument *arg = malloc(sizeof(Argument));
  arg->isDisabled = 0;
  arg->isGlobal = 0;
  arg->prev = NULL;
  arg->next = NULL;
  if (argName)
    arg->arg = strdup(argName);
  if (value)
    arg->value = strdup(value);

  return arg;
}

// Appends arg to the end of target
void appendArgument(ArgumentList *target, Argument *arg) {
  target->total++;

  if (!target->first) {
    target->first = arg;
  } else {
    target->last->next = arg;
    arg->prev = target->last;
  }
  target->last = arg;
}

// Does a deep copy of arg and inserts it into target.
// Always places COMPAT_MODES_ARG on the top of the list
void appendArgumentCopy(ArgumentList *target, Argument *arg) {
  // Do a deep copy for argument and value
  Argument *copy = copyArgument(arg);
  appendArgument(target, copy);
}

// Merges two lists into one, ignoring arguments in the second list that already exist in the first list.
// All arguments merged from the second list are a deep copy of arguments in source lists.
// Expects both lists to be initialized.
void mergeArgumentLists(ArgumentList *list1, ArgumentList *list2) {
  Argument *curArg1;
  Argument *curArg2 = list2->first;
  int isDuplicate = 0;

  // Copy arguments from the second list into result
  while (curArg2 != NULL) {
    isDuplicate = 0;
    // Look for duplicate arguments in the first list
    curArg1 = list1->first;
    while (curArg1 != NULL) {
      // If result already contains argument with the same name, skip it
      if (!strcmp(curArg2->arg, curArg1->arg)) {
        isDuplicate = 1;
        // If argument is disabled and has no value
        if (curArg1->isDisabled && (curArg1->value[0] == '\0')) {
          // Replace element in list1 with disabled element from list2
          replaceArgument(curArg1, curArg2);
          curArg1->isDisabled = 1;
        }
        break;
      }
      curArg1 = curArg1->next;
    }
    // If no duplicate was found, insert the argument
    if (!isDuplicate) {
      appendArgumentCopy(list1, curArg2);
    }
    curArg2 = curArg2->next;
  }
}

// Retrieves argument from the list
Argument *getArgument(ArgumentList *target, const char *argumentName) {
  Argument *arg = target->first;
  while (arg != NULL) {
    if (!strcmp(arg->arg, argumentName)) {
      return arg;
    }
    arg = arg->next;
  }
  return NULL;
}

// Creates new argument and inserts it into the list
Argument *insertArgument(ArgumentList *target, const char *argumentName, char *value) {
  Argument *arg = newArgument(argumentName, value);
  appendArgument(target, arg);
  return arg;
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

// Generates 32-bit timestamp from RTC.
// Will wrap around every 64th year
uint32_t getTimestamp() {
  // Initialize libcdvd to get timestamp
  if (sceCdInit(SCECdINoD)) {
    // Read clock
    sceCdCLOCK time;
    sceCdReadClock(&time);
    sceCdInit(SCECdEXIT);

    // Pack date into 32-bit timestamp
    // Y   26 M 22 D  17 H  12 M    6 S    0
    // 111111 1111 11111 11111 111111 111111
    uint32_t sum = ((uint32_t)btoi(time.year)) << 26 |        // Year
                   ((uint32_t)btoi(time.month) & 0xF) << 22 | // Month
                   ((uint32_t)btoi(time.day)) << 17 |         // Day
                   ((uint32_t)btoi(time.hour)) << 12 |        // Hour
                   ((uint32_t)btoi(time.minute)) << 6 |       // Minute
                   (btoi(time.second) & 0x3F);                // Second
    return sum;
  }
  return 0;
}
