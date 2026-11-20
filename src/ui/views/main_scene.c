#include "ui/views/main_scene.h"
#include "backends/backends.h"
#include "backends/cache.h"
#include "backends/target.h"
#include "config/arguments.h"
#include "config/config.h"
#include "config/neutrino_args.h"
#include "devices/pad.h"
#include "devices/utils.h"
#include "dprintf.h"
#include "neutrino/neutrino.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/png.h"
#include "ui/scale.h"
#include "ui/folder_view.h"
#include "ui/title_list.h"
#include "ui/ui_context.h"
#include "ui/strings.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/menu_scene.h"
#include "ui/views/title_options_scene.h"
#include "ui/views/view_options_popup.h"
#include "ui/worker/worker.h"
#include <gsKit.h>
#include <gsToolkit.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OVERLAY_DURATION_FRAMES 120

#define HEADER_Y (SCENE_KEEPOUT)
#define LIST_LEFT (SCENE_KEEPOUT * 2)
#define LIST_TOP (SCENE_KEEPOUT + SCENE_ROW_SIZE)
#define COVER_W 140
#define COVER_H 200
#define COVER_LEFT (SCENE_VW - LIST_LEFT - COVER_W)
#define LIST_RIGHT (COVER_LEFT - SCENE_KEEPOUT)
#define COVER_TOP (LIST_TOP + (SCENE_CONTENT_BOTTOM - LIST_TOP) / 2 - COVER_H / 2)
#define COVER_BORDER_MARGIN 2
#define ART_PATH "/ART"
#define COVER_DEBOUNCE_FRAMES 18
#define MARQUEE_HOLD_FRAMES 45
#define MARQUEE_SPEED 1

static View s_view;
static MainSceneData s_data;
static int s_autolaunchCanceled;
static int s_maxVisible = 0;
static int s_lineHeight = 0;

void uiLaunchTarget(void);
static char *getISOFormat(uint32_t titleFlags);
static void syncSelectedTitle(MainSceneData *d);

void mainSceneBeginBackendResync(MainSceneData *d) {
  UiContext *ctx = uiGetContext();
  if (!d)
    return;
  d->filterDeviceMask = 0;
  titleListViewClear(&d->listView);
  folderViewFree(&d->folderView);
  folderViewInit(&d->folderView);
  d->selectedIndex = -1;
  d->scrollOffset = 0;
  d->coverTarget = NULL;
  d->coverDebounceFramesLeft = 0;
  d->rebuildSelectionTarget = NULL;
  d->listDirty = 0;
  ctx->selectedTitle = NULL;
  ctx->listUpdating = 1;
  ctx->marqueeOffset = 0;
  ctx->marqueeHoldFrames = 0;
  s_autolaunchCanceled = 1;
  d->autoLaunchCountdown = 0;
}

void mainSceneInit(void) {
  titleListViewInit(&s_data.listView);
  folderViewInit(&s_data.folderView);
  s_data.folderMode = getFolderView();
  s_data.filterDeviceMask = 0;
  s_data.favoritesOnly = 0;
  s_data.favoritesOnTop = 0;
  s_data.sortAscending = 1;
  s_data.coverTexture = NULL;
  s_data.coverTarget = NULL;
  s_data.rebuildSelectionTarget = NULL;
  s_data.coverDebounceFramesLeft = 0;
  s_data.autoLaunchCountdown = getAutolaunchTimeout() * scaleGetRefreshRateHz();
  if (!s_data.autoLaunchCountdown)
    s_autolaunchCanceled = 1;
  s_data.selectedIndex = -1;
  s_data.listDirty = 0;

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
  folderViewFree(&s_data.folderView);
}

static void requestLoadCover(MainSceneData *d, Target *t) {
  UiContext *ctx = uiGetContext();
  if (!t || !t->device || !t->id)
    return;
  if (ctx->listUpdating || workerGetState() != WorkerState_Idle)
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
    d->coverTexture->Width = 0;
    d->coverTexture->Height = 0;
  }

  struct BackendDevice *dev = t->device;
  if (dev->metadev)
    dev = dev->metadev;

  d->coverTarget = t;
  snprintf(d->lineBuf, sizeof(d->lineBuf), "%s%s/%s_COV.png", dev->mountpoint, ART_PATH, t->id);
  if (workerRequestLoadCover(d->lineBuf, d->coverTexture) != 0)
    d->coverTarget = NULL;
}

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

void mainSceneRebuildFromBackends(MainSceneData *d) {
  mainSceneNormalizeDeviceFilter(d);
  titleListViewClear(&d->listView);
  if (titleListViewBuildFromBackendsFiltered(&d->listView, d->filterDeviceMask, d->favoritesOnly) != 0)
    return;
  titleListViewSort(&d->listView, d->sortAscending, d->favoritesOnTop);
  if (d->folderMode)
    folderViewResetRoot(&d->folderView);
  d->selectedIndex = 0;
  d->scrollOffset = 0;
  d->listDirty = 0;
  syncSelectedTitle(d);
}

static int mainSceneRowCount(const MainSceneData *d) {
  if (d->folderMode)
    return folderViewCount(&d->folderView);
  return titleListViewCount(&d->listView);
}

static void syncSelectedTitle(MainSceneData *d) {
  UiContext *ctx = uiGetContext();
  ctx->selectedTitle = NULL;
  if (d->folderMode) {
    const FolderViewRow *row = folderViewGetRow(&d->folderView, d->selectedIndex);
    if (row && row->kind == FolderRow_Title)
      ctx->selectedTitle = row->target;
  } else {
    int count = titleListViewCount(&d->listView);
    if (count > 0 && d->selectedIndex >= 0 && d->selectedIndex < count)
      ctx->selectedTitle = titleListViewGetAt(&d->listView, d->selectedIndex);
  }
  ctx->marqueeOffset = 0;
  ctx->marqueeHoldFrames = MARQUEE_HOLD_FRAMES;
}

static void mainSceneTryFirstSetup(MainSceneData *d) {
  UiContext *ctx = uiGetContext();
  if (d->selectedIndex != -1 || ctx->listUpdating)
    return;

  mainSceneRebuildFromBackends(d);
  if (d->folderMode) {
    // Stay at root, skip autolaunch.
    s_autolaunchCanceled = 1;
    d->autoLaunchCountdown = 0;
    syncSelectedTitle(d);
  } else {
    Target *last = getLastLaunchedTarget();
    if (last) {
      d->selectedIndex = titleListViewGetIdx(&d->listView, last);
      if (d->selectedIndex < 0) {
        s_autolaunchCanceled = 1;
        d->autoLaunchCountdown = 0;
        d->selectedIndex = 0;
      }
      syncSelectedTitle(d);
      if (ctx->selectedTitle)
        requestLoadCover(d, ctx->selectedTitle);
    } else {
      syncSelectedTitle(d);
    }
  }
  // Empty list after boot: open main menu once so the user can enable devices.
  if (!ctx->openedMenuOnEmpty && titleListViewCount(&d->listView) == 0) {
    ctx->openedMenuOnEmpty = 1;
    View *menu = menuGetView();
    menu->userdata = d;
    viewStackPush(viewStack, menu);
  }
}

static void mainSceneOnEnter(View *v) { mainSceneTryFirstSetup((MainSceneData *)v->userdata); }

static void mainSceneOnLeave(View *v) { }

static void updateMarquee(UiContext *ctx, const char *name, int listWidthNative) {
  int textW = fontCalcDimensionsFirstLine(FONT_DEFAULT, name);
  if (textW <= listWidthNative) {
    ctx->marqueeOffset = 0;
    ctx->marqueeHoldFrames = MARQUEE_HOLD_FRAMES;
    return;
  }
  int maxOff = textW - listWidthNative;
  if (ctx->marqueeHoldFrames > 0) {
    ctx->marqueeHoldFrames--;
    if (ctx->marqueeHoldFrames == 0 && ctx->marqueeOffset >= maxOff) {
      ctx->marqueeOffset = 0;
      ctx->marqueeHoldFrames = MARQUEE_HOLD_FRAMES;
    }
    return;
  }
  ctx->marqueeOffset += MARQUEE_SPEED;
  if (ctx->marqueeOffset >= maxOff) {
    ctx->marqueeOffset = maxOff;
    ctx->marqueeHoldFrames = MARQUEE_HOLD_FRAMES;
  }
}

static void mainSceneDraw(View *v, int zOrder) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  UiContext *ctx = uiGetContext();

  // Splash may have been skipped before InitList finished
  mainSceneTryFirstSetup(d);

  if (d->listDirty && !ctx->listUpdating) {
    Target *prev = ctx->selectedTitle;
    titleListViewClear(&d->listView);
    if (titleListViewBuildFromBackendsFiltered(&d->listView, d->filterDeviceMask, d->favoritesOnly) == 0) {
      titleListViewSort(&d->listView, d->sortAscending, d->favoritesOnTop);
      if (d->folderMode) {
        folderViewRebuild(&d->folderView);
        d->selectedIndex = 0;
      } else {
        d->selectedIndex = titleListViewGetIdx(&d->listView, prev);
        if (d->selectedIndex < 0)
          d->selectedIndex = 0;
      }
    }
    d->scrollOffset = 0;
    d->listDirty = 0;
    syncSelectedTitle(d);
  }

  if (d->folderMode) {
    if (d->folderView.atDeviceList)
      snprintf(d->lineBuf, sizeof(d->lineBuf), "%s", uiStr(UI_STR_DEVICES));
    else {
      const char *mount = (d->folderView.device && d->folderView.device->mountpoint) ? d->folderView.device->mountpoint : "";
      if (d->folderView.pathPrefix[0])
        snprintf(d->lineBuf, sizeof(d->lineBuf), "%s/%s/", mount, d->folderView.pathPrefix);
      else if (mount[0])
        snprintf(d->lineBuf, sizeof(d->lineBuf), "%s/", mount);
      else
        snprintf(d->lineBuf, sizeof(d->lineBuf), "/");
    }
    fontRenderInRect(FONT_DEFAULT, LIST_LEFT, HEADER_Y, SCENE_VW - LIST_LEFT, LIST_TOP, FONT_ALIGN_TOP | FONT_ALIGN_LEFT, zOrder, d->lineBuf,
                     HeaderTextColor);
    uiDrawScenePrompt(gsGlobal, 0, zOrder, HeaderTextColor, ICON_SQUARE, uiStr(UI_STR_TITLE_OPTIONS));
    uiDrawScenePrompt(gsGlobal, 1, zOrder, HeaderTextColor, ICON_TRIANGLE, uiStr(UI_STR_UP));
    uiDrawScenePrompt(gsGlobal, 2, zOrder, HeaderTextColor, ICON_START, uiStr(UI_STR_MENU));
    uiDrawScenePrompt2(gsGlobal, 3, zOrder, HeaderTextColor, ICON_CROSS, ICON_CIRCLE, uiStr(UI_STR_SELECT));
  } else {
    fontRenderInRect(FONT_DEFAULT, 0, HEADER_Y, SCENE_VW, LIST_TOP, FONT_ALIGN_TOP | FONT_ALIGN_HCENTER, zOrder, uiStr(UI_STR_TITLE_LIST),
                     HeaderTextColor);
    uiDrawScenePrompt(gsGlobal, 0, zOrder, HeaderTextColor, ICON_TRIANGLE, uiStr(UI_STR_TITLE_OPTIONS));
    uiDrawScenePrompt(gsGlobal, 1, zOrder, HeaderTextColor, ICON_SELECT, uiStr(UI_STR_VIEW));
    uiDrawScenePrompt(gsGlobal, 2, zOrder, HeaderTextColor, ICON_START, uiStr(UI_STR_MENU));
    uiDrawScenePrompt2(gsGlobal, 3, zOrder, HeaderTextColor, ICON_CROSS, ICON_CIRCLE, uiStr(UI_STR_LAUNCH));
  }

  if (ctx->listUpdating) {
    fontRenderInRect(FONT_DEFAULT, 0, LIST_TOP, SCENE_VW, SCENE_CONTENT_BOTTOM, FONT_ALIGN_CENTER, zOrder, uiStr(UI_STR_UPDATING_DEVICES),
                     FontMainColor);
    return;
  }

  int count = mainSceneRowCount(d);
  if (count > 0 && d->selectedIndex >= 0 && d->selectedIndex < count) {
    char posBuf[16];
    snprintf(posBuf, sizeof(posBuf), "%d/%d", d->selectedIndex + 1, count);
    fontRenderInRect(FONT_DEFAULT, 0, HEADER_Y, SCENE_VW - LIST_LEFT, LIST_TOP, FONT_ALIGN_TOP | FONT_ALIGN_RIGHT, zOrder, posBuf, HeaderTextColor);
  }

  Target *cur = ctx->selectedTitle;
  if (count == 0) {
    fontRenderInRect(FONT_DEFAULT, 0, 0, SCENE_VW, SCENE_CONTENT_BOTTOM, FONT_ALIGN_CENTER, zOrder, uiStr(UI_STR_NO_TITLES), FontMainColor);
    return;
  }

  if (cur) {
    int coverW = COVER_W, coverH = COVER_H, coverLeft = COVER_LEFT, coverTop = COVER_TOP;
    if (scaleGetDisplayAspect() == ASPECT_16_9) {
      coverW = (int)(COVER_W * 1.2f);
      coverH = (int)(COVER_H * 1.2f);
      coverLeft = COVER_LEFT + COVER_W / 2 - coverW / 2;
      coverTop = COVER_TOP + COVER_H / 2 - coverH / 2;
    }
    uiDrawTextureInVirtualRect(gsGlobal, coverLeft - COVER_BORDER_MARGIN, coverTop - COVER_BORDER_MARGIN, coverW + COVER_BORDER_MARGIN * 2,
                               coverH + COVER_BORDER_MARGIN * 2, NULL, zOrder, FontMainColor, 0);

    if (cur != d->coverTarget && d->coverDebounceFramesLeft <= 0)
      d->coverDebounceFramesLeft = COVER_DEBOUNCE_FRAMES;
    if (d->coverDebounceFramesLeft > 0)
      d->coverDebounceFramesLeft--;
    if (workerGetState() == WorkerState_Idle && d->coverDebounceFramesLeft == 0 && cur != d->coverTarget) {
      requestLoadCover(d, cur);
      d->coverDebounceFramesLeft = -1;
    }

    int coverLoading = (workerGetState() == WorkerState_Running && workerGetJobType() == WorkerJob_LoadCover);
    if (d->coverTexture && d->coverTarget == cur && d->coverTexture->Width > 0 && !coverLoading) {
      gsKit_TexManager_bind(gsGlobal, d->coverTexture);
      uiDrawTextureInVirtualRect(gsGlobal, coverLeft, coverTop, coverW, coverH, d->coverTexture, zOrder + 1, FontMainColor, 1);
    } else {
      // Blank slot while debouncing/loading
      uiDrawTextureInVirtualRect(gsGlobal, coverLeft, coverTop, coverW, coverH, NULL, zOrder + 1, BGColor, 0);
      int coverFailed = (d->coverTarget == cur && d->coverTexture && d->coverTexture->Width == 0 && !coverLoading);
      if (coverFailed)
        fontRenderInRect(FONT_DEFAULT, coverLeft, coverTop, coverLeft + coverW, coverTop + coverH, FONT_ALIGN_CENTER, zOrder + 1, uiStr(UI_STR_NO_COVER),
                         FontMainColor);
    }
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

  int listWidthNative = scaleScaleX(LIST_RIGHT - LIST_LEFT);
  for (int i = 0; i < s_maxVisible; i++) {
    int idx = d->scrollOffset + i;
    if (idx >= count)
      break;
    int y = LIST_TOP + i * s_lineHeight;
    uint64_t col = (idx == d->selectedIndex) ? ColorSelected : FontMainColor;
    const char *name = uiStr(UI_STR_NO_NAME);
    char folderLabel[FOLDER_VIEW_NAME_MAX + 2];
    if (d->folderMode) {
      const FolderViewRow *row = folderViewGetRow(&d->folderView, idx);
      if (row) {
        if (row->kind == FolderRow_Title)
          name = (row->target && row->target->name) ? row->target->name : uiStr(UI_STR_NO_NAME);
        else if (row->kind == FolderRow_Folder) {
          snprintf(folderLabel, sizeof(folderLabel), "%s/", row->name);
          name = folderLabel;
        } else
          name = row->name;
      }
    } else {
      Target *t = titleListViewGetAt(&d->listView, idx);
      name = (t && t->name) ? t->name : uiStr(UI_STR_NO_NAME);
    }
    if (idx == d->selectedIndex) {
      updateMarquee(ctx, name, listWidthNative);
      fontRenderStringScrolled(FONT_DEFAULT, LIST_LEFT, y, scaleUnscaleX(ctx->marqueeOffset), LIST_RIGHT - LIST_LEFT, zOrder, name, col);
    } else {
      fontRenderString(FONT_DEFAULT, LIST_LEFT, y, 0, LIST_RIGHT - LIST_LEFT, 0, zOrder, name, col);
    }
  }

  if (d->autoLaunchCountdown > 0) {
    int sec = (d->autoLaunchCountdown + 59) / scaleGetRefreshRateHz();
    snprintf(d->lineBuf, sizeof(d->lineBuf), uiStr(UI_STR_LAUNCHING_IN_FMT), sec);
    showOSD(d->lineBuf, 1);
  }
}

static ViewResult mainSceneOnInput(View *v, int padInput) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  UiContext *ctx = uiGetContext();

  if (!s_autolaunchCanceled && !ctx->listUpdating) {
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

  if (ctx->exitRequested)
    return ViewResult_Pop;

  if (padInput & PAD_START) {
    View *cv = menuGetView();
    cv->userdata = d;
    viewStackPush(viewStack, cv);
    return ViewResult_Continue;
  }

  if (ctx->listUpdating)
    return ViewResult_Continue;

  int count = mainSceneRowCount(d);

  if ((padInput & PAD_SELECT) && !d->folderMode) {
    View *cv = viewOptionsPopupGetView();
    cv->userdata = d;
    viewStackPush(viewStack, cv);
    return ViewResult_Continue;
  }
  if (d->folderMode) {
    if (padInput & PAD_TRIANGLE) {
      if (folderViewGoUp(&d->folderView)) {
        d->selectedIndex = 0;
        d->scrollOffset = 0;
        d->coverDebounceFramesLeft = COVER_DEBOUNCE_FRAMES;
        syncSelectedTitle(d);
      }
      return ViewResult_Continue;
    }
    if ((padInput & PAD_SQUARE) && ctx->selectedTitle) {
      View *cv = titleOptionsSelectorGetView();
      TitleOptionsSelectorData *opt = (TitleOptionsSelectorData *)cv->userdata;
      opt->target = ctx->selectedTitle;
      viewStackPush(viewStack, cv);
      return ViewResult_Continue;
    }
  } else if (padInput & PAD_TRIANGLE) {
    if (ctx->selectedTitle) {
      View *cv = titleOptionsSelectorGetView();
      TitleOptionsSelectorData *opt = (TitleOptionsSelectorData *)cv->userdata;
      opt->target = ctx->selectedTitle;
      viewStackPush(viewStack, cv);
    }
    return ViewResult_Continue;
  }
  if (count > 0) {
    if (padInput & (PAD_UP | PAD_L1)) {
      if (padInput & PAD_UP)
        d->selectedIndex--;
      else
        d->selectedIndex -= s_maxVisible;

      if (d->selectedIndex < 0)
        d->selectedIndex = count - 1;
      d->coverDebounceFramesLeft = COVER_DEBOUNCE_FRAMES;
      syncSelectedTitle(d);
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
      syncSelectedTitle(d);
      return ViewResult_Continue;
    }
    if (padInput & PAD_SELECT_ACTION) {
      if (d->folderMode) {
        const FolderViewRow *row = folderViewGetRow(&d->folderView, d->selectedIndex);
        if (row && (row->kind == FolderRow_Device || row->kind == FolderRow_Folder)) {
          if (folderViewEnter(&d->folderView, d->selectedIndex)) {
            d->selectedIndex = 0;
            d->scrollOffset = 0;
            d->coverDebounceFramesLeft = COVER_DEBOUNCE_FRAMES;
            syncSelectedTitle(d);
          }
          return ViewResult_Continue;
        }
      }
      if (ctx->selectedTitle) {
        uiLaunchTarget();
        return ViewResult_Continue;
      }
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

void uiLaunchTarget(void) {
  UiContext *ctx = uiGetContext();
  Target *t = ctx->selectedTitle;
  if (!t) {
    showOSD(uiStr(UI_STR_NO_TITLE_SELECTED), OVERLAY_DURATION_FRAMES);
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
      snprintf(s_data.lineBuf, sizeof(s_data.lineBuf), uiStr(UI_STR_LAUNCH_FAILED_FMT), res);
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
    showOSD(uiStr(UI_STR_NO_TITLE), OVERLAY_DURATION_FRAMES);
    return;
  }
  ArgumentList *copy = duplicateArgumentList(effectiveMerged);
  if (!copy)
    return;
  int res = launchTarget(t, copy);
  freeArgumentList(copy);
  if (res != 0) {
    snprintf(s_data.lineBuf, sizeof(s_data.lineBuf), uiStr(UI_STR_TEST_LAUNCH_FAILED_FMT), res);
    showOSD(s_data.lineBuf, OVERLAY_DURATION_FRAMES);
  }
}

void uiTestLaunchWithInMemoryGlobalArgs(ArgumentList *globalArgsInMemory) {
  UiContext *ctx = uiGetContext();
  if (!globalArgsInMemory) {
    showOSD(uiStr(UI_STR_NO_CONFIGURATION), OVERLAY_DURATION_FRAMES);
    return;
  }
  Target *t = ctx->selectedTitle;
  if (!t) {
    showOSD(uiStr(UI_STR_NO_TITLE_SELECTED), OVERLAY_DURATION_FRAMES);
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
    snprintf(s_data.lineBuf, sizeof(s_data.lineBuf), uiStr(UI_STR_TEST_LAUNCH_FAILED_FMT), res);
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
