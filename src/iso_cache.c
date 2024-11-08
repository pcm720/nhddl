// Implements title ID cache to make bulding target list faster
#include "iso_cache.h"
#include "common.h"
#include "iso.h"
#include "devices.h"
#include "options.h"
#include <malloc.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <string.h>

#define CACHE_MAGIC "NIDC"
#define CACHE_VERSION 2

const char titleIDCacheFile[] = "/cache.bin";
#define MAX_CACHE_PATH_LEN MASS_PLACEHOLDER_LEN + BASE_CONFIG_PATH_LEN + (sizeof(titleIDCacheFile) / sizeof(char))

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

// Saves TargetList into title ID cache on every storage device
int storeTitleIDCache(TargetList *list) {
  if (list->total == 0) {
    return 0;
  }

  // Get total number of valid cache entries
  int total = 0;
  Target *curTitle = list->first;
  while (curTitle != NULL) {
    if (strlen(curTitle->id) == 11) {
      total++;
    }
    curTitle = curTitle->next;
  }
  if (total == 0) {
    printf("WARN: No valid cache entries found\n");
    return 0;
  }

  // Prepare paths and header
  char cachePath[MAX_CACHE_PATH_LEN];
  char dirPath[MAX_CACHE_PATH_LEN];
  buildConfigFilePath(dirPath, MASS_PLACEHOLDER, NULL);
  buildConfigFilePath(cachePath, MASS_PLACEHOLDER, titleIDCacheFile);

  CacheEntryHeader header;
  CacheMetadata meta = {.magic = CACHE_MAGIC, .version = CACHE_VERSION, .total = total};
  for (int i = 0; i < MAX_MASS_DEVICES; i++) {
    if (deviceModeMap[i].mode  == MODE_ALL) {
      break;
    }
    cachePath[4] = i + '0';
    dirPath[4] = i + '0';

    // Get path to config directory and make sure it exists
    struct stat st;
    if (stat(dirPath, &st) == -1) {
      printf("Creating config directory: %s\n", dirPath);
      if (mkdir(dirPath, 0777)) {
        printf("ERROR: Failed to create directory\n");
        continue;
      }
    }

    // Open cache file for writing
    FILE *file = fopen(cachePath, "wb");
    if (file == NULL) {
      printf("ERROR: Failed to open cache file for writing\n");
      continue;
    }

    int result;
    // Write cache file header
    result = fwrite(&meta, sizeof(CacheMetadata), 1, file);
    if (!result) {
      printf("ERROR: Failed to write metadata: %d\n", errno);
      fclose(file);
      remove(cachePath);
      return result;
    }

    // Write each entry
    curTitle = list->first;
    while (curTitle != NULL) {
      if (strlen(curTitle->id) < 11) {
        // Ignore empty entries
        curTitle = curTitle->next;
        continue;
      }

      int mountpointLen = 5;
      if (curTitle->fullPath[5] == ':') {
        mountpointLen = 6;
      }

      // Write entry header
      memcpy(header.titleID, curTitle->id, sizeof(header.titleID));
      header.titleID[11] = '\0';
      header.pathLength = strlen(curTitle->fullPath) - mountpointLen + 1;
      result = fwrite(&header, sizeof(CacheEntryHeader), 1, file);
      if (!result) {
        printf("ERROR: %s: Failed to write header: %d\n", curTitle->name, errno);
        fclose(file);
        remove(cachePath);
        return result;
      }
      // Write full ISO path without the mountpoint
      result = fwrite(curTitle->fullPath + mountpointLen, header.pathLength, 1, file);
      if (!result) {
        printf("ERROR: %s: Failed to write full path: %d\n", curTitle->name, errno);
        fclose(file);
        remove(cachePath);
        return result;
      }
      curTitle = curTitle->next;
    }
    fclose(file);
  }
  return 0;
}

// Loads title ID cache from storage into cache
int loadTitleIDCache(TitleIDCache *cache) {
  cache->total = 0;
  cache->lastMatchedIdx = 0;

  // Open cache file for reading
  char cachePath[MAX_CACHE_PATH_LEN];
  buildConfigFilePath(cachePath, MASS_PLACEHOLDER, titleIDCacheFile);

  FILE *file;
  // Load the first found cache file
  for (int i = 0; i < MAX_MASS_DEVICES; i++) {
    if (deviceModeMap[i].mode  == MODE_ALL) {
      printf("ERROR: failed to open cache file\n");
      return -ENOENT;
    }
    cachePath[4] = i + '0';

    file = fopen(cachePath, "rb");
    if (file != NULL)
      break;
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
  cache->entries = malloc((sizeof(CacheEntry) * meta.total));
  if (cache->entries == NULL) {
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
    cache->entries[readIndex] = entry;
    readIndex++;
  }
  fclose(file);

  // Free unused memory
  if (readIndex != meta.total)
    cache->entries = realloc(cache->entries, sizeof(CacheEntry) * readIndex);

  cache->total = readIndex;
  return 0;
}

// Returns a pointer to title ID or NULL if fullPath is not found in the cache
char *getCachedTitleID(char *fullPath, TitleIDCache *cache) {
  // This code takes advantage of all entries in the title list being sorted alphabetically.
  // By starting from the index of the last matched entry, we can skip comparing fullPath with entries
  // that have already been matched to a title ID, improving lookup speeds for very large lists.

  int mountpointLen = 5;
  if (fullPath[5] == ':') {
    mountpointLen = 6;
  }

  for (int i = cache->lastMatchedIdx; i < cache->total; i++) {
    if (!strcmp(cache->entries[i].fullPath, fullPath + mountpointLen)) {
      cache->lastMatchedIdx = i;
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

  for (int i = 0; i < cache->total; i++) {
    free(cache->entries[i].fullPath);
  }

  free(cache->entries);
  free(cache);
}
