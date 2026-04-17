#include "ui/views/file_selector_scene.h"
#include "backends/backends.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include <dirent.h>
#include <libpad.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define POPUP_W 500
#define POPUP_X ((SCENE_VW - POPUP_W) / 2)
#define POPUP_Y 70
#define MAX_SELECTOR_ITEMS 256
#define VISIBLE_ROWS 10

typedef struct {
  char name[128];
  int isDir;
} SelectorItem;

typedef enum {
  SelectorMode_Devices = 0,
  SelectorMode_Browse = 1,
} SelectorMode;

typedef struct {
  SelectorMode mode;
  SelectorItem items[MAX_SELECTOR_ITEMS];
  int itemCount;
  int selected;
  int scroll;
  char currentPath[PATH_MAX + 1];
  char rootMount[64];
  int boundedMount;
  FileSelectorDoneFn onDone;
  void *doneUserdata;
  FileSelectorDismissFn onDismiss;
  void *dismissUserdata;
} FileSelectorData;

static View s_view;
static FileSelectorData s_data;

static FileSelectorDismissFn s_dismissNotifierPending;
static void *s_dismissUserdataPending;

void fileSelectorSetDismissNotifier(FileSelectorDismissFn fn, void *userdata) {
  s_dismissNotifierPending = fn;
  s_dismissUserdataPending = userdata;
}

static void fileSelectorOnLeave(View *v) {
  (void)v;
  if (s_data.onDismiss)
    s_data.onDismiss(s_data.dismissUserdata);
  s_data.onDismiss = NULL;
  s_data.dismissUserdata = NULL;
}

static int cmpItem(const SelectorItem *a, const SelectorItem *b) {
  if (a->isDir != b->isDir)
    return b->isDir - a->isDir;
  return strcmp(a->name, b->name);
}

static void sortItems(SelectorItem *items, int count) {
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (cmpItem(&items[i], &items[j]) > 0) {
        SelectorItem tmp = items[i];
        items[i] = items[j];
        items[j] = tmp;
      }
    }
  }
}

static int isMountRoot(const char *path, const char *rootMount) {
  if (!path || !rootMount)
    return 0;
  if (strcmp(path, rootMount) == 0)
    return 1;
  char withSlash[80];
  snprintf(withSlash, sizeof(withSlash), "%s/", rootMount);
  return strcmp(path, withSlash) == 0;
}

static void joinPath(char *out, size_t outSize, const char *base, const char *name) {
  size_t len = strlen(base);
  if (len > 0 && base[len - 1] == '/')
    snprintf(out, outSize, "%s%s", base, name);
  else
    snprintf(out, outSize, "%s/%s", base, name);
}

static void enterDeviceList(FileSelectorData *d) {
  d->mode = SelectorMode_Devices;
  d->itemCount = 0;
  d->selected = 0;
  d->scroll = 0;
  d->rootMount[0] = '\0';
  d->currentPath[0] = '\0';

  int n = getBackendDeviceCount();
  for (int i = 0; i < n && d->itemCount < MAX_SELECTOR_ITEMS; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (!dev || !dev->mountpoint || !dev->mountpoint[0])
      continue;
    snprintf(d->items[d->itemCount].name, sizeof(d->items[d->itemCount].name), "%s", dev->mountpoint);
    d->items[d->itemCount].isDir = 1;
    d->itemCount++;
  }
}

static void loadDirectory(FileSelectorData *d, const char *path) {
  d->mode = SelectorMode_Browse;
  d->itemCount = 0;
  d->selected = 0;
  d->scroll = 0;
  snprintf(d->currentPath, sizeof(d->currentPath), "%s", path);

  if (!isMountRoot(d->currentPath, d->rootMount)) {
    snprintf(d->items[d->itemCount].name, sizeof(d->items[d->itemCount].name), "..");
    d->items[d->itemCount].isDir = 1;
    d->itemCount++;
  }

  DIR *dir = opendir(d->currentPath);
  if (!dir) {
    showOSD("Path open failed", 120);
    return;
  }

  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL && d->itemCount < MAX_SELECTOR_ITEMS) {
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
      continue;

    SelectorItem *it = &d->items[d->itemCount];
    snprintf(it->name, sizeof(it->name), "%s", entry->d_name);

    char fullPath[PATH_MAX + 1];
    joinPath(fullPath, sizeof(fullPath), d->currentPath, entry->d_name);

    struct stat st = {0};
    it->isDir = (stat(fullPath, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
    d->itemCount++;
  }
  closedir(dir);

  int start = 0;
  if (d->itemCount > 0 && !strcmp(d->items[0].name, ".."))
    start = 1;
  sortItems(&d->items[start], d->itemCount - start);
}

static void fileSelectorOnEnter(View *v) {
  (void)v;
  if (s_data.boundedMount && s_data.rootMount[0] != '\0') {
    const char *start = s_data.currentPath[0] ? s_data.currentPath : s_data.rootMount;
    loadDirectory(&s_data, start);
    return;
  }
  if (s_data.currentPath[0] != '\0') {
    char mount[64];
    if (getMountpointFromPath(s_data.currentPath, mount, sizeof(mount)) == 0) {
      snprintf(s_data.rootMount, sizeof(s_data.rootMount), "%s", mount);
      loadDirectory(&s_data, s_data.currentPath);
      return;
    }
  }
  enterDeviceList(&s_data);
}

static void fileSelectorDraw(View *v, int zOrder) {
  (void)v;
  FileSelectorData *d = &s_data;

  int rows = d->itemCount;
  if (rows < 2)
    rows = 2;
  if (rows > VISIBLE_ROWS)
    rows = VISIBLE_ROWS;

  int popupH = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (rows + 2)) + POPUP_PROMPT_ROW_OFFSET;
  int vx1 = POPUP_X;
  int vy1 = POPUP_Y;
  int vx2 = POPUP_X + POPUP_W;
  int vy2 = POPUP_Y + popupH;
  int centerX = (vx1 + vx2) / 2;
  int leftX = vx1 + SCENE_KEEPOUT;
  int rightX = vx2 - SCENE_KEEPOUT;

  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  fontRenderString(FONT_DEFAULT, centerX, vy1 + SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder,
                   (d->mode == SelectorMode_Devices) ? "Select device" : "Select file", HeaderTextColor);

  if (d->mode == SelectorMode_Browse) {
    fontRenderInRect(FONT_DEFAULT, leftX, vy1 + SCENE_KEEPOUT + 2, rightX, vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE,
                     FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, d->currentPath, HeaderTextColor);
  }

  int first = d->scroll;
  int last = first + rows;
  if (last > d->itemCount)
    last = d->itemCount;

  int entryY = vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE;
  for (int i = first; i < last; i++) {
    uint64_t col = (i == d->selected) ? ColorSelected : FontMainColor;
    char line[160];
    if (d->items[i].isDir)
      snprintf(line, sizeof(line), "%s/", d->items[i].name);
    else
      snprintf(line, sizeof(line), "%s", d->items[i].name);
    fontRenderInRect(FONT_DEFAULT, leftX, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, line, col);
    entryY += fontGetLineHeight(0);
  }

  uiDrawModalPrompt(gsGlobal, vx1, vx2, 0, vy2, zOrder, HeaderTextColor, ICON_CROSS, "Open");
  uiDrawModalPrompt(gsGlobal, vx1, vx2, 1, vy2, zOrder, HeaderTextColor, ICON_CIRCLE, "Back");
}

static void updateScroll(FileSelectorData *d) {
  if (d->selected < d->scroll)
    d->scroll = d->selected;
  if (d->selected >= d->scroll + VISIBLE_ROWS)
    d->scroll = d->selected - VISIBLE_ROWS + 1;
  if (d->scroll < 0)
    d->scroll = 0;
}

static void navigateParent(FileSelectorData *d) {
  if (isMountRoot(d->currentPath, d->rootMount)) {
    enterDeviceList(d);
    return;
  }

  char *slash = strrchr(d->currentPath, '/');
  if (slash && slash > d->currentPath) {
    *slash = '\0';
    loadDirectory(d, d->currentPath);
  } else {
    enterDeviceList(d);
  }
}

static ViewResult fileSelectorOnInput(View *v, int padInput) {
  (void)v;
  FileSelectorData *d = &s_data;

  if (padInput & PAD_CIRCLE) {
    if (d->boundedMount && d->mode == SelectorMode_Browse && isMountRoot(d->currentPath, d->rootMount))
      return ViewResult_Pop;
    if (d->mode == SelectorMode_Devices)
      return ViewResult_Pop;
    navigateParent(d);
    return ViewResult_Continue;
  }

  if (d->itemCount <= 0)
    return ViewResult_Continue;

  if (padInput & PAD_UP) {
    d->selected--;
    if (d->selected < 0)
      d->selected = d->itemCount - 1;
    updateScroll(d);
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    d->selected++;
    if (d->selected >= d->itemCount)
      d->selected = 0;
    updateScroll(d);
    return ViewResult_Continue;
  }

  if (padInput & PAD_CROSS) {
    const SelectorItem *it = &d->items[d->selected];
    if (d->mode == SelectorMode_Devices) {
      snprintf(d->rootMount, sizeof(d->rootMount), "%s", it->name);
      loadDirectory(d, it->name);
      return ViewResult_Continue;
    }

    if (it->isDir) {
      if (!strcmp(it->name, "..")) {
        navigateParent(d);
      } else {
        char nextPath[PATH_MAX + 1];
        joinPath(nextPath, sizeof(nextPath), d->currentPath, it->name);
        loadDirectory(d, nextPath);
      }
      return ViewResult_Continue;
    }

    char selectedPath[PATH_MAX + 1];
    joinPath(selectedPath, sizeof(selectedPath), d->currentPath, it->name);
    if (d->onDone)
      d->onDone(selectedPath, d->doneUserdata);
    return ViewResult_Pop;
  }
  return ViewResult_Continue;
}

void fileSelectorConfigure(const char *initialPath, FileSelectorDoneFn onDone, void *userdata) {
  FileSelectorDismissFn d = s_dismissNotifierPending;
  void *du = s_dismissUserdataPending;
  s_dismissNotifierPending = NULL;
  s_dismissUserdataPending = NULL;
  memset(&s_data, 0, sizeof(s_data));
  s_data.onDismiss = d;
  s_data.dismissUserdata = du;
  s_data.onDone = onDone;
  s_data.doneUserdata = userdata;
  if (initialPath && initialPath[0] != '\0')
    snprintf(s_data.currentPath, sizeof(s_data.currentPath), "%s", initialPath);
}

void fileSelectorConfigureForMount(const char *mountRoot, FileSelectorDoneFn onDone, void *userdata) {
  FileSelectorDismissFn d = s_dismissNotifierPending;
  void *du = s_dismissUserdataPending;
  s_dismissNotifierPending = NULL;
  s_dismissUserdataPending = NULL;
  memset(&s_data, 0, sizeof(s_data));
  s_data.onDismiss = d;
  s_data.dismissUserdata = du;
  s_data.boundedMount = 1;
  s_data.onDone = onDone;
  s_data.doneUserdata = userdata;
  if (mountRoot && mountRoot[0])
    snprintf(s_data.rootMount, sizeof(s_data.rootMount), "%s", mountRoot);
  snprintf(s_data.currentPath, sizeof(s_data.currentPath), "%s", s_data.rootMount);
}

struct View *fileSelectorGetView(void) {
  s_view = (View){
      .type = ViewType_Modal,
      .userdata = &s_data,
      .onEnter = fileSelectorOnEnter,
      .onLeave = fileSelectorOnLeave,
      .draw = fileSelectorDraw,
      .onInput = fileSelectorOnInput,
  };
  return &s_view;
}
