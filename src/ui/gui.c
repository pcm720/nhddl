// src/ui/gui.c
#include "common.h"
#include "dprintf.h"
#include "neutrino.h"
#include "options.h"
#include "ui/args.h"
#include "ui/graphics.h"
#include "ui/pad.h"
#include "ui/ui.h"
#include <dmaKit.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <kernel.h>
#include <libpad.h>
#include <malloc.h>
#include <ps2sdkapi.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define DIV_ROUND(n, d) (n + (d - 1)) / d

// Assuming 140x200 cover art
#define COVER_ART_RES_W 140
#define COVER_ART_RES_H 200

void closeUI();
int uiLoop(TargetList *titles);
int uiTitleOptionsLoop(Target *title);
int uiArgumentListLoop(Target *target, ArgumentList *titleArguments);
void drawTitleList(TargetList *titles, int selectedTitleIdx, int maxTitlesPerPage, GSTEXTURE *covers[], int visibleCount);
void uiLaunchTitle(Target *target, ArgumentList *arguments);
void drawGameID(const char *game_id);
int createSplashThread();
void uiSplashThread();
void closeUISplashThread();

GSGLOBAL *gsGlobal;
static GSTEXTURE *coverTexture;
static char lineBuffer[255];

// Path relative to storage device mountpoint.
// Used to load cover art
static const char artPath[] = "/ART";

// Cover art sprite coordinates
// Initialized during uiInit from screen width and height
static int coverArtX2;
static int coverArtY2;
static int coverArtX1;
static int coverArtY1;

static const int keepoutArea = 20;
static const int headerHeight = 20 + keepoutArea;
static const int footerHeight = 40 + keepoutArea;

// Carousel defaults
#define CAROUSEL_VISIBLE 5
#define CAROUSEL_HALF ((CAROUSEL_VISIBLE)/2)
#define CAROUSEL_ANIM_FRAMES 10

typedef struct {
  GSTEXTURE *tex;
  int loaded;        // 0 = not loaded, 1 = loaded OK, -1 = missing
  int titleIdx;      // which title index this slot represents
} CarouselSlot;

static CarouselSlot carousel[CAROUSEL_VISIBLE];
static int carouselVisible = CAROUSEL_VISIBLE;

// Animation state
static int animating = 0;           // 0 = idle, 1 = animating
static int anim_dir = 0;            // +1 = move next (down), -1 = move prev (up)
static int anim_frame = 0;
static int anim_frames = CAROUSEL_ANIM_FRAMES;
static float anim_t = 0.0f;         // eased progress 0..1
static int targetSelectedIdx = 0;
static const int slideDistance = 140; // horizontal slide distance per step (tweakable)

// Helper: load PNG into provided GSTEXTURE, returning 0 on success, -1 on failure
static int loadCoverIntoTex(GSTEXTURE *tex, struct DeviceMapEntry *device, char *titleID) {
  if (!tex || !device || !titleID)
    return -1;
  if (device->metadev) { // fallback to metadata device
    device = device->metadev;
  }
  snprintf(lineBuffer, sizeof(lineBuffer), "%s%s/%s_COV.png", device->mountpoint, artPath, titleID);
  gsKit_TexManager_invalidate(gsGlobal, tex);
  if (gsKit_texture_png(gsGlobal, tex, lineBuffer)) {
    return -1;
  }
  gsKit_TexManager_bind(gsGlobal, tex);
  if (tex->Mem) {
    free(tex->Mem);
    tex->Mem = NULL;
  }
  return 0;
}

// Backwards-compatible single-texture loader (kept for other code)
int loadCoverArt(struct DeviceMapEntry *device, char *titleID) {
  if (device->metadev) {
    device = device->metadev;
  }
  snprintf(lineBuffer, 255, "%s%s/%s_COV.png", device->mountpoint, artPath, titleID);
  gsKit_TexManager_invalidate(gsGlobal, coverTexture);
  if (gsKit_texture_png(gsGlobal, coverTexture, lineBuffer)) {
    return -1;
  }
  gsKit_TexManager_bind(gsGlobal, coverTexture);
  if (coverTexture->Mem) {
    free(coverTexture->Mem);
    coverTexture->Mem = NULL;
  }
  return 0;
}

// Frees textures and deinits gsKit
void closeUI() {
  // Free carousel textures
  for (int i = 0; i < carouselVisible; i++) {
    if (carousel[i].tex) {
      if (carousel[i].tex->Mem) {
        free(carousel[i].tex->Mem);
        carousel[i].tex->Mem = NULL;
      }
      free(carousel[i].tex);
      carousel[i].tex = NULL;
    }
    carousel[i].loaded = 0;
    carousel[i].titleIdx = -1;
  }

  if (coverTexture) {
    if (coverTexture->Mem) {
      free(coverTexture->Mem);
      coverTexture->Mem = NULL;
    }
    free(coverTexture);
    coverTexture = NULL;
  }

  gsKit_vram_clear(gsGlobal);
  closeFont();
  gsKit_deinit_global(gsGlobal);
}

void initVMode(GSGLOBAL *gsGlobal) {
  switch (LAUNCHER_OPTIONS.vmode) {
  case GS_MODE_NTSC:
    DPRINTF("Forcing NTSC mode\n");
    gsGlobal->Mode = GS_MODE_NTSC;
    gsGlobal->Interlace = GS_INTERLACED;
    gsGlobal->Field = GS_FIELD;
    gsGlobal->Width = 640;
    gsGlobal->Height = 448;
    break;
  case GS_MODE_PAL:
    DPRINTF("Forcing PAL mode\n");
    gsGlobal->Mode = GS_MODE_PAL;
    gsGlobal->Interlace = GS_INTERLACED;
    gsGlobal->Field = GS_FIELD;
    gsGlobal->Width = 640;
    gsGlobal->Height = 512;
    break;
  case GS_MODE_DTV_480P:
    DPRINTF("Forcing 480p mode\n");
    gsGlobal->Mode = GS_MODE_DTV_480P;
    gsGlobal->Interlace = GS_NONINTERLACED;
    gsGlobal->Field = GS_FRAME;
    gsGlobal->Width = 640;
    gsGlobal->Height = 448;
    break;
  default:
  }
}

int uiInit() {
  if (gsGlobal != NULL) {
    DPRINTF("Reinitializing UI\n");
    closeUI();
  }
  gsGlobal = gsKit_init_global();
  initVMode(gsGlobal);
  gsGlobal->PSM = GS_PSM_CT24; // Set color depth to avoid PAL VRAM issues
  gsGlobal->PSMZ = GS_PSMZ_16S;
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsGlobal->DoubleBuffering = GS_SETTING_ON;
  // Setup TEST register to ignore fully transparent pixels
  gsGlobal->Test->ATST = 7;    // Set alpha test method to NOTEQUAL (pixels with A not equal to AREF pass)
  gsGlobal->Test->AREF = 0x00; // Set reference value to 0x00 (transparent)
  gsGlobal->Test->AFAIL = 0;   // Don't update buffers when test fails

  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

  // Initialize the DMAC
  int res;
  if ((res = dmaKit_chan_init(DMA_CHANNEL_GIF))) {
    DPRINTF("ERROR: Failed to initlize DMAC: %d\n", res);
    return res;
  }

  // Init screen
  gsKit_vram_clear(gsGlobal);
  gsKit_init_screen(gsGlobal);
  gsKit_display_buffer(gsGlobal); // Switch display buffer to avoid garbage appearing on screen
  gsKit_TexManager_init(gsGlobal);
  // Set alpha and mode, clear active buffer
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_mode_switch(gsGlobal, GS_ONESHOT);
  gsKit_clear(gsGlobal, BGColor);

  // Initialize resources
  if (initGraphics()) {
    DPRINTF("ERROR: Failed to initialize font\n");
    return -1;
  };

  // Init cover texture (legacy single texture kept)
  coverTexture = calloc(sizeof(GSTEXTURE), 1);

  // Initialize carousel slots
  for (int i = 0; i < carouselVisible; i++) {
    carousel[i].tex = calloc(sizeof(GSTEXTURE), 1);
    carousel[i].tex->Delayed = 1;
    carousel[i].loaded = 0;
    carousel[i].titleIdx = -1;
  }

  coverArtX2 = (gsGlobal->Width - keepoutArea - 10);
  coverArtY2 = (gsGlobal->Height / 2) + (COVER_ART_RES_H / 2);
  coverArtX1 = coverArtX2 - COVER_ART_RES_W;
  coverArtY1 = coverArtY2 - COVER_ART_RES_H;
  coverTexture->Delayed = 1;

  // reset animation state
  animating = 0;
  anim_dir = 0;
  anim_frame = 0;
  anim_t = 0.0f;
  targetSelectedIdx = 0;

  return 0;
}

// Main UI loop. Displays the target list.
int uiLoop(TargetList *titles) {
  // Reinitialize UI if video mode doesn't match
  if ((LAUNCHER_OPTIONS.vmode != VMODE_NONE) && (gsGlobal->Mode != LAUNCHER_OPTIONS.vmode)) {
    uiInit();
  }

  int res = 0;
  if ((gsGlobal == NULL) && (res = uiInit())) {
    DPRINTF("ERROR: Failed to init UI: %d\n", res);
    goto exit;
  }
  // Init gamepad inputs
  initPad();

  int selectedTitleIdx = 0;
  int maxTitlesPerPage = (gsGlobal->Height - (headerHeight + footerHeight)) / getFontLineHeight();
  Target *curTarget = titles->first;

  // Get last launched title and find it in the target list
  char *lastTitle = calloc(sizeof(char), PATH_MAX + 1);
  if (!getLastLaunchedTitle(lastTitle)) {
    int mountpointLen;
    while (curTarget != NULL) {
      // Compare paths without the mountpoint
      mountpointLen = getRelativePathIdx(curTarget->fullPath);
      if (mountpointLen == -1)
        mountpointLen = 0;

      if (!strcmp(lastTitle, &curTarget->fullPath[mountpointLen])) {
        selectedTitleIdx = curTarget->idx;
        break;
      }
      curTarget = curTarget->next;
    }
    // Reinitialize target if last launched title couldn't be loaded
    if (curTarget == NULL) {
      curTarget = titles->first;
    }
  }
  free(lastTitle);

  // Initialize carousel: load visible covers around selectedTitleIdx
  int total = titles->total;
  for (int s = 0; s < carouselVisible; s++) {
    int offset = s - CAROUSEL_HALF; // -2,-1,0,1,2 for CAROUSEL_VISIBLE=5
    int idx = ((selectedTitleIdx + offset) % total + total) % total;
    Target *t = getTargetByIdx(titles, idx);
    if (!t) {
      carousel[s].loaded = -1;
      carousel[s].titleIdx = -1;
      continue;
    }
    if (loadCoverIntoTex(carousel[s].tex, t->device, t->id) == 0) {
      carousel[s].loaded = 1;
      carousel[s].titleIdx = idx;
    } else {
      carousel[s].loaded = -1;
      carousel[s].titleIdx = idx;
    }
  }

  // Main UI loop
  int frameCount = 0;
  int prevInput = 0;
  int input = 0;
  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);

    // Advance animation if running
    if (animating) {
      anim_frame++;
      if (anim_frame >= anim_frames) {
        // finalize animation
        animating = 0;
        anim_t = 1.0f;
        // commit selection
        selectedTitleIdx = targetSelectedIdx;
        // reload carousel around new selection to ensure textures match
        for (int s = 0; s < carouselVisible; s++) {
          int offset = s - CAROUSEL_HALF;
          int idx = ((selectedTitleIdx + offset) % total + total) % total;
          Target *t = getTargetByIdx(titles, idx);
          if (!t) {
            carousel[s].loaded = -1;
            carousel[s].titleIdx = -1;
          Continue...