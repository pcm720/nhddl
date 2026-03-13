#ifndef _UI_TITLE_LIST_H_
#define _UI_TITLE_LIST_H_

#include "backends/target.h"
#include <stddef.h>
#include <stdint.h>

// View list over the per-device master lists. Holds pointers into backend device->titles
// only; no copying. Linked list so length can be 1 to 1000+ without fixed capacity.
// Categories and sort are session-only. Rebuild when entering main scene or view options change.

struct TitleListViewNode {
  Target *target;
  struct TitleListViewNode *next;
};

typedef struct TitleListView {
  struct TitleListViewNode *first;
  struct TitleListViewNode *last;
  int count;
} TitleListView;

// Initializes an empty view list.
void titleListViewInit(TitleListView *v);

// Frees all list nodes; does not free any Target.
void titleListViewFree(TitleListView *v);

// Builds the view list from all initialized backend devices: appends a pointer to each
// Target from each device->titles. Does not clear existing entries; call titleListViewClear
// first if rebuilding. Returns 0 on success, -1 on allocation failure.
int titleListViewBuildFromBackends(TitleListView *v);

// Same as BuildFromBackends but filters by device mask and/or favorites only.
// deviceMask 0 = all devices; otherwise only devices with (deviceMask & (1u << i)) are included.
// favoritesOnly 0 = all titles, 1 = only TitleFlag_Favorite.
int titleListViewBuildFromBackendsFiltered(TitleListView *v, uint32_t deviceMask, int favoritesOnly);

// Sorts the list by target name. ascending 1 = A–Z, 0 = Z–A. favoritesOnTop 1 = favorites first, then by name order.
// Does not clear or rebuild.
int titleListViewSort(TitleListView *v, int ascending, int favoritesOnTop);

// Removes all entries (frees nodes only; does not free targets). Count becomes 0.
void titleListViewClear(TitleListView *v);

// Returns the Target* at index (0-based), or NULL if index out of range. O(index).
Target *titleListViewGetAt(const TitleListView *v, int index);

// Returns the Target index or -1 if target was not found in list
int titleListViewGetIdx(const TitleListView *v, Target *);

// Returns the number of entries in the view list.
int titleListViewCount(const TitleListView *v);

#endif
