#ifndef _FAVORITES_H_
#define _FAVORITES_H_

#include "target.h"

// Favorites and recently-played tracking.
//
// Persisted per storage device as plain text files in the device's config
// directory (next to cache.bin): nhddl/favorites.txt and nhddl/recent.txt,
// one path per line, relative to the device mountpoint (same convention as
// the title ID cache). Both files are created automatically.

// Loads favorites and recents from every initialized device
void favoritesInit();

// Frees all in-memory state (does not touch the files)
void favoritesFree();

// Returns 1 if the target is marked as a favorite
int favoritesIsFavorite(Target *target);

// Toggles favorite state for the target and saves the device's file
void favoritesToggle(Target *target);

// Returns the recently-played rank of the target
// (0 = most recent), or -1 if the target isn't in the list
int favoritesGetRecentRank(Target *target);

// Prepends the launched title to the recently-played list and saves it.
// Called right before a title is launched.
void favoritesAddRecent(struct DeviceMapEntry *device, char *fullPath);

#endif
