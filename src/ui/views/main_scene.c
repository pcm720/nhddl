#include "ui/views/main_scene.h"
#include "backends/backends.h"
#include "backends/cache.h"
#include "backends/target.h"
#include "config/arguments.h"
#include "config/config.h"
#include "config/neutrino_args.h"
#include "devices/utils.h"
#include "dprintf.h"
#include "neutrino/neutrino.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/png.h"
#include "ui/scale.h"
#include "ui/title_list.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/menu_scene.h"
#include "ui/views/title_options_scene.h"
#include "ui/views/view_options_popup.h"
#include "ui/worker/worker.h"
#include <dirent.h>
#include <fcntl.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <libpad.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define OVERLAY_DURATION_FRAMES 120

#define HEADER_Y (SCENE_KEEPOUT)
#define LIST_LEFT (SCENE_KEEPOUT * 2)
#define LIST_TOP (SCENE_KEEPOUT + SCENE_ROW_SIZE)
#define COVER_W 140
#define COVER_H 200
#define COVER_LEFT (SCENE_VW - LIST_LEFT - COVER_W)
#define LIST_RIGHT COVER_LEFT - SCENE_KEEPOUT
#define COVER_TOP (LIST_TOP + (SCENE_CONTENT_BOTTOM - LIST_TOP) / 2 - COVER_H / 2)
#define COVER_BORDER_MARGIN 2
#define ART_PATH "/ART"
#define COVER_DEBOUNCE_FRAMES 18

static View s_view;
static MainSceneData s_data;
static int s_autolaunchCanceled;
static int s_maxVisible = 0;
static int s_lineHeight = 0;

void uiLaunchTarget();
static char *getISOFormat(uint32_t titleFlags);

void mainSceneBeginBackendResync(MainSceneData *d) {
  if (!d)
    return;
  d->filterDeviceMask = 0;
  titleListViewClear(&d->listView);
  d->selectedIndex = -1;
  d->scrollOffset = 0;
  d->coverTarget = NULL;
  d->coverDebounceFramesLeft = 0;
  d->rebuildSelectionTarget = NULL;
  d->listDirty = 0;
  d->listRebuildPending = 1;
  s_autolaunchCanceled = 1;
  d->autoLaunchCountdown = 0;
}

void mainSceneInit(void) {
  titleListViewInit(&s_data.listView);
  s_data.filterDeviceMask = 0;
  s_data.favoritesOnTop = 0;
  s_data.sortAscending = 1;
  s_data.exitRequested = 0;
  s_data.coverTexture = NULL;
  s_data.coverTarget = NULL;
  s_data.listRebuildPending = 0;
  s_data.rebuildSelectionTarget = NULL;
  s_data.coverDebounceFramesLeft = 0;
  s_data.autoLaunchCountdown = getAutolaunchTimeout() * scaleGetRefreshRateHz();
  if (!s_data.autoLaunchCountdown)
    s_autolaunchCanceled = 1;
  s_data.selectedIndex = -1;

  s_lineHeight = fontGetLineHeight(FONT_DEFAULT);
  if (s_lineHeight <= 0)
    s_lineHeight = 20;
  s_maxVisible = (SCENE_CONTENT_BOTTOM - LIST_TOP) / s_lineHeight;
  if (s_maxVisible < 1)
    s_maxVisible = 1;
}

void mainSceneCleanup(void) {
  if (s_data.coverTexture) {
    if (s_data.coverTexture->Mem)
      free(s_data.coverTexture->Mem);
    if (s_data.coverTexture->Clut)
      free(s_data.coverTexture->Clut);
    free(s_data.coverTexture);
    s_data.coverTexture = NULL;
  }
  titleListViewFree(&s_data.listView);
}

// View list is owned by UI and built from backends on UI thread. Worker only inits/rescans devices; UI shows "Rebuilding list..." until worker
// reports ready.

static void enqueueLoadCover(MainSceneData *d, Target *t) {
  if (!t || !t->device || !t->id)
    return;

  if (!d->coverTexture) {
    d->coverTexture = calloc(1, sizeof(GSTEXTURE));
    if (!d->coverTexture)
      return;
    d->coverTexture->Delayed = 1;
    d->coverTexture->Filter = GS_FILTER_LINEAR;
  } else {
    if (d->coverTexture->Mem) {
      free(d->coverTexture->Mem);
      d->coverTexture->Mem = NULL;
    }
    if (d->coverTexture->Clut) {
      free(d->coverTexture->Clut);
      d->coverTexture->Clut = NULL;
    }
    gsKit_TexManager_invalidate(gsGlobal, d->coverTexture);
  }

  struct BackendDevice *dev = t->device;
  if (dev->metadev)
    dev = dev->metadev;

  d->coverTarget = t;
  snprintf(d->lineBuf, sizeof(d->lineBuf), "%s%s/%s_COV.png", dev->mountpoint, ART_PATH, t->id);
  workerEnqueueLoadCover(d->lineBuf, gsGlobal, d->coverTexture);
}

// filterDeviceMask uses backend slot index bits; after enabling/disabling device types, indices shift.
// If no selected bit matches any current device, clear filter so the list is not empty by mistake.
static void mainSceneNormalizeDeviceFilter(MainSceneData *d) {
  if (!d || d->filterDeviceMask == 0)
    return;
  int n = getBackendDeviceCount();
  uint32_t valid = 0;
  for (int i = 0; i < n; i++)
    valid |= (1u << (unsigned)i);
  if ((d->filterDeviceMask & valid) == 0)
    d->filterDeviceMask = 0;
}

static void rebuildViewListFromBackends(MainSceneData *d) {
  mainSceneNormalizeDeviceFilter(d);
  titleListViewClear(&d->listView);
  if (titleListViewBuildFromBackendsFiltered(&d->listView, d->filterDeviceMask, d->favoritesOnly) != 0)
    return;
  titleListViewSort(&d->listView, d->sortAscending, d->favoritesOnTop);
  d->selectedIndex = 0;
  d->scrollOffset = 0;
  d->listDirty = 0;
}

static void mainSceneOnEnter(View *v) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  if (d->selectedIndex == -1) {
    rebuildViewListFromBackends(d);
    Target *last = getLastLaunchedTarget();
    if (last) {
      enqueueLoadCover(d, last);
      d->selectedIndex = titleListViewGetIdx(&d->listView, last);
      if (d->selectedIndex < 0) {
        s_autolaunchCanceled = 1;
        d->autoLaunchCountdown = 0;
        d->selectedIndex = 0;
      }
    }
  }
}

static void mainSceneOnLeave(View *v) {}

static void mainSceneDraw(View *v, int zOrder) {
  MainSceneData *d = (MainSceneData *)v->userdata;

  if (d->listRebuildPending && workerPollJobDone()) {
    (void)workerGetAndClearListNeedsRebuild();
    Target *prev = d->rebuildSelectionTarget;
    rebuildViewListFromBackends(d);
    d->selectedIndex = titleListViewGetIdx(&d->listView, prev);
    if (d->selectedIndex < 0)
      d->selectedIndex = 0;
    d->scrollOffset = 0;
    d->listRebuildPending = 0;
    d->rebuildSelectionTarget = NULL;
  }

  if (d->listDirty && !d->listRebuildPending) {
    Target *prev =
        (d->selectedIndex >= 0 && d->selectedIndex < titleListViewCount(&d->listView)) ? titleListViewGetAt(&d->listView, d->selectedIndex) : NULL;
    titleListViewClear(&d->listView);
    if (titleListViewBuildFromBackendsFiltered(&d->listView, d->filterDeviceMask, d->favoritesOnly) == 0) {
      titleListViewSort(&d->listView, d->sortAscending, d->favoritesOnTop);
      d->selectedIndex = titleListViewGetIdx(&d->listView, prev);
      if (d->selectedIndex < 0)
        d->selectedIndex = 0;
    }
    d->scrollOffset = 0;
    d->listDirty = 0;
  }

  // Header + prompts (safe while list is cleared; do not touch title rows or cover until rebuild completes)
  fontRenderInRect(FONT_DEFAULT, 0, HEADER_Y, SCENE_VW, LIST_TOP, FONT_ALIGN_TOP | FONT_ALIGN_HCENTER, zOrder, "Title List", HeaderTextColor);
  uiDrawScenePrompt(gsGlobal, 0, zOrder, HeaderTextColor, ICON_TRIANGLE, "Title options");
  uiDrawScenePrompt(gsGlobal, 1, zOrder, HeaderTextColor, ICON_SELECT, "View");
  uiDrawScenePrompt(gsGlobal, 2, zOrder, HeaderTextColor, ICON_START, "Menu");
  uiDrawScenePrompt(gsGlobal, 3, zOrder, HeaderTextColor, ICON_CROSS, "Launch");

  if (d->listRebuildPending) {
    fontRenderInRect(FONT_DEFAULT, 0, LIST_TOP, SCENE_VW, SCENE_CONTENT_BOTTOM, FONT_ALIGN_CENTER, zOrder, "Updating devices...", FontMainColor);
    return;
  }

  int count = titleListViewCount(&d->listView);
  if (count > 0 && d->selectedIndex >= 0 && d->selectedIndex < count) {
    snprintf(d->lineBuf, sizeof(d->lineBuf), "%d / %d", d->selectedIndex + 1, count);
    fontRenderInRect(FONT_DEFAULT, 0, HEADER_Y, SCENE_VW - LIST_LEFT, LIST_TOP, FONT_ALIGN_TOP | FONT_ALIGN_RIGHT, zOrder, d->lineBuf,
                     HeaderTextColor);
  }

  // Title list
  Target *cur = (count > 0 && d->selectedIndex >= 0 && d->selectedIndex < count) ? titleListViewGetAt(&d->listView, d->selectedIndex) : NULL;
  if (count == 0) {
    fontRenderInRect(FONT_DEFAULT, 0, 0, SCENE_VW, SCENE_CONTENT_BOTTOM, FONT_ALIGN_CENTER, zOrder, "No titles", FontMainColor);
    return;
  }
  // Cover art: ratio-preserving border and content so they match in widescreen.
  // In widescreen draw at 1.5x size, centered in the same slot.
  int coverW = COVER_W, coverH = COVER_H, coverLeft = COVER_LEFT, coverTop = COVER_TOP;
  if (scaleGetDisplayAspect() == ASPECT_16_9) {
    coverW = COVER_W * 1.2;
    coverH = COVER_H * 1.2;
    coverLeft = COVER_LEFT + COVER_W / 2 - coverW / 2;
    coverTop = COVER_TOP + COVER_H / 2 - coverH / 2;
  }
  uiDrawTextureInVirtualRect(gsGlobal, coverLeft - COVER_BORDER_MARGIN, coverTop - COVER_BORDER_MARGIN, coverW + COVER_BORDER_MARGIN * 2,
                             coverH + COVER_BORDER_MARGIN * 2, NULL, zOrder, FontMainColor, 0);

  if (cur && cur != d->coverTarget && d->coverDebounceFramesLeft <= 0)
    d->coverDebounceFramesLeft = COVER_DEBOUNCE_FRAMES;
  if (d->coverDebounceFramesLeft > 0)
    d->coverDebounceFramesLeft--;
  if (workerPollCoverLoadDone() && d->coverDebounceFramesLeft == 0 && cur && cur != d->coverTarget) {
    enqueueLoadCover(d, cur);
    d->coverDebounceFramesLeft = -1;
  }

  // Only show cover texture when it matches current selection; when a new cover is requested, show placeholder until it loads
  if (workerPollCoverLoadDone() && d->coverTexture && d->coverTarget == cur && d->coverTexture->Width > 0) {
    gsKit_TexManager_bind(gsGlobal, d->coverTexture);
    // Some cover art might have inverted alpha values; work around this issue by disabling alpha test for cover art
    uiDrawTextureInVirtualRect(gsGlobal, coverLeft, coverTop, coverW, coverH, d->coverTexture, zOrder + 1, FontMainColor, 1);
  } else {
    uiDrawTextureInVirtualRect(gsGlobal, coverLeft, coverTop, coverW, coverH, NULL, zOrder + 1, BGColor, 0);
    fontRenderInRect(FONT_DEFAULT, coverLeft, coverTop, coverLeft + coverW, coverTop + coverH, FONT_ALIGN_CENTER, zOrder + 1, "No cover",
                     FontMainColor);
  }
  if (cur) {
    if (cur->id)
      fontRenderInRect(FONT_DEFAULT, coverLeft, (coverTop + coverH), (coverLeft + coverW), 0, FONT_ALIGN_CENTER, zOrder, cur->id, HeaderTextColor);
    if (cur->device && cur->device->type) {
      if (getBackendDeviceCountByType(cur->device->type) > 1)
        snprintf(d->lineBuf, sizeof(d->lineBuf), "%s %d", getDeviceString(cur->device->type), cur->device->index);
      else
        strncpy(d->lineBuf, getDeviceString(cur->device->type), sizeof(d->lineBuf));
      fontRenderInRect(FONT_DEFAULT, coverLeft, (coverTop + coverH + fontGetLineHeight(FONT_DEFAULT) + 2), (coverLeft + coverW), 0, FONT_ALIGN_CENTER,
                       zOrder, d->lineBuf, HeaderTextColor);
      fontRenderInRect(FONT_DEFAULT, coverLeft, (coverTop + coverH + fontGetLineHeight(FONT_DEFAULT) * 2 + 2), (coverLeft + coverW), 0,
                       FONT_ALIGN_CENTER, zOrder, getISOFormat(cur->flags), HeaderTextColor);
    }
  }

  if (d->selectedIndex < d->scrollOffset)
    d->scrollOffset = d->selectedIndex;
  if (d->selectedIndex >= d->scrollOffset + s_maxVisible)
    d->scrollOffset = d->selectedIndex - s_maxVisible + 1;
  for (int i = 0; i < s_maxVisible; i++) {
    int idx = d->scrollOffset + i;
    if (idx >= count)
      break;
    Target *t = titleListViewGetAt(&d->listView, idx);
    int y = LIST_TOP + i * s_lineHeight;
    uint64_t col = (idx == d->selectedIndex) ? ColorSelected : FontMainColor;
    const char *name = (t && t->name) ? t->name : "(no name)";
    fontRenderString(FONT_DEFAULT, LIST_LEFT, y, 0, LIST_RIGHT - LIST_LEFT, 0, zOrder, name, col);
  }

  // Handle autolaunch
  if (d->autoLaunchCountdown > 0) {
    int sec = (d->autoLaunchCountdown + 59) / scaleGetRefreshRateHz();
    snprintf(d->lineBuf, sizeof(d->lineBuf), "Launching in %d s... (any button to cancel)", sec);
    showOSD(d->lineBuf, 1);
  }
}

static ViewResult mainSceneOnInput(View *v, int padInput) {
  MainSceneData *d = (MainSceneData *)v->userdata;

  // Handle autolaunch
  if (!s_autolaunchCanceled && !d->listRebuildPending) {
    if (!d->autoLaunchCountdown) {
      uiLaunchTarget();
      return ViewResult_Continue;
    }
    d->autoLaunchCountdown--;
    if (padInput) {
      s_autolaunchCanceled = 1;
      d->autoLaunchCountdown = 0;
    }
  }

  if (d->exitRequested)
    return ViewResult_Pop;

  if (padInput & PAD_START) {
    View *cv = menuGetView();
    cv->userdata = d;
    viewStackPush(viewStack, cv);
    return ViewResult_Continue;
  }

  if (d->listRebuildPending)
    return ViewResult_Continue;

  int count = titleListViewCount(&d->listView);

  if (padInput & PAD_SELECT) {
    if (count > 0) {
      View *cv = viewOptionsPopupGetView();
      cv->userdata = d;
      viewStackPush(viewStack, cv);
    }
    return ViewResult_Continue;
  }
  if (padInput & PAD_TRIANGLE) {
    if (count > 0 && d->selectedIndex >= 0 && d->selectedIndex < count) {
      View *cv = titleOptionsSelectorGetView();
      TitleOptionsSelectorData *opt = (TitleOptionsSelectorData *)cv->userdata;
      opt->target = titleListViewGetAt(&d->listView, d->selectedIndex);
      viewStackPush(viewStack, cv);
    }
    return ViewResult_Continue;
  }
  if (count > 0) {
    // Parentheses required: & binds tighter than |; without them, (padInput & PAD_UP) | PAD_L1 is often non-zero every frame.
    if (padInput & (PAD_UP | PAD_L1)) {
      if (padInput & PAD_UP)
        d->selectedIndex--;
      else
        d->selectedIndex -= s_maxVisible;

      if (d->selectedIndex < 0)
        d->selectedIndex = count - 1;
      d->coverDebounceFramesLeft = COVER_DEBOUNCE_FRAMES;
      return ViewResult_Continue;
    }
    if (padInput & (PAD_DOWN | PAD_R1)) {
      if (padInput & PAD_DOWN)
        d->selectedIndex++;
      else
        d->selectedIndex += s_maxVisible;

      if (d->selectedIndex >= count)
        d->selectedIndex = 0;
      d->coverDebounceFramesLeft = COVER_DEBOUNCE_FRAMES;
      return ViewResult_Continue;
    }
    if ((padInput & PAD_CROSS) && d->selectedIndex >= 0 && d->selectedIndex < count) {
      uiLaunchTarget();
      return ViewResult_Continue;
    }
  }
  return ViewResult_Continue;
}

struct View *mainSceneGetView(void) {
  s_view = (View){
      .type = ViewType_Scene,
      .userdata = &s_data,
      .onEnter = mainSceneOnEnter,
      .onLeave = mainSceneOnLeave,
      .draw = mainSceneDraw,
      .onInput = mainSceneOnInput,
  };
  return &s_view;
}

MainSceneData *mainSceneGetData(void) { return &s_data; }

void uiLaunchTarget() {
  Target *t = (s_data.selectedIndex >= 0 && s_data.selectedIndex < titleListViewCount(&s_data.listView))
                  ? titleListViewGetAt(&s_data.listView, s_data.selectedIndex)
                  : NULL;
  if (!t) {
    showOSD("No title selected", OVERLAY_DURATION_FRAMES);
    return;
  }
  ArgumentList *globalArgs = calloc(1, sizeof(ArgumentList));
  ArgumentList *titleArgs = calloc(1, sizeof(ArgumentList));
  if (globalArgs && titleArgs) {
    loadGlobalNeutrinoArguments(globalArgs, t->device);
    loadTitleNeutrinoArguments(titleArgs, t);
    ArgumentList *merged = mergeNeutrinoArguments(globalArgs, titleArgs);
    if (merged) {
      updateLastLaunchedTitle(t);
      int res = launchTarget(t, merged);
      freeArgumentList(merged);
      if (res == 0) {
        freeArgumentList(globalArgs);
        freeArgumentList(titleArgs);
        return;
      }
      snprintf(s_data.lineBuf, sizeof(s_data.lineBuf), "Launch failed: %d", res);
      showOSD(s_data.lineBuf, OVERLAY_DURATION_FRAMES);
    }
    freeArgumentList(globalArgs);
    freeArgumentList(titleArgs);
  } else {
    if (globalArgs)
      freeArgumentList(globalArgs);
    if (titleArgs)
      freeArgumentList(titleArgs);
  }
}

void uiTestLaunchWithTitleArgs(Target *t, ArgumentList *effectiveMerged) {
  if (!t || !effectiveMerged) {
    showOSD("No title", OVERLAY_DURATION_FRAMES);
    return;
  }
  ArgumentList *copy = duplicateArgumentList(effectiveMerged);
  if (!copy)
    return;
  int res = launchTarget(t, copy);
  freeArgumentList(copy);
  if (res != 0) {
    snprintf(s_data.lineBuf, sizeof(s_data.lineBuf), "Test launch failed: %d", res);
    showOSD(s_data.lineBuf, OVERLAY_DURATION_FRAMES);
  }
}

void uiTestLaunchWithInMemoryGlobalArgs(ArgumentList *globalArgsInMemory) {
  if (!globalArgsInMemory) {
    showOSD("No configuration", OVERLAY_DURATION_FRAMES);
    return;
  }
  Target *t = (s_data.selectedIndex >= 0 && s_data.selectedIndex < titleListViewCount(&s_data.listView))
                  ? titleListViewGetAt(&s_data.listView, s_data.selectedIndex)
                  : NULL;
  if (!t) {
    showOSD("No title selected", OVERLAY_DURATION_FRAMES);
    return;
  }
  ArgumentList *titleArgs = calloc(1, sizeof(ArgumentList));
  if (!titleArgs)
    return;
  loadTitleNeutrinoArguments(titleArgs, t);
  ArgumentList *merged = mergeNeutrinoArguments(globalArgsInMemory, titleArgs);
  if (merged) {
    int res = launchTarget(t, merged);
    freeArgumentList(merged);
    if (res == 0) {
      freeArgumentList(titleArgs);
      return;
    }
    snprintf(s_data.lineBuf, sizeof(s_data.lineBuf), "Test launch failed: %d", res);
    showOSD(s_data.lineBuf, OVERLAY_DURATION_FRAMES);
  }
  freeArgumentList(titleArgs);
}

static char *getISOFormat(uint32_t titleFlags) {
  if (titleFlags & TitleFlag_ZSO)
    return "ZSO";
  else if (titleFlags & TitleFlag_CSO)
    return "CSO";
  else if (titleFlags & TitleFlag_CHD)
    return "CHD";
  return "ISO";
}
