#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "options.h"

const char baseConfigPath[] = "/config";
const char globalOptionsPath[] = "/global.yaml";
const char lastTitlePath[] = "/lastTitle";
// Device + baseConfigPath + lastTitlePath
#define MAX_LAST_LAUNCHED_LENGTH 25

int parseOptionsFile(struct ArgumentList *result, FILE *file);
int loadArgumentList(struct ArgumentList *options, char *filePath);

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
void buildConfigFilePath(char *targetPath, const char *basePath, const char *targetFileName) {
  if (basePath[4] == ':') {
    strncpy(targetPath, basePath, 5);
    targetPath[5] = '\0';
  }
  else  { // Handle numbered devices
    strncpy(targetPath, basePath, 6);
    targetPath[6] = '\0';
  }

  strcat(targetPath, baseConfigPath); // Append base config path
  if (targetFileName != NULL) {
    // Append / to path if targetFileName doesn't have it already
    if (targetFileName[0] != '/') strcat(targetPath, "/");

    strcat(targetPath, targetFileName); // Append target file name
  }
}

// Gets last launched title path into titlePath
int getLastLaunchedTitle(const char *basePath, char *titlePath) {
  logString("Reading last launched title\n");
  char *targetPath = calloc(sizeof(char), MAX_LAST_LAUNCHED_LENGTH);
  buildConfigFilePath(targetPath, basePath, lastTitlePath);

  // Open last launched title file and read it
  int fd = open(targetPath, O_RDONLY);
  if (fd < 0) {
    logString("ERROR: Failed to open last launched title file: %d\n", fd);
    free(targetPath);
    return -ENOENT;
  }

  // Determine file size
  struct stat st;
  if (fstat(fd, &st)) {
    close(fd);
    free(targetPath);
    return -EIO;
  }
  // Read file contents into titlePath
  if (read(fd, titlePath, st.st_size) < 0) {
    close(fd);
    free(targetPath);
    logString("ERROR: Failed to read last launched title\n");
    return -EIO;
  }
  close(fd);
  free(targetPath);
  return 0;
}

// Writes last launched title path into lastTitle file
int updateLastLaunchedTitle(char *titlePath) {
  logString("Writing last launched title as %s\n", titlePath);
  char *targetPath = calloc(sizeof(char), MAX_LAST_LAUNCHED_LENGTH);
  buildConfigFilePath(targetPath, titlePath, NULL);

  // Make sure config directory exists
  struct stat st;
  if (stat(targetPath, &st) == -1) {
    logString("Creating config directory: %s\n", targetPath);
    mkdir(targetPath, 0777);
  }

  // Append last title file path
  strcat(targetPath, lastTitlePath);

  // Open last launched title file and write the full title path into it
  int fd = open(targetPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    logString("ERROR: Failed to open last launched title file: %d\n", fd);
    free(targetPath);
    return -ENOENT;
  }
  size_t writeLen = strlen(titlePath);
  if (write(fd, titlePath, writeLen) != writeLen) {
    logString("ERROR: Failed to write last launched title\n");
    close(fd);
    free(targetPath);
    return -EIO;
  }
  close(fd);
  free(targetPath);
  return 0;
}

// Generates ArgumentList from global config file
int getGlobalLaunchArguments(struct ArgumentList *result, char *basePath) {
  char *targetPath = calloc(sizeof(char), PATH_MAX + 1);
  buildConfigFilePath(targetPath, basePath, globalOptionsPath);
  int ret = loadArgumentList(result, targetPath);
  free(targetPath);
  return ret;
}

// Generates ArgumentList from global and title-specific config file
int getTitleLaunchArguments(struct ArgumentList *result, struct Target *target) {
  logString("Looking for game-specific config for %s (%s)\n", target->name, target->id);
  char *targetPath = calloc(sizeof(char), PATH_MAX + 1);
  buildConfigFilePath(targetPath, target->fullPath, NULL);
  // Determine actual title options file from config directory contents
  DIR *directory = opendir(targetPath);
  if (directory == NULL) {
    logString("ERROR: Can't open %s\n", targetPath);
    free(targetPath);
    return -ENOENT;
  }

  // Find game config in config directory
  char *configPath = calloc(sizeof(char), PATH_MAX + 1);
  struct dirent *entry;
  while ((entry = readdir(directory)) != NULL) {
    if (entry->d_type != DT_DIR) {
      if (!strncmp(entry->d_name, target->name, strlen(target->name))) {
        // If file starts with ISO name
        // Prefer ISO name config to title ID config
        buildConfigFilePath(configPath, targetPath, entry->d_name);
        break;
      } else if (!strncmp(entry->d_name, target->id, strlen(target->id))) {
        // If file starts with title ID
        buildConfigFilePath(configPath, targetPath, entry->d_name);
      }
    }
  }
  closedir(directory);
  free(targetPath);

  if (configPath[0] == '\0') {
    logString("Game-specific config not found\n");
    goto out;
  }

  // Load arguments
  logString("Loading game-specific config from %s\n", configPath);
  int ret = loadArgumentList(result, configPath);
  if (ret) {
    logString("Failed to load argument list: %d\n", ret);
  }

out:
  free(configPath);
  return 0;
}

// Saves title launch arguments to title-specific config file.
int updateTitleLaunchArguments(struct Target *target, struct ArgumentList *options) {
  // Build file path
  char *targetPath = calloc(sizeof(char), PATH_MAX + 1);
  buildConfigFilePath(targetPath, target->fullPath, target->name);
  strcat(targetPath, ".yaml");
  logString("Saving game-specific config to %s\n", targetPath);

  // Open file, truncating it
  int fd = open(targetPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    logString("ERROR: Failed to open file");
    free(targetPath);
    return fd;
  }

  // Write each argument into the file
  char *lineBuffer = calloc(sizeof(char), PATH_MAX + 1);
  int len = 0;
  int ret = 0;
  struct Argument *tArg = options->first;
  while (tArg != NULL) {
    len = sprintf(lineBuffer, "%s%s: %s\n", (tArg->isDisabled) ? "$" : "", tArg->arg, tArg->value);
    if ((ret = write(fd, lineBuffer, len)) != len) {
      logString("ERROR: Failed to write to file\n");
      goto out;
    }
    tArg = tArg->next;
  }
out:
  free(lineBuffer);
  close(fd);
  return ret;
}

// Parses options file into ArgumentList
int loadArgumentList(struct ArgumentList *options, char *filePath) {
  // Open global settings file and read it
  FILE *file = fopen(filePath, "r");
  if (file == NULL) {
    logString("ERROR: Failed to open %s\n", filePath);
    return -ENOENT;
  }

  // Initialize ArgumentList
  options->total = 0;
  options->first = NULL;
  options->last = NULL;

  if (parseOptionsFile(options, file)) {
    fclose(file);
    freeArgumentList(options);
    return -EIO;
  }

  fclose(file);
  return 0;
}

// Parses file into ArgumentList. Result may contain parsed arguments even if an error is returned.
int parseOptionsFile(struct ArgumentList *result, FILE *file) {
  // Our lines will mostly consist of file paths, which aren't likely to exceed 300 characters due to 255 character limit in exFAT path component
  char *lineBuffer = calloc(sizeof(char), PATH_MAX + 1);
  int startIdx;
  int substrIdx;
  int isDisabled = 0;
  while (fgets(lineBuffer, PATH_MAX, file)) { // fgets reutrns NULL if EOF or an error occurs
    startIdx = 0;
    isDisabled = 0;

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
    while (lineBuffer[substrIdx] != ':') {
      if ((lineBuffer[substrIdx] == '\0')) { // EOL reached, read next line
        goto next;
      } else if (isspace((unsigned char)lineBuffer[startIdx])) { // Ignore whitespace by advancing start index to ignore this character
        startIdx = substrIdx + 1;
      } else if ((lineBuffer[substrIdx]) == '$') { // Handle disabled argument by doing the same as about
        isDisabled = 1;
        startIdx = substrIdx + 1;
      }
      substrIdx++;
    }

    // Copy argument to argName (whitespace between the argument and ':' will be included)
    char *argName = calloc(sizeof(char), substrIdx - startIdx + 1);
    strncpy(argName, &lineBuffer[startIdx], substrIdx - startIdx);

    //
    // Parse value
    //
    startIdx = substrIdx + 1;
    // Advance line index until we read a non-whitespace character or return at EOL
    while (isspace((unsigned char)lineBuffer[startIdx])) {
      if (lineBuffer[substrIdx] == '\0') {
        free(argName);
        goto next;
      }
      startIdx++;
    }
    // Try to read value until we reach comment, a new line or the end of string
    substrIdx = startIdx;
    while ((lineBuffer[substrIdx] != '#') && (lineBuffer[substrIdx] != '\r') && (lineBuffer[substrIdx] != '\n')) {
      if (lineBuffer[substrIdx] == '\0') {
        free(argName);
        goto next;
      }
      substrIdx++; // Advance until we reach the comment or end of line
    }
    substrIdx--; // Decrement index to a previous value since the current one points at '#', '\r' or '\n'

    // Remove possible whitespace suffix
    while (isspace((unsigned char)lineBuffer[substrIdx])) {
      substrIdx--; // Decrement substring index until we read a non-whitespace character
    }
    substrIdx++; // Increment index since this the current one points to the last character of the value

    struct Argument *arg = malloc(sizeof(struct Argument));
    arg->arg = argName;
    arg->isDisabled = isDisabled;
    arg->value = NULL;
    arg->prev = NULL;
    arg->next = NULL;

    arg->value = calloc(sizeof(char), substrIdx - startIdx + 1);
    strncpy(arg->value, &lineBuffer[startIdx], substrIdx - startIdx);

    // Increment title counter and update target list
    result->total++;
    if (result->first == NULL) {
      // If this is the first entry, update both pointers
      result->first = arg;
      result->last = arg;
    } else {
      // Else, update the last entry
      result->last->next = arg;
      arg->prev = result->last;
      result->last = arg;
    }
  next:
  }
  if (ferror(file) || !feof(file)) {
    logString("ERROR: Failed to read config file\n");
    free(lineBuffer);
    return -EIO;
  }

  free(lineBuffer);
  return 0;
}

// Completely frees Argument and returns pointer to a previous argument in the list
struct Argument *freeArgument(struct Argument *arg) {
  struct Argument *prev = NULL;
  free(arg->arg);
  free(arg->value);
  if (arg->prev != NULL) {
    prev = arg->prev;
  }
  free(arg);
  return prev;
}

// Completely frees ArgumentList. Passed pointer will not be valid after this function executes
void freeArgumentList(struct ArgumentList *result) {
  struct Argument *tArg = result->last;
  while (tArg != NULL) {
    tArg = freeArgument(tArg);
  }
  result->first = NULL;
  result->last = NULL;
  result->total = 0;
  free(result);
}