#include "common.h"
#include "dprintf.h"
#include "favorites.h"
#include "neutrino.h"
#include "options.h"
#include "ui/args.h"
#include "ui/coverart.h"
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

#define DIV_ROUND(n, d) (n + (d - 1)) / d

// Assuming 140x200 cover art
#define COVER_ART_RES_W 140
#define COVER_ART_RES_H 200

void closeUI();
int uiLoop(TargetList *titles);
int uiTitleOptionsLoop(Target *title);
int uiArgumentListLoop(Target *target, ArgumentList *titleArguments);
void drawTitleView(Target **view, int viewTotal, const char *viewName, int selectedIdx, int maxTitlesPerPage, GSTEXTURE *selectedTitleCover);

// Title list view modes (cycled with Select)
typedef enum {
  VIEW_ALL = 0,
  VIEW_FAVORITES,
  VIEW_RECENT,
  VIEW_COUNT,
} TitleViewMode;
static const char *viewNames[] = {"Title List", "Favorites", "Recently Played"};

// Fills view with targets for the given mode and returns the view size.
// view must have room for titles->total pointers.
static int buildTitleView(TargetList *titles, Target **view, TitleViewMode mode) {
  int total = 0;
  Target *cur;

  if (mode == VIEW_RECENT) {
    // Order by recency; ranks can have gaps if a recently-played title
    // no longer exists in the list, so scan a fixed rank range
    for (int rank = 0; rank < 16; rank++) {
      cur = titles->first;
      while (cur != NULL) {
        if (favoritesGetRecentRank(cur) == rank) {
          view[total++] = cur;
          break;
        }
        cur = cur->next;
      }
    }
    return total;
  }

  cur = titles->first;
  while (cur != NULL) {
    if ((mode == VIEW_ALL) || favoritesIsFavorite(cur))
      view[total++] = cur;
    cur = cur->next;
  }
  return total;
}
void uiLaunchTitle(Target *target, ArgumentList *arguments);
void drawGameID(const char *game_id);
int createSplashThread();
void uiSplashThread();
void closeUISplashThread();

GSGLOBAL *gsGlobal;
static char lineBuffer[255];

// Cover art sprite coordinates
// Initialized during uiInit from screen width and height
static int coverArtX2;
static int coverArtY2;
static int coverArtX1;
static int coverArtY1;

static const int keepoutArea = 20;
static const int headerHeight = 20 + keepoutArea;
static const int footerHeight = 40 + keepoutArea;

//
// VSync handling
//
// gsKit_sync_flip/gsKit_vsync_wait busy-wait on the GS CSR register, which
// keeps the main thread spinning at full priority and starves lower-priority
// threads (the async cover art loader would never run). Instead, a vsync
// interrupt handler signals a semaphore so the main thread truly sleeps until
// the next vertical blank, yielding the CPU to background threads.
//

static int32_t vsyncSemaID = -1;
static int vsyncHandlerID = -1;

static int vsyncHandler(int cause) {
  if (vsyncSemaID >= 0)
    iSignalSema(vsyncSemaID);
  ExitHandler();
  return 0;
}

// Blocks until the next vertical blank without spinning.
// No-op if the UI is not initialized.
void uiWaitVSync() {
  if (vsyncSemaID < 0)
    return;
  // Drain stale signals to lock onto the next vsync edge
  while (PollSema(vsyncSemaID) == vsyncSemaID) {
  }
  WaitSema(vsyncSemaID);
}

// Drop-in replacement for gsKit_sync_flip that sleeps instead of spinning
static void uiSyncFlip() {
  if (!gsGlobal->FirstFrame) {
    if (vsyncSemaID >= 0)
      uiWaitVSync();
    else
      gsKit_vsync_wait(); // Fallback: spin like gsKit_sync_flip would

    if (gsGlobal->DoubleBuffering == GS_SETTING_ON) {
      GS_SET_DISPFB2(gsGlobal->ScreenBuffer[gsGlobal->ActiveBuffer & 1] / 8192, gsGlobal->Width / 64, gsGlobal->PSM, 0, 0);
      gsGlobal->ActiveBuffer ^= 1;
    }
  }
  gsKit_setactive(gsGlobal);
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
  case GS_MODE_DTV_576P:
    DPRINTF("Forcing 576p mode\n");
    gsGlobal->Mode = GS_MODE_DTV_576P;
    gsGlobal->Interlace = GS_NONINTERLACED;
    gsGlobal->Field = GS_FRAME;
    gsGlobal->Width = 640;
    gsGlobal->Height = 512;
    break;
  // HD modes (require component cables). The GS upscales the 640-wide
  // framebuffer horizontally on output; a 16-bit framebuffer keeps the
  // full-height render targets inside the 4 MB of VRAM.
  case GS_MODE_DTV_720P:
    DPRINTF("Forcing 720p mode\n");
    gsGlobal->Mode = GS_MODE_DTV_720P;
    gsGlobal->Interlace = GS_NONINTERLACED;
    gsGlobal->Field = GS_FRAME;
    gsGlobal->Width = 640; // Scaled to 1280 on output
    gsGlobal->Height = 720;
    gsGlobal->PSM = GS_PSM_CT16S;
    break;
  case GS_MODE_DTV_1080I:
    DPRINTF("Forcing 1080i mode\n");
    gsGlobal->Mode = GS_MODE_DTV_1080I;
    gsGlobal->Interlace = GS_INTERLACED;
    gsGlobal->Field = GS_FRAME; // Full-height frame; gsKit sets SMODE2 for 1080i
    gsGlobal->Width = 640; // Scaled to 1920 on output
    gsGlobal->Height = 1080;
    gsGlobal->PSM = GS_PSM_CT16S;
    // Two 640x1080 buffers plus Z would exceed 4 MB of VRAM
    gsGlobal->DoubleBuffering = GS_SETTING_OFF;
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
  gsGlobal->PSM = GS_PSM_CT24; // Set color depth to avoid PAL VRAM issues
  gsGlobal->PSMZ = GS_PSMZ_16S;
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  gsGlobal->DoubleBuffering = GS_SETTING_ON;
  // Applied after the defaults above: HD modes override PSM/DoubleBuffering
  // to fit their larger framebuffers into VRAM
  initVMode(gsGlobal);
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

  // Set up blocking vsync waits (see uiSyncFlip)
  ee_sema_t vsyncSema;
  vsyncSema.init_count = 0;
  vsyncSema.max_count = 1;
  vsyncSema.option = 0;
  vsyncSemaID = CreateSema(&vsyncSema);
  if (vsyncSemaID >= 0) {
    vsyncHandlerID = gsKit_add_vsync_handler(vsyncHandler);
    if (vsyncHandlerID < 0) {
      // No handler to signal the semaphore: waiting on it would deadlock.
      // Drop it so uiSyncFlip falls back to the spinning gsKit_vsync_wait.
      DeleteSema(vsyncSemaID);
      vsyncSemaID = -1;
    }
  }

  // Init cover art sprite coordinates and async loader
  coverArtX2 = (gsGlobal->Width - keepoutArea - 10);
  coverArtY2 = (gsGlobal->Height / 2) + (COVER_ART_RES_H / 2);
  coverArtX1 = coverArtX2 - COVER_ART_RES_W;
  coverArtY1 = coverArtY2 - COVER_ART_RES_H;
  if (coverArtInit()) {
    // Not fatal: the UI works without cover art
    DPRINTF("ERROR: Failed to start cover art loader\n");
  }

  return 0;
}

// Frees textures and deinits gsKit
void closeUI() {
  coverArtShutdown();
  if (vsyncHandlerID >= 0) {
    gsKit_remove_vsync_handler(vsyncHandlerID);
    vsyncHandlerID = -1;
  }
  if (vsyncSemaID >= 0) {
    DeleteSema(vsyncSemaID);
    vsyncSemaID = -1;
  }
  gsKit_vram_clear(gsGlobal);
  closeFont();
  gsKit_deinit_global(gsGlobal);
}

// Main UI loop. Displays the target list.
int uiLoop(TargetList *titles) {
  // Reinitialize UI if video mode doesn't match
  if ((LAUNCHER_OPTIONS.vmode != VMODE_NONE) && (gsGlobal->Mode != LAUNCHER_OPTIONS.vmode)) {
    uiInit();
  }

  int res = 0;
  Target **viewList = NULL;
  if ((gsGlobal == NULL) && (res = uiInit())) {
    DPRINTF("ERROR: Failed to init UI: %d\n", res);
    goto exit;
  }
  // Init gamepad inputs
  initPad();

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

  // Load favorites/recently-played and build the initial view
  favoritesInit();
  TitleViewMode viewMode = VIEW_ALL;
  viewList = malloc(sizeof(Target *) * titles->total);
  int viewTotal = buildTitleView(titles, viewList, viewMode);
  int selectedViewIdx = 0;
  for (int i = 0; i < viewTotal; i++) {
    if (viewList[i] == curTarget) {
      selectedViewIdx = i;
      break;
    }
  }

  // Main UI loop
  int frameCount = 0;
  int prevInput = 0;
  int input = 0;
  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);

    // Reload target if selection has changed
    if ((viewTotal > 0) && (curTarget != viewList[selectedViewIdx]))
      curTarget = viewList[selectedViewIdx];

    // Get cover art for the selected title.
    // Loads happen asynchronously: returns NULL until the texture is ready,
    // so the UI never blocks on file IO or PNG decoding while scrolling.
    GSTEXTURE *selectedTitleCover = NULL;
    if (viewTotal > 0)
      selectedTitleCover = coverArtGet(titles, curTarget);

    // Draw title list for the active view
    drawTitleView(viewList, viewTotal, viewNames[viewMode], selectedViewIdx, maxTitlesPerPage, selectedTitleCover);

    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    uiSyncFlip();

    // Process user inputs:
    if (input == -1)            // If input is -1, block until input changes
      input = waitForInput(-1); // Used to ignore held inputs after returning from title options
    else
      input = pollInput();

    if (gsGlobal->Mode == GS_MODE_PAL)
      frameCount = (frameCount + 1) % 8; // Handle input only every 8th frame unless it changes
    else
      frameCount = (frameCount + 1) % 10; // Handle input only every 10th frame unless it changes

    if (frameCount && (input == prevInput))
      continue;

    frameCount = 0;
    prevInput = input;

    if ((input & (PAD_CROSS | PAD_CIRCLE)) && (viewTotal > 0)) {
      // Quiesce cover art IO before launching
      coverArtPause();
      // Copy target, free title list and launch
      Target *target = copyTarget(curTarget);
      free(viewList);
      freeTargetList(titles);
      uiLaunchTitle(target, NULL);
      // Something went wrong, main loop must exit immediately
      return -1;
    } else if ((input & PAD_UP) && (viewTotal > 0)) {
      // Point to the previous title
      selectedViewIdx = ((selectedViewIdx - 1) + viewTotal) % viewTotal;
    } else if ((input & PAD_DOWN) && (viewTotal > 0)) {
      // Advance to the next title
      selectedViewIdx = (selectedViewIdx + 1) % viewTotal;
    } else if ((input & PAD_R1) && (viewTotal > 0)) {
      // Switch to the next page
      if (selectedViewIdx == viewTotal - 1) {
        selectedViewIdx = 0; // Wrap around if the last title is selected
      } else {
        selectedViewIdx += maxTitlesPerPage;
        if (selectedViewIdx >= viewTotal)
          selectedViewIdx = viewTotal - 1;
      }
    } else if ((input & PAD_L1) && (viewTotal > 0)) {
      // Switch to the previous page
      if (selectedViewIdx == 0) {
        selectedViewIdx = viewTotal - 1; // Wrap around if the first title is selected
      } else {
        selectedViewIdx -= maxTitlesPerPage;
        if (selectedViewIdx < 0)
          selectedViewIdx = 0;
      }
    } else if ((input & PAD_SQUARE) && (viewTotal > 0)) {
      // Toggle favorite for the selected title
      favoritesToggle(curTarget);
      if (viewMode == VIEW_FAVORITES) {
        // Rebuild the view in case the title was just removed from it
        viewTotal = buildTitleView(titles, viewList, viewMode);
        if (selectedViewIdx >= viewTotal)
          selectedViewIdx = (viewTotal > 0) ? (viewTotal - 1) : 0;
      }
    } else if (input & PAD_SELECT) {
      // Cycle between All -> Favorites -> Recently Played views
      viewMode = (viewMode + 1) % VIEW_COUNT;
      viewTotal = buildTitleView(titles, viewList, viewMode);
      selectedViewIdx = 0;
    } else if ((input & PAD_TRIANGLE) && (viewTotal > 0)) {
      input = -1;    // Force UI loop to wait once uiTitleOptionsLoop returns
      prevInput = 0; // Reset previous input
      // Pause cover art IO while the options screens do file IO
      coverArtPause();
      // Enter title options screen
      if ((res = uiTitleOptionsLoop(curTarget)) < 0) {
        // Something went wrong, main loop must exit immediately
        return -1;
      }
      coverArtResume();
    } else if (input & PAD_START) {
      // Quit
      break;
    }
  }

exit:
  free(viewList);
  favoritesFree();
  closePad();
  closeUI();
  return res;
}

void drawTitleListFooter(int baseX) {
  int baseY = gsGlobal->Height - footerHeight;
  drawIconWindow(baseX, baseY, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CIRCLE);
  drawIconWindow(baseX + getIconWidth(ICON_CIRCLE), baseY, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CROSS);
  drawTextWindow(baseX + 5 + getIconWidth(ICON_CIRCLE) + getIconWidth(ICON_CROSS), baseY, 0, gsGlobal->Height - 1, 0, HeaderTextColor, ALIGN_VCENTER,
                 "Launch title");

  drawIconWindow(0, baseY, gsGlobal->Width - getLineWidth("Exit") - 5, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_START);
  drawTextWindow(5 + getIconWidth(ICON_START), baseY, gsGlobal->Width, gsGlobal->Height - 1, 0, HeaderTextColor, ALIGN_CENTER, "Exit");

  drawIconWindow(gsGlobal->Width - baseX - 5 - getIconWidth(ICON_TRIANGLE) - getLineWidth("Title options"), baseY, gsGlobal->Width - baseX,
                 gsGlobal->Height, 0, FontMainColor, ALIGN_VCENTER | ALIGN_LEFT, ICON_TRIANGLE);
  drawTextWindow(0, baseY, gsGlobal->Width - baseX, gsGlobal->Height - 1, 0, HeaderTextColor, ALIGN_VCENTER | ALIGN_RIGHT, "Title options");
}

// Draws the title list for the active view
void drawTitleView(Target **view, int viewTotal, const char *viewName, int selectedIdx, int maxTitlesPerPage, GSTEXTURE *selectedTitleCover) {
  int curPage = (viewTotal > 0) ? (selectedIdx / maxTitlesPerPage) : 0;

  // Draw header and footer
  int titleY = headerHeight;
  int baseX = keepoutArea + 10;
  drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_HCENTER, viewName);
  if (viewTotal > 0) {
    snprintf(lineBuffer, 255, "Page %d/%d\nTitle %d/%d", curPage + 1, DIV_ROUND(viewTotal, maxTitlesPerPage), selectedIdx + 1, viewTotal);
    drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_RIGHT, lineBuffer);
  }

  drawTitleListFooter(baseX);

  if (viewTotal == 0) {
    // Empty view: favorites/recents have no entries yet
    drawTextWindow(0, 0, gsGlobal->Width, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER,
                   "Nothing here yet\nPress Square on a title to add it to Favorites");
    return;
  }

  // Draw title list
  int pageEnd = (curPage + 1) * maxTitlesPerPage;
  if (pageEnd > viewTotal)
    pageEnd = viewTotal;

  titleY += getFontLineHeight() / 2;
  for (int i = curPage * maxTitlesPerPage; i < pageEnd; i++) {
    Target *curTitle = view[i];

    // Draw title ID for selected title
    if (i == selectedIdx) {
      // Draw title ID and device type under the cover art
      drawTextWindow(coverArtX1,
                     drawTextWindow(coverArtX1, coverArtY2 + 5, coverArtX2, 0, 0, FontMainColor, ALIGN_HCENTER,
                                    curTitle->id), // Use y coordinate return by title ID drawing function as an argument
                     coverArtX2, 0, 0, FontMainColor, ALIGN_HCENTER, modeToString(curTitle->device->mode));
    }

    // Draw title name, marking favorites with a star
    if (favoritesIsFavorite(curTitle)) {
      snprintf(lineBuffer, 255, "* %s", curTitle->name);
      titleY = drawText(baseX, titleY, 0, coverArtX1 - 5, 0, ((i == selectedIdx) ? ColorSelected : FontMainColor), lineBuffer);
    } else {
      titleY = drawText(baseX, titleY, 0, coverArtX1 - 5, 0, ((i == selectedIdx) ? ColorSelected : FontMainColor), curTitle->name);
    }
  }

  // Draw cover art placeholder/frame
  gsKit_prim_sprite(gsGlobal, coverArtX1 - 2, coverArtY1 - 2, coverArtX2 + 2, coverArtY2 + 2, 1, FontMainColor);

  // Draw cover art if it exists
  if (selectedTitleCover != NULL) {
    // Temporaily disable alpha blending
    // Some PNGs require inverted alpha channel value to display properly
    // Since cover art has nothing to blend, we can bypass the issue altogether
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_prim_sprite_texture(gsGlobal, selectedTitleCover, coverArtX1, coverArtY1, 0.0f, 0.0f, coverArtX2, coverArtY2, selectedTitleCover->Width,
                              selectedTitleCover->Height, 2, FontMainColor);
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  } else {
    gsKit_prim_sprite(gsGlobal, coverArtX1, coverArtY1, coverArtX2, coverArtY2, 1, BGColor);
    // "Loading..." while the async load is in flight, "No cover art" if it failed
    drawTextWindow(coverArtX1, coverArtY1, coverArtX2, coverArtY2, 1, FontMainColor, ALIGN_CENTER, coverArtStatusText());
  }
}

void drawTitleOptionsFooter(int baseX) {
  drawIconWindow(baseX, gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CIRCLE);
  drawIconWindow(baseX + getIconWidth(ICON_CIRCLE), gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CROSS);
  drawTextWindow(baseX + 5 + getIconWidth(ICON_CIRCLE) + getIconWidth(ICON_CROSS), gsGlobal->Height - 1 - footerHeight, 0, gsGlobal->Height, 0,
                 HeaderTextColor, ALIGN_VCENTER, "Toggle");

  drawIconWindow((gsGlobal->Width * 3 / 8) - getIconWidth(ICON_SQUARE), gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_SQUARE);
  drawTextWindow((gsGlobal->Width * 3 / 8) + 5, gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER,
                 "Test");

  drawIconWindow((gsGlobal->Width * 5 / 8), gsGlobal->Height - footerHeight, gsGlobal->Width - getLineWidth("Save") - 5, gsGlobal->Height, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_START);
  drawTextWindow((gsGlobal->Width * 5 / 8) + 5 + getIconWidth(ICON_START), gsGlobal->Height - 1 - footerHeight, gsGlobal->Width, gsGlobal->Height, 0,
                 HeaderTextColor, ALIGN_VCENTER, "Save");

  drawIconWindow(gsGlobal->Width - baseX - 5 - getIconWidth(ICON_TRIANGLE) - getLineWidth("Cancel"), gsGlobal->Height - footerHeight,
                 gsGlobal->Width - baseX, gsGlobal->Height, 0, FontMainColor, ALIGN_VCENTER | ALIGN_LEFT, ICON_TRIANGLE);
  drawTextWindow(0, gsGlobal->Height - 1 - footerHeight, gsGlobal->Width - baseX, gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER | ALIGN_RIGHT,
                 "Cancel");

  drawTextWindow(0, gsGlobal->Height - 1 - footerHeight - getFontLineHeight() / 2, gsGlobal->Width, gsGlobal->Height, 0, HeaderTextColor,
                 ALIGN_TOP | ALIGN_HCENTER, "Switch views");
  drawIconWindow(0, gsGlobal->Height - footerHeight - getFontLineHeight() / 2, (gsGlobal->Width - getLineWidth("Switch views")) / 2 - 5,
                 gsGlobal->Height, 0, FontMainColor, ALIGN_TOP | ALIGN_RIGHT, ICON_L1);
  drawIconWindow((gsGlobal->Width + getLineWidth("Switch views")) / 2 + 5, gsGlobal->Height - footerHeight - getFontLineHeight() / 2, gsGlobal->Width,
                 gsGlobal->Height, 0, FontMainColor, ALIGN_TOP | ALIGN_LEFT, ICON_R1);
}

// Draws well-known Neutrino arguments
// Returns -1 if error occurs
int uiTitleOptionsLoop(Target *target) {
  int res = 0;

  // Load arguments from config files
  ArgumentList *titleArguments = loadLaunchArgumentLists(target);
  int input = 0;
  int activeArgumentIdx = 0;

  // Parse arguments
  for (int i = 0; i < (uiArgumentsTotal); i++)
    uiArguments[i].parse(&uiArguments[i], titleArguments);

  int baseX = keepoutArea + 10;
  int i = 0;
  while (1) {
    gsKit_clear(gsGlobal, BGColor);

    // Draw header
    snprintf(lineBuffer, 255, "%s\n%s", target->name, target->id);
    drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);

    int startY = headerHeight + 1.5 * getFontLineHeight();
    for (i = 0; i < uiArgumentsTotal; i++) {
      startY = getFontLineHeight() / 2 +
               uiArguments[i].draw(&uiArguments[i], (i == activeArgumentIdx) ? 1 : 0, baseX, startY, 0, gsGlobal->Width - baseX, 0);
    }

    // Draw footer
    drawTitleOptionsFooter(baseX);

    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    uiSyncFlip();

    // Process user inputs
    input = waitForInput(-1);
    if (input & (PAD_L1 | PAD_R1)) {
      // Show full argument list
      if ((res = uiArgumentListLoop(target, titleArguments)))
        goto exit;

      // Re-parse arguments
      activeArgumentIdx = 0;
      for (i = 0; i < uiArgumentsTotal; i++)
        uiArguments[i].parse(&uiArguments[i], titleArguments);
    } else if (input & PAD_SQUARE) {
      // Launch title without saving arguments
      uiLaunchTitle(target, titleArguments);
      res = -1; // If this was somehow reached, something went terribly wrong
      goto exit;
    } else if (input & PAD_START) {
      updateTitleLaunchArguments(target, titleArguments);
      goto exit;
    } else if (input & PAD_TRIANGLE) {
      // Quit to title list
      goto exit;
    } else {
      switch (uiArguments[activeArgumentIdx].handleInput(&uiArguments[activeArgumentIdx], input)) {
      case ACTION_CHANGED:
        uiArguments[activeArgumentIdx].marshal(&uiArguments[activeArgumentIdx], titleArguments);
        break;
      case ACTION_NEXT_ARGUMENT:
        if (activeArgumentIdx < uiArgumentsTotal - 1)
          activeArgumentIdx++;
        break;
      case ACTION_PREV_ARGUMENT:
        if (activeArgumentIdx > 0)
          activeArgumentIdx--;
        break;
      default:
      }
    }
  }
exit:
  freeArgumentList(titleArguments);
  return res;
}

// Handles all arguments in arugment list
// Returns -1 if error occurs, 1 if parent needs to exit to title list
int uiArgumentListLoop(Target *target, ArgumentList *titleArguments) {
  int selectedArgIdx = 0;
  int input = 0;

  Argument *curArgument = titleArguments->first;
  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    int baseX = keepoutArea + 10;

    // Draw header
    snprintf(lineBuffer, 255, "%s\n%s", target->name, target->id);
    drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);
    drawTextWindow(baseX, headerHeight + 1.5 * getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, FontMainColor, ALIGN_HCENTER, "Launch arguments");

    // Draw footer
    drawTitleOptionsFooter(baseX);

    int startY = headerHeight + 2.5 * getFontLineHeight();
    int idx = 0;

    // Set number of elements per page according to line height and available screen height
    int maxArguments = (gsGlobal->Height - startY - footerHeight - getFontLineHeight() / 2) / getFontLineHeight();
    int curPage = selectedArgIdx / maxArguments;

    snprintf(lineBuffer, 255, "Page %d/%d", curPage + 1, (!titleArguments->total) ? 1 : DIV_ROUND(titleArguments->total, maxArguments));
    startY = drawTextWindow(baseX, startY - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_RIGHT, lineBuffer);

    Argument *argument = titleArguments->first;
    while (argument != NULL) {
      // Do not display arguments before the current page
      if (idx < maxArguments * curPage) {
        idx++;
        goto next;
      }
      // Do not display arguments beyond the current page
      if (idx >= maxArguments * (curPage + 1)) {
        break;
      }

      // Draw argument
      if (!argument->isDisabled)
        drawIconWindow(baseX, startY, 20, startY + getFontLineHeight(), 0, FontMainColor, ALIGN_CENTER, ICON_ENABLED);

      snprintf(lineBuffer, 255, "%s%s%s %s", ((argument->isGlobal) ? "[G] " : ""), argument->arg, (!strlen(argument->value)) ? "" : ":",
               argument->value);
      startY = drawText(baseX + getIconWidth(ICON_ENABLED), startY, 0, 0, 0, ((selectedArgIdx == idx) ? ColorSelected : FontMainColor), lineBuffer);

      idx++;
    next:
      argument = argument->next;
    }

    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    uiSyncFlip();

    // Process user inputs
    input = waitForInput(-1);
    if (input & (PAD_L1 | PAD_R1)) {
      return 0;
    } else if (input & PAD_SQUARE) {
      // Launch title without saving arguments
      uiLaunchTitle(target, titleArguments);
      return -1; // If this was somehow reached, something went terribly wrong
    } else if (input & PAD_START) {
      updateTitleLaunchArguments(target, titleArguments);
      return 1;
    } else if (input & PAD_TRIANGLE) {
      return 1;
    }

    // Ignore inputs when the argument is not initialized
    if (!curArgument)
      continue;

    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      // Toggle argument
      curArgument->isDisabled = !curArgument->isDisabled;
      // If the argument was disabled, reset global flag
      if (curArgument->isDisabled)
        curArgument->isGlobal = 0;
    } else if (input & PAD_UP) {
      // Point to the previous argument
      selectedArgIdx = (selectedArgIdx - 1 + titleArguments->total) % titleArguments->total;
      curArgument = (curArgument->prev) ? curArgument->prev : titleArguments->last;
    } else if (input & PAD_DOWN) {
      // Advance to the next argument
      selectedArgIdx = (selectedArgIdx + 1) % titleArguments->total;
      curArgument = (curArgument->next) ? curArgument->next : titleArguments->first;
    }
  }
}

// Displays Game ID and launches the title
void uiLaunchTitle(Target *target, ArgumentList *arguments) {
  // Initialize arugments if not set
  if (arguments == NULL) {
    arguments = loadLaunchArgumentLists(target);
  }

  gsKit_clear(gsGlobal, BGColor);

  // Draw screen with GameID and title parameters
  snprintf(lineBuffer, 255, "Launching\n%s\n%s\n\n%s", target->name, target->id, target->fullPath);
  drawTextWindow(0, 0, gsGlobal->Width, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, lineBuffer);
  drawGameID(target->id);

  gsKit_queue_exec(gsGlobal);
  gsKit_finish();
  uiSyncFlip();

  // Cleanup the UI and launch title
  closePad();
  closeUI();
  launchTitle(target, arguments);
}

//
// GameID code based on https://github.com/CosmicScale/Retro-GEM-PS2-Disc-Launcher
//

static uint8_t calculateCRC(const uint8_t *data, int len) {
  uint8_t crc = 0x00;
  for (int i = 0; i < len; i++) {
    crc += data[i];
  }
  return 0x100 - crc;
}

void drawGameID(const char *gameID) {
  uint8_t data[64] = {0};
  int gidlen = strnlen(gameID, 11); // Ensure the length does not exceed 11 characters

  int dpos = 0;
  data[dpos++] = 0xA5; // detect word
  data[dpos++] = 0x00; // address offset
  dpos++;
  data[dpos++] = gidlen;

  memcpy(&data[dpos], gameID, gidlen);
  dpos += gidlen;

  data[dpos++] = 0x00;
  data[dpos++] = 0xD5; // end word
  data[dpos++] = 0x00; // padding

  int data_len = dpos;
  data[2] = calculateCRC(&data[3], data_len - 3);

  int xstart = (gsGlobal->Width / 2) - (data_len * 8);
  int ystart = gsGlobal->Height - (((gsGlobal->Height / 8) * 2) + 20);
  int height = 2;

  for (int i = 0; i < data_len; i++) {
    for (int j = 7; j >= 0; j--) {
      int x = xstart + (i * 16 + ((7 - j) * 2));
      int x1 = x + 1;
      gsKit_prim_sprite(gsGlobal, x, ystart, x1, ystart + height, 0, GS_SETREG_RGBA(0xFF, 0x00, 0xFF, 0x80));

      uint32_t color = (data[i] >> j) & 1 ? GS_SETREG_RGBA(0x00, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0xFF, 0xFF, 0x00, 0x80);
      gsKit_prim_sprite(gsGlobal, x1, ystart, x1 + 1, ystart + height, 0, color);
    }
  }
}

//
// Splash screen functions
//

struct {
  int32_t doneSema;          // Used to signal UI splash thread to exit
  int32_t newStringSema;     // Used to signal UI splash thread that a new string is ready
  int32_t drawnSema;         // Used to signal that UI splash thread has finished drawing or closed
  UILogLevelType level;      // Log level
  char neutrinoVersion[100]; // Neutrino version string
  char buf[255];             // String buffer. String must be null-terminated
  int progressCur;           // Progress counter (LEVEL_PROGRESS)
  int progressTotal;         // Progress total; 0 = unknown (LEVEL_PROGRESS)
} logBuffer = {};
#define THREAD_STACK_SIZE 0x1000
static uint8_t threadStack[THREAD_STACK_SIZE] __attribute__((aligned(16)));

// Initializes and starts UI splash thread
int startSplashScreen() {
  DPRINTF("Starting UI splash thread\n");
  // Initialize splash semaphores
  ee_sema_t semaphore;
  semaphore.init_count = 0;
  semaphore.max_count = 1;
  semaphore.option = 0;
  logBuffer.drawnSema = CreateSema(&semaphore);
  logBuffer.newStringSema = CreateSema(&semaphore);
  logBuffer.doneSema = CreateSema(&semaphore);

  // Initialize thread
  ee_thread_t thread;
  thread.func = uiSplashThread;
  thread.stack = threadStack;
  thread.stack_size = THREAD_STACK_SIZE;
  thread.gp_reg = &_gp;
  thread.initial_priority = 0x2;
  thread.attr = thread.option = 0;

  // Start thread
  int32_t threadID;
  if ((threadID = CreateThread(&thread)) >= 0) {
    if (StartThread(threadID, NULL) < 0) {
      DeleteThread(threadID);
      threadID = -1;
    }
  }

  return threadID;
}

// Draws loading splash screen in a separate thread
void uiSplashThread() {
  // Draw logo and version
  gsKit_mode_switch(gsGlobal, GS_PERSISTENT);
  gsKit_TexManager_nextFrame(gsGlobal);
  gsKit_clear(gsGlobal, BGColor);
  drawLogo((gsGlobal->Width - getLogoWidth()) / 2, gsGlobal->Height / 4, 2);
  drawTextWindow(0, (gsGlobal->Height / 4 + getLogoHeight() + 10), gsGlobal->Width, 0, 0, GS_SETREG_RGBA(0x40, 0x40, 0x40, 0x80), ALIGN_HCENTER,
                 GIT_VERSION);
  gsKit_mode_switch(gsGlobal, GS_ONESHOT);

  drawGameID("NHDDL");

  uint64_t color = HeaderTextColor;
  int logStartY = gsGlobal->Height - footerHeight - getFontLineHeight() * 3;
  // Loop until something sends a signal
  while (PollSema(logBuffer.doneSema) != logBuffer.doneSema) {
    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    gsKit_sync_flip(gsGlobal);
    // Wait until a new string is written to buffer
    WaitSema(logBuffer.newStringSema);
    gsKit_TexManager_nextFrame(gsGlobal);

    if (logBuffer.level == LEVEL_PROGRESS) {
      // Erase the log area, then draw the label with a counter and
      // a progress bar (known total) or a spinner (unknown total)
      static const char spinnerFrames[] = "|/-\\";
      static int spinnerIdx = 0;
      gsKit_prim_sprite(gsGlobal, 0, logStartY, gsGlobal->Width, gsGlobal->Height - footerHeight, 0, BGColor);
      if (logBuffer.progressTotal > 0) {
        snprintf(lineBuffer, 255, "%s\n%d/%d", logBuffer.buf, logBuffer.progressCur, logBuffer.progressTotal);
        drawTextWindow(0, logStartY, gsGlobal->Width, gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);
        // Progress bar: outline with proportional fill
        int barW = gsGlobal->Width / 2;
        int barX = (gsGlobal->Width - barW) / 2;
        int barY = gsGlobal->Height - footerHeight - 10;
        int fillW = ((barW - 4) * logBuffer.progressCur) / logBuffer.progressTotal;
        gsKit_prim_sprite(gsGlobal, barX, barY, barX + barW, barY + 8, 0, FontMainColor);
        gsKit_prim_sprite(gsGlobal, barX + 2, barY + 2, barX + barW - 2, barY + 6, 0, BGColor);
        gsKit_prim_sprite(gsGlobal, barX + 2, barY + 2, barX + 2 + fillW, barY + 6, 0, ColorSelected);
      } else {
        spinnerIdx = (spinnerIdx + 1) % 4;
        snprintf(lineBuffer, 255, "%s %c\n%d found", logBuffer.buf, spinnerFrames[spinnerIdx], logBuffer.progressCur);
        drawTextWindow(0, logStartY, gsGlobal->Width, gsGlobal->Height - footerHeight, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);
      }
      if (logBuffer.neutrinoVersion[0] != '\0')
        drawTextWindow(0, (gsGlobal->Height / 4 + getLogoHeight() + getFontLineHeight() + 10), gsGlobal->Width, 0, 0,
                       GS_SETREG_RGBA(0x40, 0x40, 0x40, 0x80), ALIGN_HCENTER, logBuffer.neutrinoVersion);
      SignalSema(logBuffer.drawnSema);
      continue;
    }

    switch (logBuffer.level) {
    case LEVEL_INFO_NODELAY:
    case LEVEL_INFO:
      color = HeaderTextColor;
      break;
    case LEVEL_WARN:
      color = WarnTextColor;
      break;
    case LEVEL_ERROR:
      color = ErrorTextColor;
      break;
    }
    drawTextWindow(0, logStartY, gsGlobal->Width, gsGlobal->Height - footerHeight, 0, color, ALIGN_CENTER, logBuffer.buf);
    if (logBuffer.neutrinoVersion[0] != '\0')
      drawTextWindow(0, (gsGlobal->Height / 4 + getLogoHeight() + getFontLineHeight() + 10), gsGlobal->Width, 0, 0,
                     GS_SETREG_RGBA(0x40, 0x40, 0x40, 0x80), ALIGN_HCENTER, logBuffer.neutrinoVersion);
    SignalSema(logBuffer.drawnSema);
  }
  gsKit_queue_reset(gsGlobal->Per_Queue);
  DeleteSema(logBuffer.doneSema);
  DeleteSema(logBuffer.newStringSema);
  SignalSema(logBuffer.drawnSema);
  ExitDeleteThread();
}

// Stops UI splash thread
void stopUISplashThread() {
  SignalSema(logBuffer.doneSema);
  SignalSema(logBuffer.newStringSema);
  WaitSema(logBuffer.drawnSema);
  DeleteSema(logBuffer.drawnSema);
}

// Logs to splash screen and debug console in a thread-safe way
void uiSplashLogString(UILogLevelType level, const char *str, ...) {
  va_list args;
  va_start(args, str);

  logBuffer.level = level;
  vsnprintf(logBuffer.buf, 255, str, args);
  va_end(args);
  DPRINTF(logBuffer.buf);

  if (!gsGlobal)
    return;

  SignalSema(logBuffer.newStringSema);
  WaitSema(logBuffer.drawnSema);

  switch (level) {
  case LEVEL_INFO_NODELAY:
    return;
  case LEVEL_INFO:
    sleep(1);
    return;
  case LEVEL_WARN:
  case LEVEL_ERROR:
    sleep(2);
    return;
  }
}

// Shows scan progress on the splash screen without delay (thread-safe).
// total > 0 draws a progress bar with a cur/total counter;
// total == 0 draws a spinner with just the count (unknown total).
void uiSplashLogProgress(const char *label, int cur, int total) {
  if (!gsGlobal)
    return;

  logBuffer.level = LEVEL_PROGRESS;
  snprintf(logBuffer.buf, 255, "%s", label);
  logBuffer.progressCur = cur;
  logBuffer.progressTotal = total;

  SignalSema(logBuffer.newStringSema);
  WaitSema(logBuffer.drawnSema);
}

// Sets Neutrino version on the splash screen
void uiSplashSetNeutrinoVersion(const char *str) {
  if (!gsGlobal)
    return;

  if (str[0] == '\0')
    return;

  strcpy(logBuffer.neutrinoVersion, "Neutrino");
  strncat(logBuffer.neutrinoVersion, str, 100 - 10);

  SignalSema(logBuffer.newStringSema);
  WaitSema(logBuffer.drawnSema);
}
