#include "ui/ui_context.h"
#include "config/config.h"
#include "ui/title_list.h"
#include "ui/strings.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/main_scene.h"
#include "ui/worker/worker.h"
#include <string.h>

#define OVERLAY_DURATION_FRAMES 120

extern UiContext uiContext;

void uiContextInit(UiContext *ctx, MainSceneData *main) {
  if (!ctx)
    return;
  memset(ctx, 0, sizeof(*ctx));
  ctx->main = main;
  ctx->enabledDevices = getEnabledDevices();
}

UiContext *uiGetContext(void) { return &uiContext; }

static void applyListChanged(UiContext *ctx, MainSceneData *d) {
  Target *prev = d->rebuildSelectionTarget;
  if (!prev)
    prev = ctx->selectedTitle;
  mainSceneRebuildFromBackends(d);
  if (prev) {
    int idx = titleListViewGetIdx(&d->listView, prev);
    d->selectedIndex = (idx >= 0) ? idx : 0;
  } else {
    d->selectedIndex = 0;
  }
  d->scrollOffset = 0;
  d->rebuildSelectionTarget = NULL;
  ctx->listUpdating = 0;

  int count = titleListViewCount(&d->listView);
  if (count > 0 && d->selectedIndex >= 0 && d->selectedIndex < count)
    ctx->selectedTitle = titleListViewGetAt(&d->listView, d->selectedIndex);
  else
    ctx->selectedTitle = NULL;

  ctx->marqueeOffset = 0;
  ctx->marqueeHoldFrames = 0;
}

void uiPollWorker(void) {
  UiContext *ctx = &uiContext;
  MainSceneData *d = ctx->main;
  if (workerGetState() != WorkerState_Done)
    return;

  WorkerResult result;
  if (!workerTakeResult(&result))
    return;

  if (result.message[0])
    showOSD(result.message, OVERLAY_DURATION_FRAMES);

  if (result.listChanged && d) {
    // Boot InitList: leave selectedIndex at -1 so mainSceneOnEnter does first-time setup.
    if (result.type == WorkerJob_InitList && d->selectedIndex < 0)
      ctx->listUpdating = 0;
    else
      applyListChanged(ctx, d);
  } else if (result.type == WorkerJob_InitList) {
    ctx->listUpdating = 0;
  }

  if (result.type == WorkerJob_Rescan && ctx->pendingSaveAfterRescan) {
    ctx->pendingSaveAfterRescan = 0;
    if (workerRequest(WorkerJob_SaveConfig) != 0)
      showOSD(uiStr(UI_STR_FAILED_SAVE_CONFIG), OVERLAY_DURATION_FRAMES);
  }
}
