#include "favorites.h"
#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "options.h"
#include <errno.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// Maximum number of entries kept in the recently-played list
#define RECENT_MAX 10

static const char favoritesFile[] = "/favorites.txt";
static const char recentFile[] = "/recent.txt";

typedef struct ListEntry {
  struct DeviceMapEntry *device; // Device the path is relative to
  char *relPath;                 // Path relative to the device mountpoint
  struct ListEntry *next;
} ListEntry;

static ListEntry *favorites = NULL;
static ListEntry *recents = NULL; // Newest first

//
// Shared list helpers
//

static void freeList(ListEntry **list) {
  ListEntry *cur = *list;
  while (cur != NULL) {
    ListEntry *next = cur->next;
    free(cur->relPath);
    free(cur);
    cur = next;
  }
  *list = NULL;
}

// Returns the entry matching (device, relPath), or NULL.
// If prevOut is not NULL, receives the preceding entry (for unlinking).
static ListEntry *findEntry(ListEntry *list, struct DeviceMapEntry *device, const char *relPath, ListEntry **prevOut) {
  ListEntry *prev = NULL;
  for (ListEntry *cur = list; cur != NULL; cur = cur->next) {
    if ((cur->device == device) && !strcmp(cur->relPath, relPath)) {
      if (prevOut)
        *prevOut = prev;
      return cur;
    }
    prev = cur;
  }
  return NULL;
}

// Returns the target's path relative to its device mountpoint, or NULL
static char *getTargetRelPath(Target *target) {
  int mountpointLen = getRelativePathIdx(target->fullPath);
  if (mountpointLen == -1)
    return NULL;
  return &target->fullPath[mountpointLen];
}

// Loads entries for the device from file into the end of *list
static void loadListFile(ListEntry **list, struct DeviceMapEntry *device, const char *fileName) {
  char filePath[PATH_MAX];
  buildConfigFilePath(filePath, device->mountpoint, fileName);

  FILE *file = fopen(filePath, "rb");
  if (file == NULL)
    return;

  // Find the current end of the list to preserve order across devices
  ListEntry **tail = list;
  while (*tail != NULL)
    tail = &(*tail)->next;

  char lineBuf[PATH_MAX + 1];
  while (fgets(lineBuf, sizeof(lineBuf), file) != NULL) {
    // Strip trailing newline/carriage return
    lineBuf[strcspn(lineBuf, "\r\n")] = '\0';
    if (lineBuf[0] == '\0')
      continue;

    ListEntry *entry = calloc(sizeof(ListEntry), 1);
    entry->device = device;
    entry->relPath = strdup(lineBuf);
    *tail = entry;
    tail = &entry->next;
  }
  fclose(file);
}

// Saves the device's entries from list into file
static void saveListFile(ListEntry *list, struct DeviceMapEntry *device, const char *fileName) {
  char filePath[PATH_MAX];
  char dirPath[PATH_MAX];
  buildConfigFilePath(dirPath, device->mountpoint, NULL);
  buildConfigFilePath(filePath, device->mountpoint, fileName);

  // Make sure the config directory exists (same behavior as the title cache)
  struct stat st;
  if (stat(dirPath, &st) == -1) {
    if (mkdir(dirPath, 0777)) {
      DPRINTF("ERROR: Failed to create config directory %s\n", dirPath);
      return;
    }
  }

  FILE *file = fopen(filePath, "wb");
  if (file == NULL) {
    DPRINTF("ERROR: Failed to open %s for writing\n", filePath);
    return;
  }
  for (ListEntry *cur = list; cur != NULL; cur = cur->next) {
    if (cur->device != device)
      continue;
    fputs(cur->relPath, file);
    fputc('\n', file);
  }
  fclose(file);
}

//
// Public API
//

void favoritesInit() {
  favoritesFree();
  for (int i = 0; i < MAX_DEVICES; i++) {
    if ((deviceModeMap[i].mode == MODE_NONE) || (deviceModeMap[i].mode == MODE_ALL) || (deviceModeMap[i].mountpoint == NULL))
      continue;
    loadListFile(&favorites, &deviceModeMap[i], favoritesFile);
    loadListFile(&recents, &deviceModeMap[i], recentFile);
  }
}

void favoritesFree() {
  freeList(&favorites);
  freeList(&recents);
}

int favoritesIsFavorite(Target *target) {
  char *relPath = getTargetRelPath(target);
  if (relPath == NULL)
    return 0;
  return (findEntry(favorites, target->device, relPath, NULL) != NULL);
}

void favoritesToggle(Target *target) {
  char *relPath = getTargetRelPath(target);
  if (relPath == NULL)
    return;

  ListEntry *prev = NULL;
  ListEntry *entry = findEntry(favorites, target->device, relPath, &prev);
  if (entry != NULL) {
    // Remove
    if (prev != NULL)
      prev->next = entry->next;
    else
      favorites = entry->next;
    free(entry->relPath);
    free(entry);
  } else {
    // Add to the end
    entry = calloc(sizeof(ListEntry), 1);
    entry->device = target->device;
    entry->relPath = strdup(relPath);
    ListEntry **tail = &favorites;
    while (*tail != NULL)
      tail = &(*tail)->next;
    *tail = entry;
  }
  saveListFile(favorites, target->device, favoritesFile);
}

int favoritesGetRecentRank(Target *target) {
  char *relPath = getTargetRelPath(target);
  if (relPath == NULL)
    return -1;

  int rank = 0;
  for (ListEntry *cur = recents; cur != NULL; cur = cur->next) {
    if ((cur->device == target->device) && !strcmp(cur->relPath, relPath))
      return rank;
    rank++;
  }
  return -1;
}

void favoritesAddRecent(struct DeviceMapEntry *device, char *fullPath) {
  int mountpointLen = getRelativePathIdx(fullPath);
  if (mountpointLen == -1)
    return;
  char *relPath = &fullPath[mountpointLen];

  // Remove an existing entry for the same title
  ListEntry *prev = NULL;
  ListEntry *entry = findEntry(recents, device, relPath, &prev);
  if (entry != NULL) {
    if (prev != NULL)
      prev->next = entry->next;
    else
      recents = entry->next;
    free(entry->relPath);
    free(entry);
  }

  // Prepend as the most recent
  entry = calloc(sizeof(ListEntry), 1);
  entry->device = device;
  entry->relPath = strdup(relPath);
  entry->next = recents;
  recents = entry;

  // Trim the list to RECENT_MAX entries
  int count = 1;
  for (ListEntry *cur = recents; cur != NULL; cur = cur->next) {
    if ((count == RECENT_MAX) && (cur->next != NULL)) {
      ListEntry *excess = cur->next;
      cur->next = NULL;
      freeList(&excess);
      break;
    }
    count++;
  }

  saveListFile(recents, device, recentFile);
}
