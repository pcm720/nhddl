#ifndef _TITLE_CACHE_H_
#define _TITLE_CACHE_H_

#include "iso.h"

typedef struct {
  char titleID[12];
  char *fullPath;
} CacheEntry;

typedef struct TitleIDCache {
  int total;           // Total number of elements in cache
  int lastMatchedIdx;  // Used to skip ahead to the last matched entry when getting title ID from cache
  CacheEntry *entries; // Pointer to cache entry array
} TitleIDCache;

// Saves TargetList into title ID cache
int storeTitleIDCache(TargetList *list);

// Loads title ID cache from storage into cache
int loadTitleIDCache(TitleIDCache *cache);

// Returns a pointer to title ID or NULL if path doesn't exist in cache
char *getCachedTitleID(char *fullPath, TitleIDCache *cache);

// Frees memory used by title ID cache
// All pointers to cache entries (including title IDs) will be invalid
void freeTitleCache(TitleIDCache *cache);

#endif
