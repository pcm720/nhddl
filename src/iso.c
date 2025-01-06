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

int _findISO(DIR *directory, TargetList *result, DeviceMapEntry *device);
void insertIntoList(TargetList *result, Target *title);
void processTitleID(TargetList *result);

// Directories to skip when browsing for ISOs
const char *ignoredDirs[] = {
    "nhddl", "APPS", "ART", "CFG", "CHT", "LNG", "THM", "VMC", "XEBPLUS", "MemoryCards",
};

// Used by _findISO to limit recursion depth
#define MAX_SCAN_DEPTH 6
static int curRecursionLevel = 1;

// Generates a list of launch candidates found on BDM devices
// Returns NULL if no targets were found or an error occurs
TargetList *findISO() {
  DIR *directory;
  TargetList *result = malloc(sizeof(TargetList));
  result->total = 0;
  result->first = NULL;
  result->last = NULL;

  char *mountpoint;

  for (int i = 0; i < MAX_DEVICES; i++) {
    if (deviceModeMap[i].mode == MODE_NONE || deviceModeMap[i].mountpoint == NULL)
      break;

    // Ignore devices with doNotScan flag
    if (deviceModeMap[i].doNotScan)
      continue;

    mountpoint = deviceModeMap[i].mountpoint;

    curRecursionLevel = 1; // Reset recursion level
    directory = opendir(mountpoint);
    // Check if the directory can be opened
    if (directory == NULL) {
      uiSplashLogString(LEVEL_ERROR, "ERROR: Can't open %s\n", mountpoint);
      return NULL;
    }

    chdir(mountpoint);
    if (_findISO(directory, result, &deviceModeMap[i])) {
      freeTargetList(result);
      closedir(directory);
      return NULL;
    }
    closedir(directory);
  }

  if (result->total == 0) {
    free(result);
    return NULL;
  }

  // Get title IDs for each found title
  processTitleID(result);

  if (result->first == NULL) {
    freeTargetList(result);
    return NULL;
  }

  // Set indexes for each title
  int idx = 0;
  Target *curTitle = result->first;
  while (curTitle != NULL) {
    curTitle->idx = idx;
    idx++;
    curTitle = curTitle->next;
  }

  return result;
}

// Searches rootpath and adds discovered ISOs to TargetList
int _findISO(DIR *directory, TargetList *result, DeviceMapEntry *device) {
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
          insertIntoList(result, title);
        }
      }
    }
  }
  curRecursionLevel--;

  return 0;
}

// Converts lowercase ASCII string into uppercase
void toUppercase(char *str) {
  for (int i = 0; i <= strlen(str); i++)
    if (str[i] >= 0x61 && str[i] <= 0x7A) {
      str[i] -= 32;
    }
}

// Inserts title in the list while keeping the alphabetical order
void inline insertIntoList(TargetList *result, Target *title) {
  // Traverse the list in reverse
  Target *curTitle = result->last;

  // Covert title name to uppercase
  char *curUppercase = strdup(title->name);
  toUppercase(curUppercase);

  // Overall, title name should not exceed PATH_MAX
  char lastUppercase[PATH_MAX];

  while (1) {
    // Reset string buffer
    lastUppercase[0] = '\0';
    // Convert name of the last title to uppercase
    strlcpy(lastUppercase, curTitle->name, PATH_MAX);
    toUppercase(lastUppercase);

    // Compare new title name and the current title name
    if (strcmp(curUppercase, lastUppercase) >= 0) {
      // First letter of the new title is after or the same as the current one
      // New title must be inserted after the current list element
      if (curTitle->next != NULL) {
        // Current title has a next title, update the next element
        curTitle->next->prev = title;
        title->next = curTitle->next;
      } else {
        // Current title has no next title (it's the last list element)
        result->last = title;
      }
      title->prev = curTitle;
      curTitle->next = title;
      break;
    }

    if (curTitle->prev == NULL) {
      // Current title is the first in this list
      // New title must be inserted at the beginning
      curTitle->prev = title;
      title->next = curTitle;
      result->first = title;
      break;
    }

    // Keep traversing the list
    curTitle = curTitle->prev;
  }
  free(curUppercase);
}

// Completely frees Target and returns pointer to the next target in the list
Target *freeTarget(TargetList *targetList, Target *target) {
  // Update target list if target is the first or the last element
  if (targetList->first == target) {
    targetList->first = target->next;
  }
  if (targetList->last == target) {
    targetList->last = target->prev;
  }

  Target *next = NULL;
  // If target has a link to the next element
  if (target->next != NULL) {
    // Set return pointer
    next = target->next;
    if (target->prev != NULL) {
      // If target has a link to the previous element, link prev and next together
      next->prev = target->prev;
      target->prev->next = next;
    } else {
      // Else, remove link to target
      next->prev = NULL;
    }
  } else if (target->prev != NULL) {
    // If target doesn't have a link to the next element
    // but has a link to the previous element, remove link to target
    target->prev->next = NULL;
  }

  free(target->fullPath);
  free(target->name);
  if (target->id != NULL)
    free(target->id);

  free(target);
  return next;
}

// Fills in title ID for every entry in the list
void processTitleID(TargetList *result) {
  if (result->total == 0)
    return;

  // Load title cache
  TitleIDCache *cache = malloc(sizeof(TitleIDCache));
  int isCacheUpdateNeeded = 0;
  if (loadTitleIDCache(cache)) {
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
    if (storeTitleIDCache(result)) {
      uiSplashLogString(LEVEL_WARN, "Failed to save title ID cache\n");
    }
  }
}

// Completely frees TargetList. Passed pointer will not be valid after this function executes
void freeTargetList(TargetList *result) {
  Target *target = result->first;
  while (target != NULL) {
    target = freeTarget(result, target);
  }
  result->first = NULL;
  result->last = NULL;
  result->total = 0;
  free(result);
}

// Finds target with given index in the list and returns a pointer to it
Target *getTargetByIdx(TargetList *targets, int idx) {
  Target *current = targets->first;
  while (1) {
    if (current->idx == idx) {
      return current;
    }

    if (current->next == NULL)
      break;

    current = current->next;
  }
  return NULL;
}

// Makes and returns a deep copy of src without prev/next pointers.
Target *copyTarget(Target *src) {
  Target *copy = calloc(sizeof(Target), 1);
  copy->idx = src->idx;

  copy->fullPath = strdup(src->fullPath);
  copy->name = strdup(src->name);
  copy->id = strdup(src->id);
  copy->device = src->device;

  return copy;
}
