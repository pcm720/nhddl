#include "ui/title_list.h"
#include "backends/backends.h"
#include "dprintf.h"
#include <stdlib.h>
#include <string.h>

void titleListViewInit(TitleListView *v) {
  v->first = NULL;
  v->last = NULL;
  v->count = 0;
}

void titleListViewFree(TitleListView *v) { titleListViewClear(v); }

void titleListViewClear(TitleListView *v) {
  struct TitleListViewNode *n = v->first;
  while (n) {
    struct TitleListViewNode *next = n->next;
    free(n);
    n = next;
  }
  v->first = NULL;
  v->last = NULL;
  v->count = 0;
}

static struct TitleListViewNode *appendNode(TitleListView *v, Target *t) {
  struct TitleListViewNode *n = malloc(sizeof(*n));
  if (!n)
    return NULL;
  n->target = t;
  n->next = NULL;
  if (v->last)
    v->last->next = n;
  else
    v->first = n;
  v->last = n;
  v->count++;
  return n;
}

int titleListViewBuildFromBackends(TitleListView *v) {
  int n = getBackendDeviceCount();
  for (int i = 0; i < n; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    TargetList *tl = getBackendDeviceTitles(dev);
    DPRINTF("ui/title_list: device %s has %d titles\n", dev->mountpoint, tl ? tl->total : 0);
    if (!tl)
      continue;
    Target *cur = tl->first;
    while (cur) {
      if (!appendNode(v, cur)) {
        titleListViewClear(v);
        return -1;
      }
      cur = cur->next;
    }
  }
  return 0;
}

int titleListViewBuildFromBackendsFiltered(TitleListView *v, uint32_t deviceMask, int favoritesOnly) {
  int n = getBackendDeviceCount();
  for (int i = 0; i < n; i++) {
    if (deviceMask != 0 && !(deviceMask & (1u << i)))
      continue;
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (!dev)
      continue;
    TargetList *tl = getBackendDeviceTitles(dev);
    if (!tl)
      continue;
    Target *cur = tl->first;
    while (cur) {
      if (favoritesOnly && !(cur->flags & TitleFlag_Favorite)) {
        cur = cur->next;
        continue;
      }
      if (!appendNode(v, cur)) {
        titleListViewClear(v);
        return -1;
      }
      cur = cur->next;
    }
  }
  return 0;
}

static int compareTargetName(const void *a, const void *b) {
  const Target *ta = *(const Target *const *)a;
  const Target *tb = *(const Target *const *)b;
  const char *na = ta && ta->name ? ta->name : "";
  const char *nb = tb && tb->name ? tb->name : "";
  return strcmp(na, nb);
}

static int s_sortAscending;
static int s_sortFavoritesOnTop;

static int compareTargetForSort(const void *a, const void *b) {
  const Target *ta = *(const Target *const *)a;
  const Target *tb = *(const Target *const *)b;
  if (s_sortFavoritesOnTop) {
    int fa = (ta && (ta->flags & TitleFlag_Favorite)) ? 1 : 0;
    int fb = (tb && (tb->flags & TitleFlag_Favorite)) ? 1 : 0;
    if (fa != fb)
      return fb - fa; // Favorites first
  }
  int cmp = compareTargetName(a, b);
  return s_sortAscending ? cmp : -cmp;
}

int titleListViewSort(TitleListView *v, int ascending, int favoritesOnTop) {
  if (v->count <= 1)
    return 0;
  int count = v->count;
  Target **arr = malloc((size_t)count * sizeof(Target *));
  if (!arr)
    return -1;
  for (int i = 0; i < count; i++)
    arr[i] = titleListViewGetAt(v, i);
  s_sortAscending = ascending;
  s_sortFavoritesOnTop = favoritesOnTop ? 1 : 0;
  qsort(arr, (size_t)count, sizeof(Target *), compareTargetForSort);
  titleListViewClear(v);
  for (int i = 0; i < count; i++) {
    if (!appendNode(v, arr[i])) {
      free(arr);
      return -1;
    }
  }
  free(arr);
  return 0;
}

Target *titleListViewGetAt(const TitleListView *v, int index) {
  if (index < 0 || index >= v->count)
    return NULL;
  struct TitleListViewNode *n = v->first;
  while (index-- > 0)
    n = n->next;
  return n->target;
}

int titleListViewGetIdx(const TitleListView *v, Target *t) {
  if (!t)
    return -1;
  int idx = 0;
  for (struct TitleListViewNode *n = v->first; n; n = n->next, idx++) {
    if (n->target == t)
      return idx;
  }
  return -1;
}

int titleListViewCount(const TitleListView *v) { return v->count; }
