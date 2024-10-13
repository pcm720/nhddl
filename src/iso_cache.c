// Implements title ID cache to make bulding target list faster
#include "iso_cache.h"
#include "common.h"
#include "iso.h"
#include "options.h"
#include <fcntl.h>
#include <malloc.h>
#include <ps2sdkapi.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define CACHE_MAGIC "NIDC"
#define CACHE_VERSION 1

const char titleIDCacheFile[] = "/cache.bin";
#define MAX_CACHE_PATH_LEN STORAGE_BASE_PATH_LEN + BASE_CONFIG_PATH_LEN + (sizeof(titleIDCacheFile) / sizeof(char))

// Structs used to read and write cache file contents
typedef struct {
  char titleID[12];
  size_t pathLength; // Includes null-terminator
} CacheEntryHeader;

typedef struct {
  char magic[4];   // Must be always equal to CACHE_MAGIC
  uint8_t version; // Cache version
  int total;       // Total number of elements in cache file
} CacheMetadata;

// Saves TargetList into title ID cache
int storeTitleIDCache(TargetList *list) {
  if (list->total == 0) {
    return 0;
  }

  char cachePath[MAX_CACHE_PATH_LEN];

  // Get path to config directory and make sure it exists
  buildConfigFilePath(cachePath, NULL);
  struct stat st;
  if (stat(cachePath, &st) == -1) {
    printf("Creating config directory: %s\n", cachePath);
    mkdir(cachePath, 0777);
  }

  // Open cache file for writing
  buildConfigFilePath(cachePath, titleIDCacheFile);
  FILE *file = fopen(cachePath, "wb");
  if (file == NULL) {
    printf("ERROR: failed to open cache file for writing: %d\n", errno);
    return -errno;
  }
  Target *curTitle = list->first;

  int result;
  // Write cache file header
  CacheMetadata meta = {.magic = CACHE_MAGIC, .version = CACHE_VERSION, .total = list->total};
  result = fwrite(&meta, sizeof(CacheMetadata), 1, file);
  if (!result) {
    printf("failed to write metadata: %d\n", errno);
    fclose(file);
    remove(cachePath);
    return result;
  }

  // Write each entry
  CacheEntryHeader header;
  while (curTitle != NULL) {
    // Write entry header
    memcpy(header.titleID, curTitle->id, sizeof(header.titleID));
    header.titleID[11] = '\0';
    header.pathLength = strlen(curTitle->fullPath) + 1;
    result = fwrite(&header, sizeof(CacheEntryHeader), 1, file);
    if (!result) {
      printf("%s: failed to write header: %d\n", curTitle->name, errno);
      fclose(file);
      remove(cachePath);
      return result;
    }
    // Write full ISO path
    result = fwrite(curTitle->fullPath, header.pathLength, 1, file);
    if (!result) {
      printf("%s: failed to write full path: %d\n", curTitle->name, errno);
      fclose(file);
      remove(cachePath);
      return result;
    }
    curTitle = curTitle->next;
  }
  fclose(file);
  return 0;
}

// Loads title ID cache from storage into cache
int loadTitleIDCache(TitleIDCache *cache) {
  cache->total = 0;
  cache->lastVisitedIndex = 0;

  // Open cache file for reading
  char cachePath[MAX_CACHE_PATH_LEN];
  buildConfigFilePath(cachePath, titleIDCacheFile);

  // For some reason, fopen with non-existing file causes
  // all consecutive calls to fopen for the same path to fail
  // with errno 12 with trap exception in PCSX2.
  // To work around that, check if cache file exists first
  if (access(cachePath, F_OK)) {
    printf("Cache file doesn't exist\n");
    return -ENOENT;
  }

  FILE *file = fopen(cachePath, "rb");
  if (file == NULL) {
    printf("ERROR: failed to open cache file: %d\n", -errno);
    return -errno;
  }
  int result;

  // Read cache file header
  CacheMetadata meta;
  result = fread(&meta, sizeof(CacheMetadata), 1, file);
  if (!result) {
    printf("ERROR: Failed to read cache metadata\n");
    fclose(file);
    return result;
  }

  // Make sure header is valid
  if (!strcmp(meta.magic, CACHE_MAGIC)) {
    printf("ERROR: Cache magic doesn't match, refusing to load\n");
    fclose(file);
    return -EINVAL;
  }
  if (meta.version != CACHE_VERSION) {
    printf("ERROR: Unsupported cache version %d\n", meta.version);
    fclose(file);
    return -EINVAL;
  }

  // Allocate memory for cache entries based on total entry count from header metadata
  int readIndex = 0;
  CacheEntry *cacheEntries = malloc((sizeof(CacheEntry) * meta.total));
  if (cacheEntries == NULL) {
    printf("ERROR: Can't allocate enough memory\n");
    fclose(file);
    return -ENOMEM;
  }

  // Read each entry into cache
  CacheEntryHeader header;
  char pathBuf[PATH_MAX + 1];
  while (!feof(file)) {
    // Read cache entry header
    pathBuf[0] = '\0';
    result = fread(&header, sizeof(CacheEntryHeader), 1, file);
    if (result != 1) {
      if (!feof(file))
        printf("WARN: Read less than expected, title ID cache might be incomplete\n");
      break;
    }
    // Read ISO path
    result = fread(&pathBuf, header.pathLength, 1, file);
    if (result != 1) {
      printf("WARN: Read less than expected, title ID cache might be incomplete\n");
      break;
    }
    pathBuf[header.pathLength] = '\0';

    // Store entry in cache
    CacheEntry entry;
    memcpy(entry.titleID, header.titleID, sizeof(entry.titleID));
    header.titleID[11] = '\0';
    entry.fullPath = strdup(pathBuf);
    cacheEntries[readIndex] = entry;
    readIndex++;
  }
  fclose(file);

  // Free unused memory
  if (readIndex != meta.total)
    cacheEntries = realloc(cacheEntries, sizeof(CacheEntry) * readIndex);

  cache->total = readIndex;
  cache->entries = cacheEntries;
  return 0;
}

// Returns a pointer to title ID or NULL if path doesn't exist in cache
char *getCachedTitleID(char *fullPath, TitleIDCache *cache) {
  for (int i = 0; i < cache->total; i++) {
    if (!strcmp(cache->entries[i].fullPath, fullPath)) {
      return cache->entries[i].titleID;
    }
  }
  return NULL;
}

// Frees memory used by title ID cache
// All pointers to cache entries (including title IDs) will be invalid
void freeTitleCache(TitleIDCache *cache) {
  if (cache == NULL)
    return;

  free(cache->entries);
  free(cache);
}