#ifndef _BACKENDS_CACHE_H_
#define _BACKENDS_CACHE_H_

#include "backends/backends.h"
#include <stdint.h>

// In-memory cache entry (path relative to device mountpoint in fullPath)
typedef struct {
  char titleID[12];
  char *fullPath;
  uint64_t fileSize; // File size for change detection
  uint32_t flags;    // TitleFlags bitfield. Mask with known bits when reading.
} CacheEntry;

// In-memory title ID cache
typedef struct TitleIDCache {
  int total;           // Total number of elements in cache
  int lastMatchedIdx;  // Used to skip ahead to the last matched entry when getting title ID from cache
  CacheEntry *entries; // Pointer to cache entry array
} TitleIDCache;

// Loads title ID cache from storage into cache. Returns 0 on success, non-zero on error.
int loadTitleIDCache(TitleIDCache *cache, struct BackendDevice *device);

// Saves TargetList into title ID cache on given storage device. Exported for future UI to persist metadata updates.
// Returns 0 on success, non-zero on error. Can be called without a prior load for that device.
int storeTitleIDCache(TargetList *list, struct BackendDevice *device);

// Returns a pointer to cache entry or NULL if fullPath is not found in the cache.
// Caller should stat() the file and compare st_size to entry->fileSize; if equal, use entry->titleID and entry->flags.
CacheEntry *getCachedEntry(char *fullPath, TitleIDCache *cache);

// Returns a pointer to title ID or NULL if fullPath is not found in the cache.
// Prefer getCachedEntry when you need to validate file size or apply flags.
char *getCachedTitleID(char *fullPath, TitleIDCache *cache);

// Frees memory used by title ID cache. All pointers to cache entries (including title IDs) will be invalid.
void freeTitleCache(TitleIDCache *cache);

#endif
