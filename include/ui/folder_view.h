#ifndef _UI_FOLDER_VIEW_H_
#define _UI_FOLDER_VIEW_H_

#include "backends/backends.h"
#include "backends/target.h"
#include <stdint.h>

// Path-aware browse over scanned Targets (issue #109). Not a live FS browser.
// Multi-device: root is a device list; one device: path root of that device.
// Ignores flat-list view options (device filter, favorites, sort).

#define FOLDER_VIEW_NAME_MAX 64
#define FOLDER_VIEW_PREFIX_MAX 256

typedef enum {
  FolderRow_Device = 0,
  FolderRow_Folder = 1,
  FolderRow_Title = 2,
} FolderRowKind;

typedef struct FolderViewRow {
  FolderRowKind kind;
  Target *target;                 // Title rows only
  struct BackendDevice *device;   // Device / folder / title's device
  char name[FOLDER_VIEW_NAME_MAX]; // Device or folder label
  char enterPrefix[FOLDER_VIEW_PREFIX_MAX]; // Folder: prefix after enter; unused for device/title
} FolderViewRow;

typedef struct FolderView {
  FolderViewRow *rows;
  int count;
  int capacity;
  int atDeviceList;
  struct BackendDevice *device; // NULL at device list
  char pathPrefix[FOLDER_VIEW_PREFIX_MAX]; // "" = device path root
} FolderView;

void folderViewInit(FolderView *fv);
void folderViewFree(FolderView *fv);

// Rebuild rows for current nav. Returns 0 on success.
int folderViewRebuild(FolderView *fv);

// Reset to root (device list if >1 device, else that device's path root).
int folderViewResetRoot(FolderView *fv);

// Enter selected row (device/folder). Returns 1 if navigated, 0 if not enterable.
int folderViewEnter(FolderView *fv, int rowIndex);

// Go up one level. Returns 1 if moved, 0 if already at top.
int folderViewGoUp(FolderView *fv);

const FolderViewRow *folderViewGetRow(const FolderView *fv, int index);
int folderViewCount(const FolderView *fv);

#endif
