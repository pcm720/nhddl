#include "options.h"
#include "common.h"
#include "devices.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <libcdvd.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int parseOptionsFile(ArgumentList *result, FILE *file, char deviceNumber);
int loadArgumentList(ArgumentList *options, char *filePath);
void appendArgument(ArgumentList *target, Argument *arg);
Argument *newArgument(char *argName, char *value);
uint32_t getTimestamp();

// Defines all known compatibility modes
const CompatiblityModeMap COMPAT_MODE_MAP[CM_NUM_MODES] = {
    {CM_IOP_FAST_READS, '0', "IOP: Fast reads"},
    {CM_IOP_SYNC_READS, '2', "IOP: Sync reads"},
    {CM_EE_UNHOOK_SYSCALLS, '3', "EE : Unhook syscalls"},
    {CM_IOP_EMULATE_DVD_DL, '5', "IOP: Emulate DVD-DL"},
    {CM_IOP_FIX_BUFFER_OVERRUN, '7', "IOP: Fix game buffer overrun"},
};

const char BASE_CONFIG_PATH[] = "/nhddl";
const size_t BASE_CONFIG_PATH_LEN = sizeof(BASE_CONFIG_PATH) / sizeof(char);

const char globalOptionsPath[] = "/global.yaml";
#define MAX_GLOBAL_OPTS_LEN (MASS_PLACEHOLDER_LEN + BASE_CONFIG_PATH_LEN + (sizeof(globalOptionsPath) / sizeof(char)))

const char lastTitlePath[] = "/lastTitle.bin";
#define MAX_LAST_TITLE_LEN (MASS_PLACEHOLDER_LEN + BASE_CONFIG_PATH_LEN + (sizeof(lastTitlePath) / sizeof(char)))

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
void buildConfigFilePath(char *targetPath, const char *targetMountpoint, const char *targetFileName) {
  if (targetMountpoint[4] == ':') {
    strncpy(targetPath, targetMountpoint, 5);
    targetPath[5] = '\0';
  } else { // Handle numbered devices
    strncpy(targetPath, targetMountpoint, 6);
    targetPath[6] = '\0';
  }

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
  printf("Reading last launched title\n");
  char targetPath[MAX_LAST_TITLE_LEN];
  targetPath[0] = '\0';
  buildConfigFilePath(targetPath, MASS_PLACEHOLDER, lastTitlePath);

  uint32_t max_timestamp = 0;
  uint32_t timestamp = 0;
  size_t fsize = 0;
  for (int i = 0; i < MAX_MASS_DEVICES; i++) {
    if (deviceModeMap[i].mode == MODE_ALL) {
      break;
    }
    targetPath[4] = i + '0';

    // Open last launched title file and read it
    int fd = open(targetPath, O_RDONLY);
    if (fd < 0) {
      printf("WARN: Failed to open last launched title file on device %d: %d\n", i, fd);
      continue;
    }

    // Read file timestamp (first 4 bytes)
    if (read(fd, &timestamp, sizeof(timestamp)) != sizeof(timestamp)) {
      printf("WARN: Failed to read last launched title file on device %d\n", i);
      close(fd);
      continue;
    }
    // Read the rest of the file only if it's newer
    if (timestamp < max_timestamp) {
      close(fd);
      continue;
    }
    max_timestamp = timestamp;

    // Get title path size
    fsize = lseek(fd, 0, SEEK_END) - sizeof(timestamp);
    lseek(fd, sizeof(timestamp), SEEK_SET);
    // Read file contents into titlePath
    if (read(fd, titlePath, fsize) <= 0) {
      close(fd);
      printf("WARN: Failed to read last launched title\n");
      continue;
    }
    close(fd);
    return 0;
  }
  return 0;
}

// Writes last launched title path into lastTitle file on title mountpoint
int updateLastLaunchedTitle(char *titlePath) {
  printf("Writing last launched title as %s\n", titlePath);
  char targetPath[MAX_LAST_TITLE_LEN];
  buildConfigFilePath(targetPath, titlePath, NULL);

  // Make sure config directory exists
  struct stat st;
  if (stat(targetPath, &st) == -1) {
    printf("Creating config directory: %s\n", targetPath);
    mkdir(targetPath, 0777);
  }

  // Append last title file path
  strcat(targetPath, lastTitlePath);

  // Open last launched title file and write the full title path into it
  int fd = open(targetPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    printf("ERROR: Failed to open last launched title file: %d\n", fd);
    return -ENOENT;
  }

  // Write timestamp
  uint32_t timestamp = getTimestamp();
  if (write(fd, &timestamp, sizeof(timestamp)) != sizeof(timestamp)) {
    printf("ERROR: Failed to write last launched title timestamp\n");
    close(fd);
    return -EIO;
  }

  // Write path without the mountpoint
  int mountpointLen = 5;
  if (titlePath[5] == ':') {
    mountpointLen = 6;
  }

  size_t writeLen = strlen(titlePath) + 1 - mountpointLen;
  if (write(fd, titlePath + mountpointLen, writeLen) != writeLen) {
    printf("ERROR: Failed to write last launched title\n");
    close(fd);
    return -EIO;
  }
  close(fd);
  return 0;
}

// Generates ArgumentList from global config file located at targetMounpoint (usually ISO full path)
int getGlobalLaunchArguments(ArgumentList *result, const char *targetMountpoint) {
  char targetPath[MAX_GLOBAL_OPTS_LEN];
  buildConfigFilePath(targetPath, targetMountpoint, globalOptionsPath);
  int ret = loadArgumentList(result, targetPath);

  Argument *curArg = result->first;
  while (curArg != NULL) {
    curArg->isGlobal = 1;
    curArg = curArg->next;
  }
  return ret;
}

// Generates ArgumentList from global and title-specific config file
int getTitleLaunchArguments(ArgumentList *result, Target *target) {
  printf("Looking for title-specific config for %s (%s)\n", target->name, target->id);
  char targetPath[PATH_MAX + 1];
  buildConfigFilePath(targetPath, target->fullPath, NULL);
  // Determine actual title options file from config directory contents
  DIR *directory = opendir(targetPath);
  if (directory == NULL) {
    printf("ERROR: Can't open %s\n", targetPath);
    return -ENOENT;
  }
  targetPath[0] = '\0';

  // Find title config in config directory
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (entry->d_type != DT_DIR) {
      // Find file that starts with ISO name (without the extension)
      if (!strncmp(entry->d_name, target->name, strlen(target->name))) {
        buildConfigFilePath(targetPath, target->fullPath, entry->d_name);
        break;
      }
    }
  }
  closedir(directory);

  if (targetPath[0] == '\0') {
    printf("Title-specific config not found\n");
    return 0;
  }

  // Load arguments
  printf("Loading title-specific config from %s\n", targetPath);
  int ret = loadArgumentList(result, targetPath);
  if (ret) {
    printf("ERROR: Failed to load argument list: %d\n", ret);
  }

  return 0;
}

// Saves title launch arguments to title-specific config file.
// '$' before the argument name is used as 'disabled' flag.
// Empty value means that the argument is empty, but still should be used without the value.
int updateTitleLaunchArguments(Target *target, ArgumentList *options) {
  // Build file path
  char lineBuffer[PATH_MAX + 1];
  buildConfigFilePath(lineBuffer, target->fullPath, target->name);
  strcat(lineBuffer, ".yaml");
  printf("Saving title-specific config to %s\n", lineBuffer);

  // Open file, truncating it
  int fd = open(lineBuffer, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    printf("ERROR: Failed to open file\n");
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
      len = sprintf(lineBuffer, "%s%s: %s\n", (tArg->isDisabled) ? "$" : "", tArg->arg, tArg->value);
    } else if (tArg->isDisabled) {
      len = sprintf(lineBuffer, "$%s:\n", tArg->arg);
    }
    if (len > 0) {
      if ((ret = write(fd, lineBuffer, len)) != len) {
        printf("ERROR: Failed to write to file\n");
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
int loadArgumentList(ArgumentList *options, char *filePath) {
  // Open options file
  FILE *file = fopen(filePath, "r");
  if (file == NULL) {
    printf("ERROR: Failed to open %s\n", filePath);
    return -ENOENT;
  }

  // Initialize ArgumentList
  options->total = 0;
  options->first = NULL;
  options->last = NULL;

  // Get driver device number (will be used by Neutrino)
  char deviceNumber = deviceModeMap[filePath[4] - '0'].index + '0';

  // Parse options file
  if (parseOptionsFile(options, file, deviceNumber)) {
    fclose(file);
    freeArgumentList(options);
    return -EIO;
  }

  fclose(file);
  return 0;
}

// Parses file into ArgumentList. Result may contain parsed arguments even if an error is returned.
// Replaces 'X' in argument values that start with MASS_PLACEHOLDER with the deviceNumber
int parseOptionsFile(ArgumentList *result, FILE *file, char deviceNumber) {
  // Our lines will mostly consist of file paths, which aren't likely to exceed 300 characters due to 255 character limit in exFAT path component
  char lineBuffer[PATH_MAX + 1];
  lineBuffer[0] = '\0';
  int startIdx;
  int substrIdx;
  int argEndIdx;
  int isDisabled = 0;

  while (fgets(lineBuffer, PATH_MAX, file)) { // fgets reutrns NULL if EOF or an error occurs
    startIdx = 0;
    isDisabled = 0;
    argEndIdx = 0;

    //
    // Parse argument
    //
    while (isspace((unsigned char)lineBuffer[startIdx])) {
      startIdx++; // Advance line index until we read a non-whitespace character
    }
    // Ignore comment lines
    if (lineBuffer[startIdx] == '#')
      continue;

    // Try to find ':' until line ends
    substrIdx = startIdx;
    while (lineBuffer[substrIdx] != ':' && lineBuffer[substrIdx] != '\0') {
      if (lineBuffer[substrIdx] == '$') {
        // Handle disabled argument
        isDisabled = 1;
        startIdx = substrIdx + 1;
      } else if (isspace((unsigned char)lineBuffer[startIdx])) {
        // Ignore whitespace by advancing start index to ignore this character
        startIdx = substrIdx + 1;
      }
      substrIdx++;
    }

    // If EOL is reached without finding ':', skip to the next line
    if (lineBuffer[substrIdx] == '\0') {
      goto next;
    }

    // Mark the end of argument name before removing trailing whitespace
    argEndIdx = substrIdx;

    // Remove trailing whitespace
    while (isspace((unsigned char)lineBuffer[substrIdx - 1])) {
      substrIdx--;
    }

    // Copy argument to argName
    char *argName = calloc(sizeof(char), substrIdx - startIdx + 1);
    strncpy(argName, &lineBuffer[startIdx], substrIdx - startIdx);
    substrIdx = argEndIdx;

    //
    // Parse value
    //
    startIdx = substrIdx + 1;
    // Advance line index until we read a non-whitespace character or return at EOL
    while (isspace((unsigned char)lineBuffer[startIdx])) {
      if (lineBuffer[startIdx] == '\0') {
        free(argName);
        goto next;
      }
      startIdx++;
    }

    // Try to read value until we reach a comment, a new line, or the end of string
    substrIdx = startIdx;
    while (lineBuffer[substrIdx] != '#' && lineBuffer[substrIdx] != '\r' && lineBuffer[substrIdx] != '\n' && lineBuffer[substrIdx] != '\0') {
      substrIdx++;
    }

    // Remove trailing whitespace
    while ((substrIdx > startIdx) && isspace((unsigned char)lineBuffer[substrIdx - 1])) {
      substrIdx--;
    }

    Argument *arg = newArgument(argName, NULL);
    arg->isDisabled = isDisabled;

    // Allocate memory for the argument value
    size_t valueLength = substrIdx - startIdx;
    if (!strcmp(COMPAT_MODES_ARG, arg->arg) && valueLength > CM_NUM_MODES + 1) {
      // Always allocate at least (CM_NUM_MODES + 1) bytes for compatibility mode string
      arg->value = calloc(sizeof(char), CM_NUM_MODES + 1);
    } else {
      arg->value = calloc(sizeof(char), valueLength + 1);
    }

    // Copy the value and add argument to the list
    strncpy(arg->value, &lineBuffer[startIdx], valueLength);
    // Replace X in path with the actual device number if argument starts with MASS_PLACEHOLDER
    if (!strncmp(arg->value, MASS_PLACEHOLDER, MASS_PLACEHOLDER_LEN-1)) {
      arg->value[4] = deviceNumber;
    }
    appendArgument(result, arg);

  next:
  }
  if (ferror(file) || !feof(file)) {
    printf("ERROR: Failed to read config file\n");
    return -EIO;
  }

  return 0;
}

// Completely frees Argument and returns pointer to a previous argument in the list
Argument *freeArgument(Argument *arg) {
  Argument *prev = NULL;
  free(arg->arg);
  free(arg->value);
  if (arg->prev != NULL) {
    prev = arg->prev;
  }
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
  copy->arg = strdup(src->arg);
  copy->value = strdup(src->value);
  return copy;
}

// Replaces argument and value in dst, freeing arg and value.
// Keeps next and prev pointers.
void replaceArgument(Argument *dst, Argument *src) {
  // Do a deep copy for argument and value
  free(dst->arg);
  free(dst->value);
  dst->isGlobal = src->isGlobal;
  dst->isDisabled = src->isDisabled;
  dst->arg = strdup(src->arg);
  dst->value = strdup(src->value);
}

// Creates new Argument with passed argName and value (without copying)
Argument *newArgument(char *argName, char *value) {
  Argument *arg = malloc(sizeof(Argument));
  arg->arg = argName;
  arg->value = value;
  arg->isDisabled = 0;
  arg->isGlobal = 0;
  arg->prev = NULL;
  arg->next = NULL;
  return arg;
}

// Appends arg to the end of target
void appendArgument(ArgumentList *target, Argument *arg) {
  target->total++;

  // Always put compatibility mode argument first
  if (!strcmp(COMPAT_MODES_ARG, arg->arg)) {
    arg->next = target->first;
    target->first = arg;
    if (target->last == NULL)
      target->last = arg;
    return;
  }

  if (target->first == NULL) {
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
        // If argument is not a compat mode flag, disabled and has no value
        if (strcmp(COMPAT_MODES_ARG, curArg2->arg) && curArg1->isDisabled && (curArg1->value[0] == '\0')) {
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

// Parses compatibility mode argument value into a bitmask
uint8_t parseCompatModes(char *stringValue) {
  uint8_t result = 0;
  for (int i = 0; i < strlen(stringValue); i++) {
    for (int j = 0; j < CM_NUM_MODES; j++) {
      if (stringValue[i] == COMPAT_MODE_MAP[j].value) {
        result |= COMPAT_MODE_MAP[j].mode;
        break;
      }
    }
  }
  return result;
}

// Stores compatibility mode from bitmask into argument value and sets isDisabled flag accordingly.
// Target must be at least 6 bytes long, including null terminator
void storeCompatModes(Argument *target, uint8_t modes) {
  int pos = 0;

  for (int i = 0; i < CM_NUM_MODES; i++) {
    if (modes & COMPAT_MODE_MAP[i].mode) {
      target->value[pos] = COMPAT_MODE_MAP[i].value;
      pos++;
    }
  }

  target->value[pos] = '\0';

  if (!pos)
    target->isDisabled = 1;
  else
    target->isDisabled = 0;
}

// Inserts a new compat mode arg into the argument list
void insertCompatModeArg(ArgumentList *target, uint8_t modes) {
  Argument *newArg = calloc(sizeof(Argument), 1);
  newArg->arg = strdup(COMPAT_MODES_ARG);

  newArg->value = calloc(sizeof(char), CM_NUM_MODES + 1);
  storeCompatModes(newArg, modes);

  target->total++;

  // Put at the start of the list
  newArg->next = target->first;
  target->first = newArg;
  if (target->last == NULL)
    target->last = newArg;
}

// Loads both global and title launch arguments, returning pointer to a merged list
ArgumentList *loadLaunchArgumentLists(Target *target) {
  int res = 0;
  // Initialize global argument list
  ArgumentList *globalArguments = calloc(sizeof(ArgumentList), 1);
  if ((res = getGlobalLaunchArguments(globalArguments, target->fullPath))) {
    printf("WARN: Failed to load global launch arguments: %d\n", res);
  }
  // Initialize title list and merge global into it
  ArgumentList *titleArguments = calloc(sizeof(ArgumentList), 1);
  if ((res = getTitleLaunchArguments(titleArguments, target))) {
    printf("WARN: Failed to load title arguments: %d\n", res);
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