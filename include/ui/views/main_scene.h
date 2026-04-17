#ifndef _UI_VIEWS_MAIN_SCENE_H_
#define _UI_VIEWS_MAIN_SCENE_H_

#include "backends/target.h"
#include "config/arguments.h"
#include "ui/title_list.h"
#include <gsKit.h>
#include <stdint.h>

struct View;

typedef struct MainSceneData {
  TitleListView listView; // Owned by UI; built from backends on UI thread
  char lineBuf[128];
  int exitRequested;
  int selectedIndex;
  int scrollOffset;
  uint32_t filterDeviceMask; // 0 = all devices; else bit i set = include device i
  int favoritesOnly;
  int favoritesOnTop;
  int sortAscending;
  int listDirty;
  int listRebuildPending; // 1 = worker rescan/init in progress; UI must not use title list pointers until cleared
  Target *rebuildSelectionTarget; // Restore selection to this target after rebuild
  int autoLaunchCountdown; // In Hz, depends on refresh rate
  int coverDebounceFramesLeft; // When >0 decrement each frame; at 0 enqueue LoadCover for current selection
  GSTEXTURE *coverTexture;
  Target *coverTarget;
} MainSceneData;

struct View *mainSceneGetView(void);
MainSceneData *mainSceneGetData(void);

void mainSceneInit(void);
void mainSceneCleanup(void);

// Clear view list and cover selection before worker mutates backend title lists (avoids dangling Target*).
void mainSceneBeginBackendResync(MainSceneData *d);

// Test launch with an in-memory merged global+title effective list (e.g. title options). Duplicates before launch; does not save .cnf.
void uiTestLaunchWithTitleArgs(Target *t, ArgumentList *effectiveMerged);

// Launch currently selected title with in-memory global Neutrino args (merged with saved per-title .cnf); does not save or update last-launched.
void uiTestLaunchWithInMemoryGlobalArgs(ArgumentList *globalArgsInMemory);

#endif
