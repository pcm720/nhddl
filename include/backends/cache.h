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

// Returns a pointer to cache entry or NULL if path is not found in the cache.
// path must be relative to device mountpoint (same format as stored in cache entries).
CacheEntry *getCachedEntry(char *path, TitleIDCache *cache);

// Returns a pointer to title ID or NULL if path is not found in the cache.
// path must be relative to device mountpoint. Prefer getCachedEntry when you need to validate file size or apply flags.
char *getCachedTitleID(char *path, TitleIDCache *cache);

// Frees memory used by title ID cache. All pointers to cache entries (including title IDs) will be invalid.
void freeTitleCache(TitleIDCache *cache);

// Reads lastTitle.bin for device and sets device->lastLaunchedTitleIdx to the matching index in device->titles.
// Call after device->titles is populated (e.g. after scan). Sets lastLaunchedTitleIdx to -1 if no file or no match.
void loadLastLaunchedIndex(struct BackendDevice *device);

// Writes last launched title (target->path) into lastTitle file on device and sets device->lastLaunchedTitleIdx.
int updateLastLaunchedTitle(Target *target);

// Removes the title ID cache file on the device so the next scan will rebuild from storage.
// Returns 0 on success, -1 if file did not exist or could not be removed.
int invalidateTitleIDCache(struct BackendDevice *device);

#endif
