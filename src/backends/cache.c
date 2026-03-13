// Per-backend NHDDL caches
// Implements title ID cache for file-based devices (MMCE, BDM, UDPFS)
// and last title information
#include "backends/cache.h"
#include "backends/target.h"
#include "config/common.h"
#include "dprintf.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CACHE_VERSION 3
char CACHE_MAGIC[] = { 'N', 'I', 'D', 'C' };

const char titleIDCacheFile[] = "/cache.bin";
static const char lastTitleFile[] = "/lastTitle.bin";

// File header
typedef struct {
  char magic[4];
  uint32_t version;
  uint64_t total;
} CacheMetadata;

// Entry header
typedef struct {
  uint32_t pathLength; // Includes null-terminator
  uint64_t fileSize;
  uint32_t flags;
  char titleID[12];
  uint8_t reserved[4];
} CacheEntryHeader;

#define CACHE_METADATA_SIZE sizeof(CacheMetadata)
#define CACHE_ENTRY_HEADER_SIZE sizeof(CacheEntryHeader)

// Saves TargetList into title ID cache on given storage device
int storeTitleIDCache(TargetList *list, struct BackendDevice *device) {
  if (list->total == 0) {
    return 0;
  }

  // Get total number of valid cache entries
  int total = 0;
  Target *curTitle = list->first;
  while (curTitle != NULL) {
    if (curTitle->id != NULL && strlen(curTitle->id) == 11) {
      total++;
    }
    curTitle = curTitle->next;
  }
  if (total == 0) {
    DPRINTF("backends/cache: warning: no valid cache entries found\n");
    return 0;
  }

  // Make sure path exists
  if (device->type == Device_None || device->mountpoint == NULL)
    return -ENODEV;

  // Use metadev for config path when set (e.g. HDL cache on PFS partition)
  struct BackendDevice *configDevice = device->metadev ? device->metadev : device;

  // Prepare paths and header
  char cachePath[PATH_MAX];
  char dirPath[PATH_MAX];
  CacheEntryHeader header;
  CacheMetadata meta;
  memcpy(meta.magic, CACHE_MAGIC, 4);
  meta.version = (uint32_t)CACHE_VERSION;
  meta.total = (uint64_t)total;

  buildConfigFilePath(dirPath, configDevice->mountpoint, NULL);
  buildConfigFilePath(cachePath, configDevice->mountpoint, titleIDCacheFile);

  // Get path to config directory and make sure it exists
  struct stat st;
  if (stat(dirPath, &st) == -1) {
    DPRINTF("backends/cache: creating config directory: %s\n", dirPath);
    if (mkdir(dirPath, 0777)) {
      DPRINTF("backends/cache: error: failed to create directory\n");
      return -EIO;
    }
  }

  // Open cache file for writing
  FILE *file = fopen(cachePath, "wb");
  if (file == NULL) {
    DPRINTF("backends/cache: error: failed to open cache file for writing\n");
    return -EIO;
  }

  int result;
  // Write cache file header (16-byte aligned)
  result = (fwrite(&meta, 1, CACHE_METADATA_SIZE, file) == CACHE_METADATA_SIZE) ? 1 : 0;
  if (!result) {
    DPRINTF("backends/cache: error: failed to write metadata: %d\n", errno);
    fclose(file);
    remove(cachePath);
    return -EIO;
  }

  // Write each entry
  curTitle = list->first;
  while (curTitle != NULL) {
    // Ignore empty entries or entries not belonging to the current device
    if ((curTitle->id == NULL || strlen(curTitle->id) < 11) || (curTitle->device != device)) {
      curTitle = curTitle->next;
      continue;
    }

    // Path is already relative to device mountpoint
    size_t pathLen = strlen(curTitle->path) + 1;
    if (pathLen > 0xFFFFFFFFu) {
      curTitle = curTitle->next;
      continue;
    }

    char fullPathBuf[PATH_MAX];
    if (getTargetFullPath(curTitle, fullPathBuf, sizeof(fullPathBuf))) {
      curTitle = curTitle->next;
      continue;
    }
    if (stat(fullPathBuf, &st)) {
      st.st_size = 0;
    }

    // Write entry header (32-byte, 16-byte aligned)
    memset(&header, 0, sizeof(header));
    header.pathLength = (uint32_t)pathLen;
    header.fileSize = (uint64_t)st.st_size;
    header.flags = curTitle->flags;
    memcpy(header.titleID, curTitle->id, sizeof(header.titleID));
    header.titleID[11] = '\0';
    result = (fwrite(&header, 1, CACHE_ENTRY_HEADER_SIZE, file) == CACHE_ENTRY_HEADER_SIZE) ? 1 : 0;
    if (!result) {
      DPRINTF("backends/cache: error: %s: failed to write header: %d\n", curTitle->name, errno);
      fclose(file);
      remove(cachePath);
      return -EIO;
    }
    result = fwrite(curTitle->path, header.pathLength, 1, file);
    if (!result) {
      DPRINTF("backends/cache: error: %s: failed to write ISO path: %d\n", curTitle->name, errno);
      fclose(file);
      remove(cachePath);
      return -EIO;
    }
    curTitle = curTitle->next;
  }
  fclose(file);

  return 0;
}

// Loads title ID cache from storage into cache
int loadTitleIDCache(TitleIDCache *cache, struct BackendDevice *device) {
  // Make sure path exists
  if (device->type == Device_None || device->mountpoint == NULL)
    return -ENODEV;

  // Use metadev for config path when set (e.g. HDL cache on PFS partition)
  struct BackendDevice *configDevice = device->metadev ? device->metadev : device;

  cache->total = 0;
  cache->lastMatchedIdx = 0;

  // Open cache file for reading
  char cachePath[PATH_MAX];
  buildConfigFilePath(cachePath, configDevice->mountpoint, titleIDCacheFile);

  FILE *file = fopen(cachePath, "rb");
  if (file == NULL)
    return -ENOENT;

  int result;

  // Read cache file header (16-byte aligned)
  CacheMetadata meta;
  memset(&meta, 0, sizeof(meta));
  result = fread(&meta, 1, CACHE_METADATA_SIZE, file);
  if (result != CACHE_METADATA_SIZE) {
    DPRINTF("backends/cache: error: failed to read cache metadata\n");
    fclose(file);
    return -EIO;
  }

  // Make sure header is valid
  if (memcmp(meta.magic, CACHE_MAGIC, 4)) {
    DPRINTF("backends/cache: error: cache magic doesn't match, refusing to load\n");
    fclose(file);
    return -EINVAL;
  }
  if (meta.version != CACHE_VERSION) {
    DPRINTF("backends/cache: error: unsupported or outdated cache version %u, rescanning\n", (unsigned)meta.version);
    fclose(file);
    return -EINVAL;
  }

  // Allocate memory for cache entries based on total entry count from header metadata
  int readIndex = 0;
  if (meta.total > (uint64_t)INT_MAX) {
    DPRINTF("backends/cache: error: cache entry count too large\n");
    fclose(file);
    return -EINVAL;
  }
  int totalEntries = (int)meta.total;
  cache->entries = malloc((sizeof(CacheEntry) * (size_t)totalEntries));
  if (cache->entries == NULL) {
    DPRINTF("backends/cache: error: can't allocate enough memory\n");
    fclose(file);
    return -ENOMEM;
  }

  // Read each entry (V3 only)
  CacheEntryHeader header;
  char pathBuf[PATH_MAX + 1];
  while (!feof(file)) {
    pathBuf[0] = '\0';
    result = fread(&header, 1, CACHE_ENTRY_HEADER_SIZE, file);
    if (result != CACHE_ENTRY_HEADER_SIZE) {
      if (!feof(file))
        DPRINTF("backends/cache: warning: read less than expected, title ID cache might be incomplete\n");
      break;
    }
    if (header.pathLength == 0 || header.pathLength > PATH_MAX) {
      DPRINTF("backends/cache: warning: invalid path length in cache entry\n");
      break;
    }
    result = fread(&pathBuf, 1, header.pathLength, file);
    if (result != header.pathLength) {
      DPRINTF("backends/cache: warning: read less than expected, title ID cache might be incomplete\n");
      break;
    }
    pathBuf[header.pathLength] = '\0';

    CacheEntry *entry = &cache->entries[readIndex];
    memcpy(entry->titleID, header.titleID, sizeof(entry->titleID));
    entry->titleID[11] = '\0';
    entry->fullPath = strdup(pathBuf);
    entry->fileSize = header.fileSize;
    entry->flags = header.flags;
    readIndex++;
  }
  fclose(file);

  // Free unused memory
  if (readIndex != totalEntries)
    cache->entries = realloc(cache->entries, sizeof(CacheEntry) * (size_t)readIndex);

  cache->total = readIndex;
  return 0;
}

// Returns a pointer to cache entry or NULL if path is not found in the cache.
// path must be relative to device mountpoint (same format as stored in cache entries).
CacheEntry *getCachedEntry(char *path, TitleIDCache *cache) {
  for (int i = cache->lastMatchedIdx; i < cache->total; i++) {
    if (!strcmp(cache->entries[i].fullPath, path)) {
      cache->lastMatchedIdx = i;
      return &cache->entries[i];
    }
  }
  return NULL;
}

// Returns a pointer to title ID or NULL if path is not found in the cache.
// path must be relative to device mountpoint.
char *getCachedTitleID(char *path, TitleIDCache *cache) {
  CacheEntry *entry = getCachedEntry(path, cache);
  return entry ? entry->titleID : NULL;
}

// Frees memory used by title ID cache
void freeTitleCache(TitleIDCache *cache) {
  if (cache == NULL)
    return;

  for (int i = 0; i < cache->total; i++) {
    free(cache->entries[i].fullPath);
  }

  free(cache->entries);
  free(cache);
}

// Reads lastTitle.bin for device and sets device->lastLaunchedTitleIdx and device->lastLaunchedTimestamp.
void loadLastLaunchedIndex(struct BackendDevice *device) {
  device->lastLaunchedTitleIdx = -1;
  device->lastLaunchedTimestamp = 0;
  if (!device || device->type == Device_None || !device->mountpoint || !device->titles)
    return;
  struct BackendDevice *configDevice = device->metadev ? device->metadev : device;
  char targetPath[PATH_MAX];
  buildConfigFilePath(targetPath, configDevice->mountpoint, lastTitleFile);
  int fd = open(targetPath, O_RDONLY);
  if (fd < 0)
    return;
  uint32_t timestamp;
  if (read(fd, &timestamp, sizeof(timestamp)) != sizeof(timestamp)) {
    close(fd);
    return;
  }
  device->lastLaunchedTimestamp = timestamp;
  size_t fsize = (size_t)(lseek(fd, 0, SEEK_END) - sizeof(timestamp));
  lseek(fd, sizeof(timestamp), SEEK_SET);
  if (fsize == 0 || fsize >= PATH_MAX) {
    close(fd);
    return;
  }
  char pathBuf[PATH_MAX];
  if ((size_t)read(fd, pathBuf, fsize) != fsize) {
    close(fd);
    return;
  }
  close(fd);
  pathBuf[fsize] = '\0';
  int idx = 0;
  for (Target *t = device->titles->first; t; t = t->next, idx++) {
    if (!strcmp(t->path, pathBuf)) {
      device->lastLaunchedTitleIdx = idx;
      return;
    }
  }
}

// Writes last launched title (target->path) into lastTitle file on device and sets device->lastLaunchedTitleIdx.
int updateLastLaunchedTitle(Target *target) {
  if (!target || !target->device || !target->path)
    return -EINVAL;
  struct BackendDevice *device = target->device;
  struct BackendDevice *writeDevice = device->metadev ? device->metadev : device;
  DPRINTF("backends/cache: writing last launched title as %s\n", target->path);
  char targetPath[PATH_MAX];
  buildConfigFilePath(targetPath, writeDevice->mountpoint, NULL);
  struct stat st;
  if (stat(targetPath, &st) == -1) {
    DPRINTF("backends/cache: creating config directory: %s\n", targetPath);
    mkdir(targetPath, 0777);
  }
  strcat(targetPath, lastTitleFile);
  int fd = open(targetPath, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    DPRINTF("backends/cache: error: failed to open last launched title file: %d\n", fd);
    return -ENOENT;
  }
  uint32_t ts = getTimestamp();
  if (write(fd, &ts, sizeof(ts)) != sizeof(ts)) {
    DPRINTF("backends/cache: error: failed to write last launched title timestamp\n");
    close(fd);
    return -EIO;
  }
  size_t pathLen = strlen(target->path) + 1;
  if (write(fd, target->path, pathLen) != (ssize_t)pathLen) {
    DPRINTF("backends/cache: error: failed to write last launched title\n");
    close(fd);
    return -EIO;
  }
  close(fd);
  device->lastLaunchedTitleIdx = -1;
  device->lastLaunchedTimestamp = ts;
  if (device->titles) {
    int idx = 0;
    for (Target *t = device->titles->first; t; t = t->next, idx++) {
      if (t == target) {
        device->lastLaunchedTitleIdx = idx;
        break;
      }
    }
  }
  return 0;
}

// Removes the title ID cache file on the device so the next scan will rebuild from storage.
int invalidateTitleIDCache(struct BackendDevice *device) {
  if (!device || device->type == Device_None || !device->mountpoint)
    return -1;
  struct BackendDevice *configDevice = device->metadev ? device->metadev : device;
  char cachePath[PATH_MAX];
  buildConfigFilePath(cachePath, configDevice->mountpoint, titleIDCacheFile);
  if (remove(cachePath) != 0)
    return -1;
  return 0;
}
