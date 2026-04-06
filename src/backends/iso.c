// Implements titleScanFunc for file-based devices (MMCE, BDM)
#include "backends/backends.h"
#include "backends/cache.h"
#include "backends/internal.h"
#include "backends/target.h"
#include "backends/title_id.h"
#include "devices/utils.h"
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
    DPRINTF("backends/iso: failed to open %s\n", device->mountpoint);
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

  int actual = 0;
  for (Target *t = result->first; t; t = t->next)
    actual++;
  result->total = actual;

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
    DPRINTF("backends/iso: failed to get cwd\n");
    return -ENOENT;
  }
  int cwdLen = strlen(titlePath);     // Get the length of base path string
  if (titlePath[cwdLen - 1] != '/') { // Add path separator if cwd doesn't have one
    strcat(titlePath, "/");
    cwdLen++;
  }

  curRecursionLevel++;
  if (curRecursionLevel == MAX_SCAN_DEPTH)
    DPRINTF("backends/iso: max recursion limit reached, all directories in %s will be ignored\n", titlePath);

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
        DPRINTF("backends/iso: failed to open %s for scanning\n", entry->d_name);
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
      if (fileext && (!strcmp(fileext, ".iso") || !strcmp(fileext, ".ISO"))) {
        // Generate full path
        strcat(titlePath, entry->d_name);

        // Initialize target (store path relative to device mountpoint)
        Target *title = calloc(sizeof(Target), 1);
        title->prev = NULL;
        title->next = NULL;
        int relIdx = getRelativePathIdx(titlePath);
        if (relIdx < 0)
          relIdx = 0;
        title->path = strdup(titlePath + relIdx);
        title->device = device;
        title->flags = 0;

        // Process file name
        // <OPL title ID>.<title name>.<compressed extension>.iso
        char nameBuf[12] = {0};
        char *nameStart = entry->d_name;
        if ((strlen(entry->d_name) > 12) && (entry->d_name[4] == '_') && (entry->d_name[8] == '.') && (entry->d_name[11] == '.')) {
          // d_name begins with title ID, extract it and advance nameStart to point the actual title name
          strncpy(nameBuf, entry->d_name, 11);
          title->id = strdup(nameBuf);
          DPRINTF("backends/iso: OPL name format, using %s as the title ID\n", title->id);
          nameStart = nameStart + 12;
        }

        // Check for compressed ISO extension
        char *tmp = strchr(nameStart, '.');
        if (tmp && (tmp != fileext)) {
          strncpy(nameBuf, tmp + 1, 4);
          nameBuf[4] = '\0';
          toUppercase(nameBuf);
          if (!strncmp(&nameBuf[1], "SO", 2) || !strncmp(&nameBuf[1], "ISO", 3)) {
            fileext = tmp;
            if (nameBuf[0] == 'C')
              title->flags |= TitleFlag_CSO;
            else if (nameBuf[0] == 'Z')
              title->flags |= TitleFlag_ZSO;
          } else if (!strncmp(nameBuf, "CHD", 3)) {
            fileext = tmp;
            title->flags |= TitleFlag_CHD;
          }
        }

        // Get file name without the extension
        int nameLength = (int)(fileext - nameStart);
        title->name = calloc(sizeof(char), nameLength + 1);
        strncpy(title->name, nameStart, nameLength);

        if (result->first == NULL) {
          result->first = title;
          result->last = title;
          title->prev = title->next = NULL;
        } else if (insertIntoTargetList(result, title) != 0) {
          freeTarget(NULL, title);
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
  TitleIDCache *cache = calloc(1, sizeof(TitleIDCache));
  int isCacheUpdateNeeded = 0;
  if (!cache) {
    isCacheUpdateNeeded = 1;
  } else if (loadTitleIDCache(cache, device)) {
    DPRINTF("backends/iso: all ISOs will be rescanned\n");
    freeTitleCache(cache);
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

    char fullPathBuf[PATH_MAX];
    if (getTargetFullPath(curTarget, fullPathBuf, sizeof(fullPathBuf)) < 0) {
      DPRINTF("backends/iso: ignoring target (no full path) %s\n", curTarget->path);
      curTarget = freeTarget(result, curTarget);
      continue;
    }
    CacheEntry *cached = (cache != NULL) ? getCachedEntry(curTarget->path, cache) : NULL;

    int sizeMatches = 0;
    if (cached != NULL) {
      struct stat st;
      sizeMatches = (cached->fileSize == 0) || (!stat(fullPathBuf, &st) && (uint64_t)st.st_size == cached->fileSize);
    }

    if (cached != NULL && sizeMatches) {
      curTarget->id = strdup(cached->titleID);
      curTarget->flags |= cached->flags;
    } else {
      cacheMisses++;
      DPRINTF("backends/iso: cache miss for %s\n", fullPathBuf);

      if (!curTarget->id)
        curTarget->id = getTitleID(fullPathBuf);

      if (curTarget->id != NULL && cached != NULL)
        curTarget->flags |= cached->flags;
    }

    if (curTarget->id == NULL) {
      DPRINTF("backends/iso: failed to get title ID for %s\n", curTarget->path);
      curTarget = freeTarget(result, curTarget);
      continue;
    }

    curTarget = curTarget->next;
  }
  freeTitleCache(cache);

  int linked = 0;
  for (Target *t = result->first; t; t = t->next)
    linked++;
  result->total = linked;

  if ((cacheMisses > 0) || (isCacheUpdateNeeded)) {
    DPRINTF("backends/iso: updating title ID cache...\n");
    if (storeTitleIDCache(result, device)) {
      DPRINTF("backends/iso: failed to save title ID cache\n");
    }
  }
}
