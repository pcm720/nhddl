#include "backends/backends.h"
#include "config/config.h"
#include "config/nhddl.h"
#include "devices/pad.h"
#include "dprintf.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/scale.h"
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
#define MAX_DEVICE_TYPES 16
#define OVERLAY_DURATION_FRAMES 120

GSGLOBAL *gsGlobal;
struct ViewStack *viewStack;

static char osdMessage[96];
static int osdFramesLeft;

void showOSD(const char *msg, int frames) {
  strncpy(osdMessage, msg, sizeof(osdMessage) - 1);
  osdMessage[sizeof(osdMessage) - 1] = '\0';
  osdFramesLeft = frames > 0 ? frames : OVERLAY_DURATION_FRAMES;
  viewStackSetOverlay(viewStack, osdOverlayGetView(), osdFramesLeft);
}

const char *getOSDMessage(void) { return osdMessage; }

int getOSDFramesLeft(void) { return osdFramesLeft; }

static void initViews(void) {
  if (initIcons(gsGlobal) != 0)
    DPRINTF("ui: initIcons failed, footer will show text only\n");
}

static void initVMode(GSGLOBAL *gs) {
  scaleSetDisplayAspect(getWidescreen() ? ASPECT_16_9 : ASPECT_4_3);
  VModeType v = getVMode();
  scaleApplyVMode(gs, v);
}

static int buildFontPath(char *buf, size_t size) {
  const char *root = getNHDDLRoot();
  if (root && root[0]) {
    size_t rlen = strlen(root);
    size_t need = rlen + strlen(FONT_PATH_THEME) + 2;
    if (need <= size) {
      memcpy(buf, root, rlen + 1);
      if (rlen > 0 && buf[rlen - 1] != '/')
        strcat(buf, "/");
      strcat(buf, FONT_PATH_THEME);
      return 0;
    }
  }
  return -1;
}

int uiInit(void) {
  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  if (dmaKit_chan_init(DMA_CHANNEL_GIF) != 0) {
    displayFatalError("ui: DMAC init failed\n");
    return -1;
  }

  if (getVMode() == VMode_720p)
    gsGlobal = gsKit_hires_init_global(); // For 720p, HIRES is required
  else
    gsGlobal = gsKit_init_global();
  if (!gsGlobal) {
    displayFatalError("ui: gsKit_init_global failed\n");
    return -1;
  }

  initVMode(gsGlobal);
  if (gsGlobal->Width * gsGlobal->Height > 704 * 576) {
    gsGlobal->PSM = GS_PSM_CT16S;
    gsGlobal->DoubleBuffering = GS_SETTING_OFF;
  } else {
    gsGlobal->PSM = GS_PSM_CT24;
    gsGlobal->DoubleBuffering = GS_SETTING_ON;
  }
  gsGlobal->PSMZ = GS_PSMZ_16S;
  gsGlobal->ZBuffering = GS_SETTING_ON;
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsGlobal->Test->ATST = 7;
  gsGlobal->Test->AREF = 0x00;
  gsGlobal->Test->AFAIL = 0;

  gsKit_vram_clear(gsGlobal);

  if (getVMode() == VMode_720p)
    gsKit_hires_init_screen(gsGlobal, 2); // Use two passes
  else {
    gsKit_init_screen(gsGlobal);
    gsKit_mode_switch(gsGlobal, GS_ONESHOT);
  }
  gsKit_display_buffer(gsGlobal);
  gsKit_TexManager_init(gsGlobal);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_test(gsGlobal, GS_ZTEST_ON);
  gsKit_clear(gsGlobal, BGColor);

  scaleUpdate(gsGlobal);
  fontInit(gsGlobal);

  char path[256];
  int fontLoaded = 0;
  if (buildFontPath(path, sizeof(path)) == 0 && fontLoadFile(path, 0) == 0)
    fontLoaded = 1;
  if (!fontLoaded && fontLoadMemory(font_ttf, (size_t)size_font_ttf, 0) != 0) {
    displayFatalError("ui: failed to load font (file and embedded)\n");
    fontEnd();
    gsKit_deinit_global(gsGlobal);
    gsGlobal = NULL;
    return -1;
  }
  fontUpdateAspectRatio();

  initPad();

  mainSceneInit();
  initViews();

  viewStack = viewStackCreate(gsGlobal);
  if (!viewStack) {
    displayFatalError("ui: view stack create failed\n");
    mainSceneCleanup();
    closePad();
    fontEnd();
    gsKit_deinit_global(gsGlobal);
    gsGlobal = NULL;
    return -1;
  }

  if (workerStart() != 0) {
    displayFatalError("ui: worker start failed\n");
    viewStackDestroy(viewStack);
    mainSceneCleanup();
    closePad();
    fontEnd();
    gsKit_deinit_global(gsGlobal);
    gsGlobal = NULL;
    return -1;
  }
  // Make UI thread priority lower to prioritize worker thread
  ChangeThreadPriority(GetThreadId(), 2);

  View *splashView = splashGetView();
  static SplashUserdata splashUserdata;
  splashUserdata.version = GIT_VERSION;
  splashView->userdata = &splashUserdata;
  if (viewStackPush(viewStack, splashView) != 0) {
    workerStop();
    displayFatalError("ui: push splash failed\n");
    viewStackDestroy(viewStack);
    mainSceneCleanup();
    closePad();
    fontEnd();
    gsKit_deinit_global(gsGlobal);
    gsGlobal = NULL;
    return -1;
  }

  workerEnqueueInitList();

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
  if (gsGlobal) {
    gsKit_vram_clear(gsGlobal);
    gsKit_deinit_global(gsGlobal);
    gsGlobal = NULL;
  }
}

void uiMain(void) {
  if (uiInit() != 0)
    return;

  static int splashDone = 0;
  DPRINTF("ui: entering main loop\n");
  while (1) {
    if (workerHasError()) {
      char msg[WORKER_ERROR_MESSAGE_SIZE];
      int frames;
      if (workerConsumeError(msg, sizeof(msg), &frames))
        showOSD(msg, frames);
    }
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

    // Yield to let other threads run
    RotateThreadReadyQueue(1);
  }

  uiCleanup();
}
