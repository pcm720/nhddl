#include "backends/backends.h"
#include "config/common.h"
#include "config/config.h"
#include "config/nhddl.h"
#include "devices/pad.h"
#include "dprintf.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/scale.h"
#include "ui/strings.h"
#include "ui/ui_context.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/exit_modal.h"
#include "ui/views/main_scene.h"
#include "ui/views/menu_scene.h"
#include "ui/views/osd_overlay.h"
#include "ui/views/splash_scene.h"
#include "ui/worker/worker.h"
#include <dmaKit.h>
#include <errno.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <kernel.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern unsigned char font_ttf[] __attribute__((aligned(16)));
extern uint32_t size_font_ttf;

#ifndef GIT_VERSION
#define GIT_VERSION "unknown"
#endif

#define FONT_PATH_THEME "theme/font.ttf"
#define OVERLAY_DURATION_FRAMES 120

GSGLOBAL *gsGlobal;
struct ViewStack *viewStack;
UiContext uiContext = {0};

static int s_gsHires = 0;

void showOSD(const char *msg, int frames) {
  strncpy(uiContext.osdMessage, msg, sizeof(uiContext.osdMessage) - 1);
  uiContext.osdMessage[sizeof(uiContext.osdMessage) - 1] = '\0';
  uiContext.osdFramesLeft = frames > 0 ? frames : OVERLAY_DURATION_FRAMES;
  viewStackSetOverlay(viewStack, osdOverlayGetView(), uiContext.osdFramesLeft);
}

const char *getOSDMessage(void) { return uiContext.osdMessage; }

static void initViews(void) {
  if (initIcons(gsGlobal) != 0)
    DPRINTF("ui: initIcons failed, footer will show text only\n");
}

static void initVMode(GSGLOBAL *gs) {
  scaleSetDisplayAspect(getWidescreen() ? ASPECT_16_9 : ASPECT_4_3);
  scaleApplyVMode(gs, getVMode());
}

static int buildFontPath(char *buf, size_t size) {
  const char *root = getNHDDLRoot();
  if (!root || !root[0] || size == 0)
    return -1;
  joinDevicePath(buf, size, root, FONT_PATH_THEME);
  return buf[0] ? 0 : -1;
}

static int loadUiFonts(void) {
  char path[256];
  int fontLoaded = 0;
  if (buildFontPath(path, sizeof(path)) == 0 && fontLoadFile(path, 0) == 0)
    fontLoaded = 1;
  if (!fontLoaded && fontLoadMemory(font_ttf, (size_t)size_font_ttf, 0) != 0)
    return -1;
  fontUpdateAspectRatio();
  return 0;
}

static void applyGsFramebufferParams(GSGLOBAL *gs) {
  if (gs->Width * gs->Height > 704 * 576) {
    gs->PSM = GS_PSM_CT16S;
    gs->DoubleBuffering = GS_SETTING_OFF;
  } else {
    gs->PSM = GS_PSM_CT24;
    gs->DoubleBuffering = GS_SETTING_ON;
  }
  gs->PSMZ = GS_PSMZ_16S;
  gs->ZBuffering = GS_SETTING_ON;
  gs->PrimAlphaEnable = GS_SETTING_ON;
  gs->Test->ATST = 7;
  gs->Test->AREF = 0x00;
  gs->Test->AFAIL = 0;
}

static void initGsScreen(GSGLOBAL *gs, int hires) {
  gsKit_vram_clear(gs);
  if (hires)
    gsKit_hires_init_screen(gs, 2);
  else {
    gsKit_init_screen(gs);
    scaleFix576pMode(gs);
    gsKit_mode_switch(gs, GS_ONESHOT);
  }
  gsKit_display_buffer(gs);
  gsKit_TexManager_init(gs);
  gsKit_set_primalpha(gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gs, GS_ATEST_ON);
  gsKit_set_test(gs, GS_ZTEST_ON);
  gsKit_clear(gs, BGColor);
}

static void invalidateCoverTexture(void) {
  MainSceneData *d = mainSceneGetData();
  if (!d || !d->coverTexture || !gsGlobal)
    return;
  gsKit_TexManager_invalidate(gsGlobal, d->coverTexture);
}

static void releaseUiGsClients(void) {
  fontEnd();
  freeIcons();
  invalidateCoverTexture();
}

static void deinitGsGlobal(void) {
  if (!gsGlobal)
    return;
  gsKit_vram_clear(gsGlobal);
  if (s_gsHires)
    gsKit_hires_deinit_global(gsGlobal);
  else
    gsKit_deinit_global(gsGlobal);
  gsGlobal = NULL;
  s_gsHires = 0;
}

static int setupDisplayFromConfig(void) {
  if (!gsGlobal)
    return -1;
  initVMode(gsGlobal);
  applyGsFramebufferParams(gsGlobal);
  initGsScreen(gsGlobal, s_gsHires);
  scaleUpdate(gsGlobal);
  fontInit(gsGlobal);
  if (loadUiFonts() != 0)
    return -1;
  initViews();
  return 0;
}

void uiApplyDisplaySettings(void) {
  if (!gsGlobal)
    return;

  int wantHires = (getVMode() == VMode_720p);
  if (wantHires)
    setWidescreen(1);

  if (wantHires != s_gsHires) {
    releaseUiGsClients();
    deinitGsGlobal();
    gsGlobal = wantHires ? gsKit_hires_init_global() : gsKit_init_global();
    if (!gsGlobal) {
      DPRINTF("ui: gs recreate failed\n");
      return;
    }
    s_gsHires = wantHires;
    viewStackSetGs(viewStack, gsGlobal);
    if (setupDisplayFromConfig() != 0)
      DPRINTF("ui: display setup after recreate failed\n");
    return;
  }

  // Refresh mode/screen/assets without destroying GSGLOBAL.
  releaseUiGsClients();
  if (setupDisplayFromConfig() != 0)
    DPRINTF("ui: display refresh failed\n");
}

int uiInit(void) {
  setUiLanguageByCode(getLang());

  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  if (dmaKit_chan_init(DMA_CHANNEL_GIF) != 0) {
    displayFatalError("ui: DMAC init failed\n");
    return -1;
  }

  s_gsHires = (getVMode() == VMode_720p);
  if (s_gsHires)
    gsGlobal = gsKit_hires_init_global();
  else
    gsGlobal = gsKit_init_global();
  if (!gsGlobal) {
    displayFatalError("ui: gsKit_init_global failed\n");
    return -1;
  }

  if (setupDisplayFromConfig() != 0) {
    displayFatalError("ui: failed to load font (file and embedded)\n");
    fontEnd();
    deinitGsGlobal();
    return -1;
  }

  initPad();

  mainSceneInit();
  uiContextInit(&uiContext, mainSceneGetData());

  viewStack = viewStackCreate(gsGlobal);
  if (!viewStack) {
    displayFatalError("ui: view stack create failed\n");
    mainSceneCleanup();
    closePad();
    fontEnd();
    deinitGsGlobal();
    return -1;
  }

  if (workerStart() != 0) {
    displayFatalError("ui: worker start failed\n");
    viewStackDestroy(viewStack);
    viewStack = NULL;
    mainSceneCleanup();
    closePad();
    fontEnd();
    deinitGsGlobal();
    return -1;
  }
  ChangeThreadPriority(GetThreadId(), 2);

  View *splashView = splashGetView();
  static SplashUserdata splashUserdata;
  splashUserdata.version = GIT_VERSION;
  splashView->userdata = &splashUserdata;
  if (viewStackPush(viewStack, splashView) != 0) {
    workerStop();
    displayFatalError("ui: push splash failed\n");
    viewStackDestroy(viewStack);
    viewStack = NULL;
    mainSceneCleanup();
    closePad();
    fontEnd();
    deinitGsGlobal();
    return -1;
  }

  uiContext.listUpdating = 1;
  if (workerRequest(WorkerJob_InitList) != 0) {
    workerStop();
    displayFatalError("ui: InitList request failed\n");
    viewStackDestroy(viewStack);
    viewStack = NULL;
    mainSceneCleanup();
    closePad();
    fontEnd();
    deinitGsGlobal();
    return -1;
  }

  return 0;
}

void uiCleanup(void) {
  workerStop();
  if (viewStack) {
    viewStackDestroy(viewStack);
    viewStack = NULL;
  }
  mainSceneCleanup();
  closePad();
  fontEnd();
  freeIcons();
  deinitGsGlobal();
}

void uiMain(void) {
  if (uiInit() != 0)
    return;

  static int splashDone = 0;
  DPRINTF("ui: entering main loop\n");
  while (1) {
    uiPollWorker();

    int exitLoop = viewStackRunFrame(viewStack);
    if (exitLoop && viewStackCount(viewStack) == 0) {
      if (!splashDone) {
        splashDone = 1;
        viewStackPush(viewStack, mainSceneGetView());
      } else {
        break;
      }
    }
    if (viewStackCount(viewStack) == 0)
      break;
    if (uiContext.exitRequested)
      break;

    RotateThreadReadyQueue(1);
  }

  uiCleanup();
}
