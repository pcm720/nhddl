// Implements titleScanFunc for file-based devices (MMCE, BDM)
#include "iso.h"
#include "common.h"
#include "devices.h"
#include "gui.h"
#include "iso_cache.h"
#include "iso_title_id.h"
#include <errno.h>
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int _findISO(DIR *directory, TargetList *result, struct DeviceMapEntry *device);
void processTitleID(TargetList *result, struct DeviceMapEntry *device);

// Directories to skip when browsing for ISOs
const char *ignoredDirs[] = {
    "nhddl", "APPS", "ART", "CFG", "CHT", "LNG", "THM", "VMC", "XEBPLUS", "MemoryCards",
};

// Used by _findISO to limit recursion depth
#define MAX_SCAN_DEPTH 6
static int curRecursionLevel = 1;

// Scans given storage device and appends valid launch candidates to TargetList
// Returns 0 if successful, non-zero if no targets were found or an error occurs
int findISO(TargetList *result, struct DeviceMapEntry *device) {
  DIR *directory;

  if (device->mode == MODE_NONE || device->mountpoint == NULL)
    return -ENODEV;

  curRecursionLevel = 1; // Reset recursion level
  directory = opendir(device->mountpoint);
  // Check if the directory can be opened
  if (directory == NULL) {
    uiSplashLogString(LEVEL_ERROR, "ERROR: Can't open %s\n", device->mountpoint);
    return -ENOENT;
  }

  chdir(device->mountpoint);
  if (_findISO(directory, result, device)) {
    freeTargetList(result);
    closedir(directory);
    return -ENOENT;
  }
  closedir(directory);

  if (result->total == 0) {
    return -ENOENT;
  }

  // Get title IDs for each found title
  processTitleID(result, device);

  if (result->first == NULL) {
    return -ENOENT;
  }

  // Set indexes for each title
  int idx = 0;
  Target *curTitle = result->first;
  while (curTitle != NULL) {
    curTitle->idx = idx;
    idx++;
    curTitle = curTitle->next;
  }

  return 0;
}

// Searches rootpath and adds discovered ISOs to TargetList
int _findISO(DIR *directory, TargetList *result, struct DeviceMapEntry *device) {
  if (directory == NULL)
    return -ENOENT;

  // Read directory entries
  struct dirent *entry;
  char *fileext;
  char titlePath[PATH_MAX + 1];
  if (!getcwd(titlePath, PATH_MAX + 1)) { // Initialize titlePath with current working directory
    uiSplashLogString(LEVEL_ERROR, "Failed to get cwd\n");
    return -ENOENT;
  }
  int cwdLen = strlen(titlePath);     // Get the length of base path string
  if (titlePath[cwdLen - 1] != '/') { // Add path separator if cwd doesn't have one
    strcat(titlePath, "/");
    cwdLen++;
  }

  curRecursionLevel++;
  if (curRecursionLevel == MAX_SCAN_DEPTH)
    printf("Max recursion limit reached, all directories in %s will be ignored\n", titlePath);

  while ((entry = readdir(directory)) != NULL) {
    // Reset titlePath by ending string on base path
    titlePath[cwdLen] = '\0';
    // Check if the entry is a directory using d_type
    switch (entry->d_type) {
    case DT_DIR:
      // Ignore directories if max scan depth is reached
      if (curRecursionLevel == MAX_SCAN_DEPTH)
        continue;

      // Ignore hidden, special and invalid directories (non-ASCII paths seem to return '?' and cause crashes when used with opendir)
      if ((entry->d_name[0] == '.') || (entry->d_name[0] == '$') || (entry->d_name[0] == '?'))
        continue;

      for (int i = 0; i < sizeof(ignoredDirs) / sizeof(char *); i++) {
        if (!strcmp(ignoredDirs[i], entry->d_name))
          continue;
      }

      // Generate full path, open dir and change cwd
      strcat(titlePath, entry->d_name);
      DIR *d = opendir(titlePath);
      if (d == NULL) {
        printf("Failed to open %s for scanning\n", entry->d_name);
        continue;
      }
      chdir(titlePath);
      // Process inner directory recursively
      _findISO(d, result, device);
      closedir(d);
    default:
      if (entry->d_name[0] == '.') // Ignore .files (most likely macOS doubles)
        continue;

      // Make sure file has .iso extension
      fileext = strrchr(entry->d_name, '.');
      if ((fileext != NULL) && (!strcmp(fileext, ".iso") || !strcmp(fileext, ".ISO"))) {
        // Generate full path
        strcat(titlePath, entry->d_name);

        // Initialize target
        Target *title = calloc(sizeof(Target), 1);
        title->prev = NULL;
        title->next = NULL;
        title->fullPath = strdup(titlePath);
        title->device = device;

        // Get file name without the extension
        int nameLength = (int)(fileext - entry->d_name);
        title->name = calloc(sizeof(char), nameLength + 1);
        strncpy(title->name, entry->d_name, nameLength);

        // Increment title counter and update target list
        result->total++;
        if (result->first == NULL) {
          // If this is the first entry, update both pointers
          result->first = title;
          result->last = title;
        } else {
          insertIntoTargetList(result, title);
        }
      }
    }
  }
  curRecursionLevel--;

  return 0;
}

// Fills in title ID for every entry in the list
void processTitleID(TargetList *result, struct DeviceMapEntry *device) {
  if (result->total == 0)
    return;

  // Load title cache
  TitleIDCache *cache = malloc(sizeof(TitleIDCache));
  int isCacheUpdateNeeded = 0;
  if (loadTitleIDCache(cache, device)) {
    uiSplashLogString(LEVEL_WARN, "Failed to load title ID cache, all ISOs will be rescanned\n");
    free(cache);
    cache = NULL;
  } else if (cache->total != result->total) {
    // Set flag if number of entries is different
    isCacheUpdateNeeded = 1;
  }

  // For every entry in target list, try to get title ID from cache
  // If cache doesn't have title ID for the path,
  // get it from ISO
  int cacheMisses = 0;
  char *titleID = NULL;
  Target *curTarget = result->first;
  while (curTarget != NULL) {
    // Ignore targets not belonging to the current device
    if (curTarget->device != device) {
      curTarget = curTarget->next;
      continue;
    }

    // Try to get title ID from cache
    if (cache != NULL) {
      titleID = getCachedTitleID(curTarget->fullPath, cache);
    }

    if (titleID != NULL) {
      curTarget->id = strdup(titleID);
    } else { // Get title ID from ISO
      cacheMisses++;
      printf("Cache miss for %s\n", curTarget->fullPath);
      curTarget->id = getTitleID(curTarget->fullPath);
      if (curTarget->id == NULL) {
        uiSplashLogString(LEVEL_WARN, "Failed to scan\n%s\n", curTarget->fullPath);
        curTarget = freeTarget(result, curTarget);
        result->total -= 1;
        continue;
      }
    }

    curTarget = curTarget->next;
  }
  freeTitleCache(cache);

  if ((cacheMisses > 0) || (isCacheUpdateNeeded)) {
    uiSplashLogString(LEVEL_INFO_NODELAY, "Updating title ID cache...\n");
    if (storeTitleIDCache(result, device)) {
      uiSplashLogString(LEVEL_WARN, "Failed to save title ID cache\n");
    }
  }
}
