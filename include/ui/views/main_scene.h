#ifndef _UI_VIEWS_MAIN_SCENE_H_
#define _UI_VIEWS_MAIN_SCENE_H_

#include "backends/target.h"
#include "config/arguments.h"
#include "ui/folder_view.h"
#include "ui/title_list.h"
#include <gsKit.h>
#include <stdint.h>

struct View;

typedef struct MainSceneData {
  TitleListView listView; // Owned by UI; built from backends on UI thread
  FolderView folderView;  // Path-aware browse when folderMode is set
  int folderMode;         // 0 = flat title list, 1 = folder browse
  char lineBuf[128];
  int selectedIndex;
  int scrollOffset;
  uint32_t filterDeviceMask; // 0 = all devices; else bit i set = include device i
  int favoritesOnly;
  int favoritesOnTop;
  int sortAscending;
  int listDirty;
  Target *rebuildSelectionTarget; // Restore selection to this target after rebuild
  int autoLaunchCountdown;        // In Hz, depends on refresh rate
  int coverDebounceFramesLeft;    // When >0 decrement each frame; at 0 enqueue LoadCover
  GSTEXTURE *coverTexture;
  Target *coverTarget;
} MainSceneData;

struct View *mainSceneGetView(void);
MainSceneData *mainSceneGetData(void);

void mainSceneInit(void);
void mainSceneCleanup(void);

// Clear view list and cover selection before worker mutates backend title lists.
void mainSceneBeginBackendResync(MainSceneData *d);

// Rebuild view list from backends; updates UiContext.selectedTitle.
void mainSceneRebuildFromBackends(MainSceneData *d);

// Test launch with an in-memory merged global+title effective list.
void uiTestLaunchWithTitleArgs(Target *t, ArgumentList *effectiveMerged);

// Launch currently selected title with in-memory global Neutrino args.
void uiTestLaunchWithInMemoryGlobalArgs(ArgumentList *globalArgsInMemory);

#endif
