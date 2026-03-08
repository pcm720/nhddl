// Implements titleScanFunc for file-based devices (MMCE, BDM)
#include "backends/backends.h"
#include "backends/cache.h"
#include "backends/title_id.h"
#include "common.h"
#include "dprintf.h"
#include "ui/ui.h"
#include <errno.h>
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int _findISO(DIR *directory, TargetList *result, struct BackendDevice *device);
void processTitleID(TargetList *result, struct BackendDevice *device);

// Directories to skip when browsing for ISOs
const char *ignoredDirs[] = {
    "nhddl", "neutrino", "APPS", "ART", "CFG", "CHT", "LNG", "THM", "VMC", "XEBPLUS", "MemoryCards", "bbnl",
};

// Used by _findISO to limit recursion depth
#define MAX_SCAN_DEPTH 6
static int curRecursionLevel = 1;

// Scans given storage device and fills device->titles with valid launch candidates
// Returns 0 if successful, non-zero if no targets were found or an error occurs
int findISO(struct BackendDevice *device) {
  if (!device || device->type == Device_None || device->mountpoint == NULL)
    return -ENODEV;
  if (device->titles)
    freeTargetList(device->titles);
  device->titles = calloc(1, sizeof(TargetList));
  if (!device->titles)
    return -ENOMEM;
  TargetList *result = device->titles;

  curRecursionLevel = 1;
  DIR *directory = opendir(device->mountpoint);
  if (directory == NULL) {
    DPRINTF(device->mountpoint);
    free(device->titles);
    device->titles = NULL;
    return -ENOENT;
  }
  chdir(device->mountpoint);
  if (_findISO(directory, result, device)) {
    closedir(directory);
    freeTargetList(device->titles);
    device->titles = NULL;
    return -ENOENT;
  }
  closedir(directory);

  if (result->total == 0) {
    freeTargetList(device->titles);
    device->titles = NULL;
    return -ENOENT;
  }
  processTitleID(result, device);
  if (result->first == NULL) {
    freeTargetList(device->titles);
    device->titles = NULL;
    return -ENOENT;
  }
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
int _findISO(DIR *directory, TargetList *result, struct BackendDevice *device) {
  if (directory == NULL)
    return -ENOENT;

  // Read directory entries
  struct dirent *entry;
  char *fileext;
  char titlePath[PATH_MAX + 1];
  if (!getcwd(titlePath, PATH_MAX + 1)) { // Initialize titlePath with current working directory
    DPRINTF("Failed to get cwd\n");
    return -ENOENT;
  }
  int cwdLen = strlen(titlePath);     // Get the length of base path string
  if (titlePath[cwdLen - 1] != '/') { // Add path separator if cwd doesn't have one
    strcat(titlePath, "/");
    cwdLen++;
  }

  curRecursionLevel++;
  if (curRecursionLevel == MAX_SCAN_DEPTH)
    DPRINTF("Max recursion limit reached, all directories in %s will be ignored\n", titlePath);

  while ((entry = readdir(directory)) != NULL) {
    // Reset titlePath by ending string on base path
    titlePath[cwdLen] = '\0';

    // Ignore .files and directories
    if (entry->d_name[0] == '.')
      continue;

    // Check if the entry is a directory using d_type
    switch (entry->d_type) {
    case DT_DIR:
      // Ignore directories if max scan depth is reached
      if (curRecursionLevel == MAX_SCAN_DEPTH)
        continue;

      // Ignore special and invalid directories (non-ASCII paths seem to return '?' and cause crashes when used with opendir)
      if ((entry->d_name[0] == '$') || (entry->d_name[0] == '?'))
        continue;

      for (int i = 0; i < sizeof(ignoredDirs) / sizeof(char *); i++) {
        if (!strcmp(ignoredDirs[i], entry->d_name))
          goto next;
      }

      // Generate full path, open dir and change cwd
      strcat(titlePath, entry->d_name);
      DIR *d = opendir(titlePath);
      if (d == NULL) {
        DPRINTF("Failed to open %s for scanning\n", entry->d_name);
        continue;
      }
      chdir(titlePath);
      // Process inner directory recursively
      _findISO(d, result, device);
      closedir(d);

    next:
      break;
    default:
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
void processTitleID(TargetList *result, struct BackendDevice *device) {
  if (result->total == 0)
    return;

  // Load title cache
  TitleIDCache *cache = malloc(sizeof(TitleIDCache));
  int isCacheUpdateNeeded = 0;
  if (loadTitleIDCache(cache, device)) {
    DPRINTF("all ISOs will be rescanned\n");
    free(cache);
    cache = NULL;
  } else if (cache->total != result->total) {
    // Set flag if number of entries is different
    isCacheUpdateNeeded = 1;
  }

  // For every entry in target list, try to get title ID from cache (validate with file size)
  int cacheMisses = 0;
  Target *curTarget = result->first;
  while (curTarget != NULL) {
    // Ignore targets not belonging to the current device
    if (curTarget->device != device) {
      curTarget = curTarget->next;
      continue;
    }

    curTarget->flags = 0;
    CacheEntry *cached = (cache != NULL) ? getCachedEntry(curTarget->fullPath, cache) : NULL;

    if (cached != NULL) {
      struct stat st;
      int sizeMatches = (cached->fileSize == 0) || (stat(curTarget->fullPath, &st) == 0 && (uint64_t)st.st_size == cached->fileSize);
      if (sizeMatches) {
        // Cache hit: use cached title ID and flags
        curTarget->id = strdup(cached->titleID);
        curTarget->flags = cached->flags;
      } else {
        // Size mismatch or stat failed: re-read from ISO, preserve flags from cache
        cacheMisses++;
        DPRINTF("Cache miss for %s\n", curTarget->fullPath);
        curTarget->id = getTitleID(curTarget->fullPath);
        if (curTarget->id != NULL)
          curTarget->flags = cached->flags;
      }
    } else {
      cacheMisses++;
      DPRINTF("Cache miss for %s\n", curTarget->fullPath);
      curTarget->id = getTitleID(curTarget->fullPath);
    }

    if (curTarget->id == NULL) {
      DPRINTF(curTarget->fullPath);
      curTarget = freeTarget(result, curTarget);
      result->total -= 1;
      continue;
    }

    curTarget = curTarget->next;
  }
  freeTitleCache(cache);

  if ((cacheMisses > 0) || (isCacheUpdateNeeded)) {
    DPRINTF("Updating title ID cache...\n");
    if (storeTitleIDCache(result, device)) {
      DPRINTF("Failed to save title ID cache\n");
    }
  }
}
