#include "ui/folder_view.h"
#include "devices/devices.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void folderViewInit(FolderView *fv) {
  if (!fv)
    return;
  fv->rows = NULL;
  fv->count = 0;
  fv->capacity = 0;
  fv->atDeviceList = 1;
  fv->device = NULL;
  fv->pathPrefix[0] = '\0';
}

void folderViewFree(FolderView *fv) {
  if (!fv)
    return;
  free(fv->rows);
  fv->rows = NULL;
  fv->count = 0;
  fv->capacity = 0;
  fv->device = NULL;
  fv->pathPrefix[0] = '\0';
}

static int ensureCapacity(FolderView *fv, int need) {
  if (need <= fv->capacity)
    return 0;
  int cap = fv->capacity ? fv->capacity : 16;
  while (cap < need)
    cap *= 2;
  FolderViewRow *n = realloc(fv->rows, (size_t)cap * sizeof(FolderViewRow));
  if (!n)
    return -1;
  fv->rows = n;
  fv->capacity = cap;
  return 0;
}

static int deviceHasTitles(struct BackendDevice *dev) {
  TargetList *tl = getBackendDeviceTitles(dev);
  if (!tl)
    return 0;
  return tl->first != NULL;
}

static int getListedDeviceCount(void) {
  int n = getBackendDeviceCount();
  int c = 0;
  for (int i = 0; i < n; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (dev && deviceHasTitles(dev))
      c++;
  }
  return c;
}

static void formatDeviceName(struct BackendDevice *dev, char *buf, size_t bufSize) {
  if (!dev || !buf || bufSize == 0) {
    if (buf && bufSize)
      buf[0] = '\0';
    return;
  }
  if (dev->mountpoint && dev->mountpoint[0]) {
    snprintf(buf, bufSize, "%s", dev->mountpoint);
    return;
  }
  snprintf(buf, bufSize, "?");
}

static int folderNameSeen(FolderView *fv, const char *name) {
  for (int i = 0; i < fv->count; i++) {
    if (fv->rows[i].kind == FolderRow_Folder && !strcmp(fv->rows[i].name, name))
      return 1;
  }
  return 0;
}

// Target.path starts with '/' (ata0:/DVD/x.iso → "/DVD/x.iso").
static const char *skipSlashes(const char *p) {
  if (!p)
    return "";
  while (*p == '/' || *p == '\\')
    p++;
  return p;
}

// path relative to mount
// prefix is current folder (no leading/trailing slash). Returns 1 if under prefix.
static int pathUnderPrefix(const char *path, const char *prefix, const char **restOut) {
  path = skipSlashes(path);
  prefix = skipSlashes(prefix);
  size_t plen = strlen(prefix);
  if (plen == 0) {
    *restOut = path;
    return 1;
  }
  if (strncmp(path, prefix, plen) != 0)
    return 0;
  if (path[plen] == '\0') {
    // Path equals the folder prefix itself — not a title under it.
    *restOut = path + plen;
    return 0;
  }
  if (path[plen] != '/' && path[plen] != '\\')
    return 0;
  *restOut = skipSlashes(path + plen + 1);
  return 1;
}

static int cmpRows(const void *a, const void *b) {
  const FolderViewRow *ra = (const FolderViewRow *)a;
  const FolderViewRow *rb = (const FolderViewRow *)b;
  if (ra->kind != rb->kind) {
    if (ra->kind == FolderRow_Device || rb->kind == FolderRow_Device)
      return (int)ra->kind - (int)rb->kind;
    if (ra->kind == FolderRow_Folder && rb->kind == FolderRow_Title)
      return -1;
    if (ra->kind == FolderRow_Title && rb->kind == FolderRow_Folder)
      return 1;
  }
  const char *na = ra->name[0] ? ra->name : (ra->target && ra->target->name ? ra->target->name : "");
  const char *nb = rb->name[0] ? rb->name : (rb->target && rb->target->name ? rb->target->name : "");
  return strcmp(na, nb);
}

static int appendRow(FolderView *fv, const FolderViewRow *row) {
  if (ensureCapacity(fv, fv->count + 1) != 0)
    return -1;
  fv->rows[fv->count++] = *row;
  return 0;
}

static int rebuildDeviceList(FolderView *fv) {
  fv->count = 0;
  fv->atDeviceList = 1;
  fv->device = NULL;
  fv->pathPrefix[0] = '\0';
  int n = getBackendDeviceCount();
  for (int i = 0; i < n; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (!dev || !deviceHasTitles(dev))
      continue;
    FolderViewRow row;
    memset(&row, 0, sizeof(row));
    row.kind = FolderRow_Device;
    row.device = dev;
    formatDeviceName(dev, row.name, sizeof(row.name));
    if (appendRow(fv, &row) != 0)
      return -1;
  }
  if (fv->count > 1)
    qsort(fv->rows, (size_t)fv->count, sizeof(FolderViewRow), cmpRows);
  return 0;
}

static int isHdlDevice(struct BackendDevice *dev) { return dev && dev->type == Device_HDD; }

static int rebuildPathLevel(FolderView *fv, struct BackendDevice *dev, const char *prefix) {
  fv->atDeviceList = 0;
  fv->device = dev;
  // Copy first since appendRow can overwrite row->enterPrefix
  char pfx[FOLDER_VIEW_PREFIX_MAX];
  snprintf(pfx, sizeof(pfx), "%s", prefix ? prefix : "");
  snprintf(fv->pathPrefix, sizeof(fv->pathPrefix), "%s", pfx);
  fv->count = 0;

  TargetList *tl = getBackendDeviceTitles(dev);
  if (!tl)
    return 0;

  // HDL: flat titles only (no path folders).
  int flat = isHdlDevice(dev);

  for (Target *t = tl->first; t; t = t->next) {
    if (flat) {
      FolderViewRow row;
      memset(&row, 0, sizeof(row));
      row.kind = FolderRow_Title;
      row.target = t;
      row.device = dev;
      if (t->name)
        snprintf(row.name, sizeof(row.name), "%s", t->name);
      if (appendRow(fv, &row) != 0)
        return -1;
      continue;
    }

    const char *rest = NULL;
    if (!pathUnderPrefix(t->path ? t->path : "", pfx, &rest))
      continue;
    if (!rest)
      continue;

    const char *slash = strchr(rest, '/');
    if (slash) {
      char folder[FOLDER_VIEW_NAME_MAX];
      size_t flen = (size_t)(slash - rest);
      if (flen == 0 || flen >= sizeof(folder))
        continue;
      memcpy(folder, rest, flen);
      folder[flen] = '\0';
      if (folderNameSeen(fv, folder))
        continue;
      FolderViewRow row;
      memset(&row, 0, sizeof(row));
      row.kind = FolderRow_Folder;
      row.device = dev;
      snprintf(row.name, sizeof(row.name), "%s", folder);
      if (pfx[0])
        snprintf(row.enterPrefix, sizeof(row.enterPrefix), "%s/%s", pfx, folder);
      else
        snprintf(row.enterPrefix, sizeof(row.enterPrefix), "%s", folder);
      if (appendRow(fv, &row) != 0)
        return -1;
    } else if (rest[0]) {
      FolderViewRow row;
      memset(&row, 0, sizeof(row));
      row.kind = FolderRow_Title;
      row.target = t;
      row.device = dev;
      if (t->name)
        snprintf(row.name, sizeof(row.name), "%s", t->name);
      if (appendRow(fv, &row) != 0)
        return -1;
    }
  }

  if (fv->count > 1)
    qsort(fv->rows, (size_t)fv->count, sizeof(FolderViewRow), cmpRows);
  return 0;
}

int folderViewRebuild(FolderView *fv) {
  if (!fv)
    return -1;
  if (fv->atDeviceList)
    return rebuildDeviceList(fv);
  if (!fv->device)
    return folderViewResetRoot(fv);
  return rebuildPathLevel(fv, fv->device, fv->pathPrefix);
}

int folderViewResetRoot(FolderView *fv) {
  if (!fv)
    return -1;
  if (getListedDeviceCount() > 1)
    return rebuildDeviceList(fv);
  // Zero or one device: path root of that device (or empty list).
  fv->atDeviceList = 0;
  fv->device = NULL;
  fv->pathPrefix[0] = '\0';
  int n = getBackendDeviceCount();
  for (int i = 0; i < n; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (dev && deviceHasTitles(dev)) {
      fv->device = dev;
      break;
    }
  }
  if (!fv->device) {
    fv->count = 0;
    return 0;
  }
  return rebuildPathLevel(fv, fv->device, "");
}

int folderViewEnter(FolderView *fv, int rowIndex) {
  if (!fv || rowIndex < 0 || rowIndex >= fv->count)
    return 0;
  FolderViewRow *row = &fv->rows[rowIndex];
  if (row->kind == FolderRow_Device && row->device) {
    struct BackendDevice *dev = row->device;
    return rebuildPathLevel(fv, dev, "") == 0 ? 1 : 0;
  }
  if (row->kind == FolderRow_Folder) {
    struct BackendDevice *dev = row->device;
    char enter[FOLDER_VIEW_PREFIX_MAX];
    snprintf(enter, sizeof(enter), "%s", row->enterPrefix);
    return rebuildPathLevel(fv, dev, enter) == 0 ? 1 : 0;
  }
  return 0;
}

int folderViewGoUp(FolderView *fv) {
  if (!fv)
    return 0;
  if (fv->atDeviceList)
    return 0;
  if (fv->pathPrefix[0]) {
    char *slash = strrchr(fv->pathPrefix, '/');
    if (slash) {
      *slash = '\0';
      return rebuildPathLevel(fv, fv->device, fv->pathPrefix) == 0 ? 1 : 0;
    }
    fv->pathPrefix[0] = '\0';
    return rebuildPathLevel(fv, fv->device, "") == 0 ? 1 : 0;
  }
  // At device path root: up to device list if multi-device.
  if (getListedDeviceCount() > 1)
    return rebuildDeviceList(fv) == 0 ? 1 : 0;
  return 0;
}

const FolderViewRow *folderViewGetRow(const FolderView *fv, int index) {
  if (!fv || index < 0 || index >= fv->count)
    return NULL;
  return &fv->rows[index];
}

int folderViewCount(const FolderView *fv) { return fv ? fv->count : 0; }
