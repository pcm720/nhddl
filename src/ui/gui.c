#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "favorites.h"
#include "ftp.h"
#include "neutrino.h"
#include "options.h"
#include "ui/args.h"
#include "ui/coverart.h"
#include "ui/graphics.h"
#include "ui/pad.h"
#include "ui/ui.h"
#include <ctype.h>
#include <dmaKit.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <kernel.h>
#include <libpad.h>
#include <libpwroff.h>
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <debug.h>
#include <stdlib.h>
#include <string.h>

#define DIV_ROUND(n, d) (n + (d - 1)) / d

// Assuming 140x200 cover art
#define COVER_ART_RES_W 140
#define COVER_ART_RES_H 200

void closeUI();
int uiLoop(TargetList *titles);
int uiTitleOptionsLoop(Target *title);
int uiArgumentListLoop(Target *target, ArgumentList *titleArguments);
void drawTitleView(Target **view, int viewTotal, const char *viewName, int selectedIdx, int maxTitlesPerPage, GSTEXTURE *selectedTitleCover);
static int uiSettingsMenu(int hasSelectedTitle);
static void uiControlsScreen();
static int persistVideoMode(const char *configValue);
static int uiDetailPane(TargetList *titles, Target *target);
static int uiSearchScreen();
static int uiFtpSettings();

// Video modes selectable from Settings.
static const struct {
  const char *label;
  const char *configValue; // Value written to nhddl.yaml, NULL = remove the line
  VModeType mode;
} videoModes[] = {
    {"Auto (console default)", NULL, VMODE_NONE},
    {"NTSC (480i)", "ntsc", VMODE_NTSC},
    {"PAL (576i)", "pal", VMODE_PAL},
    {"480p", "480p", VMODE_480P},
    {"576p", "576p", VMODE_576P},
    {"720p (HDMI adapter/component)", "720p", VMODE_720P},
    {"1080i (HDMI adapter/component)", "1080i", VMODE_1080I},
};
#define VIDEO_MODES_TOTAL (sizeof(videoModes) / sizeof(videoModes[0]))

// Title list view modes (cycled with D-pad Left; search uses Triangle)
typedef enum {
  VIEW_ALL = 0,
  VIEW_FAVORITES,
  VIEW_RECENT,
  VIEW_COUNT, // Views cycled by D-pad Left stop here
  VIEW_SEARCH,
} TitleViewMode;
static const char *viewNames[] = {"Title List", "Favorites", "Recently Played", "", "Search Results"};

// Active search query (VIEW_SEARCH)
static char searchQuery[33] = "";

// Case-insensitive substring match (newlib has no strcasestr)
static int nameMatchesQuery(const char *name, const char *query) {
  if (query[0] == '\0')
    return 1;
  int queryLen = strlen(query);
  for (int i = 0; name[i] != '\0'; i++) {
    int j = 0;
    while ((j < queryLen) && (name[i + j] != '\0') && (tolower((unsigned char)name[i + j]) == tolower((unsigned char)query[j])))
      j++;
    if (j == queryLen)
      return 1;
  }
  return 0;
}

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
    if ((mode == VIEW_ALL) || ((mode == VIEW_SEARCH) && nameMatchesQuery(cur->name, searchQuery)) ||
        ((mode == VIEW_FAVORITES) && favoritesIsFavorite(cur)))
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

// Total titles in the library (set once in uiLoop). Used by the status bar;
// a non-zero value means the game server was reachable at startup.
static int libraryTotal = 0;

// Restrained translucent surfaces keep the custom wallpaper visible while
// giving the list, metadata and controls a clear visual hierarchy.
static const uint64_t PanelColor = GS_SETREG_RGBA(0x04, 0x0B, 0x20, 0x58);
static const uint64_t PanelStrongColor = GS_SETREG_RGBA(0x03, 0x08, 0x18, 0x70);
static const uint64_t SelectionPanelColor = GS_SETREG_RGBA(0x00, 0x3A, 0x58, 0x58);
static const uint64_t AccentColor = GS_SETREG_RGBA(0x00, 0x72, 0xA0, 0x80);

// Optional custom background, loaded from <device>/THM/bg.png.
// Palettized (8-bit) PNGs are strongly recommended: they use a fraction of
// the VRAM (fullscreen CT24/CT32 backgrounds wouldn't fit alongside the
// framebuffers in some video modes).
static GSTEXTURE bgTexture;
static int bgLoaded = 0;

// Cover art sprite coordinates
// Initialized during uiInit from screen width and height
static int coverArtX2;
static int coverArtY2;
static int coverArtX1;
static int coverArtY1;

// Recomputed after every video-mode change. The old fixed 40/60-pixel bands
// overlapped scaled text in HD modes and wasted two footer rows in SD.
static int keepoutArea = 20;
static int headerHeight = 44;
static int footerHeight = 38;
static int titleRowHeight = 19;

static int getMaxTitlesPerPage() {
  int available = gsGlobal->Height - headerHeight - footerHeight - (int)(12 * getUIScale());
  int rows = available / titleRowHeight;
  return (rows > 0) ? rows : 1;
}

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

#define POWER_RESET_STACK_SIZE (8 * 1024)
static uint8_t powerResetStack[POWER_RESET_STACK_SIZE] __attribute__((aligned(16)));
static int32_t powerResetSemaID = -1;
static int32_t powerResetThreadID = -1;
static volatile int powerResetRequested = 0;

static void powerResetThread(void *arg) {
  (void)arg;

  for (;;) {
    WaitSema(powerResetSemaID);
    DPRINTF("Power reset: front-panel press detected\n");
    int result = restartDashboardNow();

    // The prepared loader should never return. If both it and the live-load
    // fallback fail, leave a visible error and return to the system browser.
    init_scr();
    scr_printf("\n\n\n  Dashboard power reset failed: %d\n", result);
    Exit(0);
  }
}

static int initPowerResetThread(void) {
  if (powerResetThreadID >= 0)
    return 0;

  ee_sema_t sema;
  sema.init_count = 0;
  sema.max_count = 1;
  sema.option = 0;
  powerResetSemaID = CreateSema(&sema);
  if (powerResetSemaID < 0)
    return powerResetSemaID;

  ee_thread_t thread;
  memset(&thread, 0, sizeof(thread));
  thread.func = powerResetThread;
  thread.stack = powerResetStack;
  thread.stack_size = POWER_RESET_STACK_SIZE;
  thread.gp_reg = &_gp;
  thread.initial_priority = 1;
  powerResetThreadID = CreateThread(&thread);
  if (powerResetThreadID < 0) {
    DeleteSema(powerResetSemaID);
    powerResetSemaID = -1;
    return powerResetThreadID;
  }
  int result = StartThread(powerResetThreadID, NULL);
  if (result < 0) {
    DeleteThread(powerResetThreadID);
    DeleteSema(powerResetSemaID);
    powerResetThreadID = -1;
    powerResetSemaID = -1;
    return result;
  }
  return 0;
}

// poweroff.irx clears the hardware event and forwards one notification here
// from its RPC thread. Keep this callback tiny: the priority-1 worker performs
// the staged dashboard handoff from ordinary EE thread context.
static void powerResetCallback(void *arg) {
  (void)arg;

  if ((powerResetSemaID >= 0) && !powerResetRequested) {
    powerResetRequested = 1;
    SignalSema(powerResetSemaID);
  }
}

int uiInitPowerReset() {
  int result = initPowerResetThread();
  if (result < 0)
    return result;

  // This must run only after poweroff.irx is resident. libpoweroff detects an
  // IOP reboot, rebuilds its RPC callback thread, and disables the module's
  // normal auto-shutdown so a short press becomes our restart event.
  result = poweroffInit();
  if (result < 0)
    return result;
  poweroffSetCallback(powerResetCallback, NULL);
  DPRINTF("Power reset: callback armed\n");
  return 0;
}

static int vsyncHandler(int cause) {
  (void)cause;

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
  // Render 720p through a square-pixel 640x360 canvas. gsKit magnifies it 2x
  // in both axes to the 1280x720 signal, so geometry stays correctly shaped.
  // Two CT16S buffers fit comfortably in GS VRAM and prevent the television
  // from scanning the same buffer that the UI is clearing/redrawing (the
  // cause of the moving blank band and flickering cover top on real hardware).
  case GS_MODE_DTV_720P:
    DPRINTF("Forcing 720p mode\n");
    gsGlobal->Mode = GS_MODE_DTV_720P;
    gsGlobal->Interlace = GS_NONINTERLACED;
    gsGlobal->Field = GS_FRAME;
    gsGlobal->Width = 640;
    gsGlobal->Height = 360;
    gsGlobal->PSM = GS_PSM_CT16S;
    gsGlobal->DoubleBuffering = GS_SETTING_ON;
    gsGlobal->ZBuffering = GS_SETTING_OFF;
    gsGlobal->Dithering = GS_SETTING_ON;
    break;
  case GS_MODE_DTV_1080I:
    DPRINTF("Forcing 1080i mode\n");
    gsGlobal->Mode = GS_MODE_DTV_1080I;
    gsGlobal->Interlace = GS_INTERLACED;
    gsGlobal->Field = GS_FRAME;
    // 960x540 is magnified 2x in both axes by gsKit for a correctly shaped
    // 1920x1080i output. Rendering 640x1080 stretched X by 3x and Y by 1x.
    gsGlobal->Width = 960;
    gsGlobal->Height = 540;
    gsGlobal->PSM = GS_PSM_CT16S;
    gsGlobal->DoubleBuffering = GS_SETTING_OFF;
    gsGlobal->ZBuffering = GS_SETTING_OFF;
    gsGlobal->Dithering = GS_SETTING_ON;
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

  // Establish all scaled metrics before resources and layout are created.
  // 720p's 640x360 canvas is magnified evenly by the GS. A slightly compact
  // logical scale preserves roughly the same physical text size as SD while
  // retaining enough rows for the title list.
  float uiScale = 1.0f;
  if (gsGlobal->Mode == GS_MODE_DTV_720P)
    uiScale = 0.85f;
  else if (gsGlobal->Mode == GS_MODE_DTV_1080I)
    uiScale = 1.25f;
  setUIScale(uiScale);
  keepoutArea = (int)(20 * uiScale);
  headerHeight = getFontLineHeight() * 2 + (int)(8 * uiScale);
  footerHeight = getFontLineHeight() + (int)(16 * uiScale);
  titleRowHeight = getFontLineHeight() + (int)(2 * uiScale);
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
  int powerThreadResult = initPowerResetThread();
  if (powerThreadResult < 0)
    DPRINTF("WARN: failed to start power-reset thread: %d\n", powerThreadResult);

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

  // Init cover art sprite coordinates and async loader.
  // All active canvases now have square logical pixels, so no per-mode aspect
  // compensation is necessary.
  int coverW = (int)(COVER_ART_RES_W * uiScale);
  int coverH = (int)(COVER_ART_RES_H * uiScale);
  int contentTop = headerHeight + (int)(8 * uiScale);
  int contentBottom = gsGlobal->Height - footerHeight - (int)(8 * uiScale);
  int metadataHeight = getFontLineHeightScaled(uiScale * 0.82f) * 2 + (int)(6 * uiScale);
  int coverAreaHeight = contentBottom - contentTop - metadataHeight;
  coverArtX2 = gsGlobal->Width - keepoutArea;
  coverArtY1 = contentTop + ((coverAreaHeight - coverH) / 2);
  if (coverArtY1 < contentTop)
    coverArtY1 = contentTop;
  coverArtY2 = coverArtY1 + coverH;
  coverArtX1 = coverArtX2 - coverW;
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

  int maxTitlesPerPage = getMaxTitlesPerPage();
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

  // Try to load a custom background from the first device that has one.
  // One-time synchronous read: the UI isn't interactive yet at this point.
  if (!bgLoaded) {
    char bgPath[PATH_MAX + 1];
    for (int i = 0; i < MAX_DEVICES; i++) {
      if ((deviceModeMap[i].mode == MODE_NONE) || (deviceModeMap[i].mode == MODE_ALL) || (deviceModeMap[i].mountpoint == NULL))
        continue;
      struct DeviceMapEntry *bgDevice = &deviceModeMap[i];
      if (bgDevice->metadev)
        bgDevice = bgDevice->metadev;
      snprintf(bgPath, sizeof(bgPath), "%s/THM/bg.png", bgDevice->mountpoint);
      memset(&bgTexture, 0, sizeof(GSTEXTURE));
      bgTexture.Delayed = 1;
      if (!gsKit_texture_png(gsGlobal, &bgTexture, bgPath)) {
        // Only accept palettized backgrounds within the logical canvas:
        // a fullscreen truecolor texture cannot fit in the VRAM left over
        // by PAL/576p framebuffers, and gsKit's VRAM allocator loops
        // forever on an allocation that can never be satisfied.
        if (((bgTexture.PSM != GS_PSM_T8) && (bgTexture.PSM != GS_PSM_T4)) || (bgTexture.Width > 640) || (bgTexture.Height > 512)) {
          DPRINTF("Rejecting background %s: use an 8-bit PNG, max 640x512\n", bgPath);
          free(bgTexture.Mem);
          free(bgTexture.Clut);
          memset(&bgTexture, 0, sizeof(GSTEXTURE));
        } else {
          // Keep Mem allocated: the texture is re-bound (and re-uploaded
          // after VRAM evictions) on every frame it is drawn
          bgLoaded = 1;
          DPRINTF("Loaded background from %s\n", bgPath);
          break;
        }
      }
    }
  }

  // Seed the random title picker from the CPU cycle counter
  // (boot timing varies with network/cache state, so this differs per boot)
  uint32_t seed;
  asm volatile("mfc0 %0, $9" : "=r"(seed));
  srand(seed);

  // Record the library size for the status bar (non-zero == server reachable)
  libraryTotal = titles->total;

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
  float scrollAccum = 0.0f; // Fractional analog scroll position
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

    // Analog speed-scroll: the left stick scrolls with speed proportional
    // to deflection, bypassing the digital input repeat throttle below
    if (viewTotal > 0) {
      int stickY = pollStickY();
      if (stickY != 0) {
        // Quadratic response: 48..127 deflection -> ~3..20 titles per second
        scrollAccum += ((float)stickY * (float)((stickY > 0) ? stickY : -stickY)) / 48000.0f;
        while (scrollAccum >= 1.0f) {
          selectedViewIdx = (selectedViewIdx + 1) % viewTotal;
          scrollAccum -= 1.0f;
        }
        while (scrollAccum <= -1.0f) {
          selectedViewIdx = ((selectedViewIdx - 1) + viewTotal) % viewTotal;
          scrollAccum += 1.0f;
        }
      } else
        scrollAccum = 0.0f;
    }

    if (gsGlobal->Mode == GS_MODE_PAL)
      frameCount = (frameCount + 1) % 8; // Handle input only every 8th frame unless it changes
    else
      frameCount = (frameCount + 1) % 10; // Handle input only every 10th frame unless it changes

    if (frameCount && (input == prevInput))
      continue;

    frameCount = 0;
    prevInput = input;

    if ((input & PAD_CROSS) && (viewTotal > 0)) {
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
    } else if ((input & PAD_R2) && (viewTotal > 0)) {
      // Jump to the first title of the next letter group
      char curLetter = toupper((unsigned char)viewList[selectedViewIdx]->name[0]);
      for (int i = 1; i <= viewTotal; i++) {
        int idx = (selectedViewIdx + i) % viewTotal;
        if (toupper((unsigned char)viewList[idx]->name[0]) != curLetter) {
          selectedViewIdx = idx;
          break;
        }
      }
    } else if ((input & PAD_L2) && (viewTotal > 0)) {
      // Jump to the start of the current letter group,
      // or the start of the previous group if already there
      char curLetter = toupper((unsigned char)viewList[selectedViewIdx]->name[0]);
      int idx = selectedViewIdx;
      while ((idx > 0) && (toupper((unsigned char)viewList[idx - 1]->name[0]) == curLetter))
        idx--;
      if ((idx == selectedViewIdx) && (idx > 0)) {
        // Already at the group start: go to the previous group's start
        char prevLetter = toupper((unsigned char)viewList[idx - 1]->name[0]);
        idx--;
        while ((idx > 0) && (toupper((unsigned char)viewList[idx - 1]->name[0]) == prevLetter))
          idx--;
      }
      selectedViewIdx = idx;
    } else if ((input & PAD_SQUARE) && (viewTotal > 0)) {
      // Jump to a random title (press Cross to play it)
      selectedViewIdx = rand() % viewTotal;
    } else if ((input & PAD_CIRCLE) && (viewTotal > 0)) {
      // Toggle favorite for the selected title.
      // Pause cover art IO first: the favorites file write must not run
      // concurrently with the worker's file reads (device access is
      // serialized), or the write can fail silently.
      coverArtPause();
      favoritesToggle(curTarget);
      coverArtResume();
      if (viewMode == VIEW_FAVORITES) {
        // Rebuild the view in case the title was just removed from it
        viewTotal = buildTitleView(titles, viewList, viewMode);
        if (selectedViewIdx >= viewTotal)
          selectedViewIdx = (viewTotal > 0) ? (viewTotal - 1) : 0;
      }
    } else if (input & PAD_LEFT) {
      // Cycle between All -> Favorites -> Recently Played views
      viewMode = (viewMode + 1) % VIEW_COUNT;
      viewTotal = buildTitleView(titles, viewList, viewMode);
      selectedViewIdx = 0;
    } else if (input & PAD_TRIANGLE) {
      // Search: on-screen keyboard filter
      input = -1;    // Wait for fresh input after the keyboard returns
      prevInput = 0;
      if (uiSearchScreen()) {
        viewMode = VIEW_SEARCH;
        viewTotal = buildTitleView(titles, viewList, viewMode);
        selectedViewIdx = 0;
      }
    } else if ((input & PAD_RIGHT) && (viewTotal > 0)) {
      input = -1;    // Wait for fresh input after the pane returns
      prevInput = 0;
      // Pause cover art IO for the pane's info file read
      // (the pane resumes the worker itself before its render loop)
      coverArtPause();
      if (uiDetailPane(titles, curTarget)) {
        // Launch was chosen from the pane
        coverArtPause();
        Target *target = copyTarget(curTarget);
        free(viewList);
        freeTargetList(titles);
        uiLaunchTitle(target, NULL);
        return -1;
      }
    } else if (input & PAD_SELECT) {
      // Settings: title options, video, controls, FTP, and maintenance
      input = -1;    // Wait for fresh input after the picker returns
      prevInput = 0;
      coverArtPause();
      int settingsResult = uiSettingsMenu(viewTotal > 0);
      if (settingsResult == 2) {
        uiFtpSettings(); // Background FTP status; dashboard stays resident
        settingsResult = 0;
      } else if (settingsResult == 3) {
        // Keep uLaunchELF available as a separate recovery/maintenance path.
        // Stop the asynchronous file worker before DEV9/fileXio disappears;
        // otherwise closeUI() can wait forever on an RPC that was reset.
        coverArtPause();
        // DEV9 must be stopped while fileXio is alive. The overlap-safe ELF
        // loader then stages BOOT.ELF before it resets the IOP.
        ftpShutdownNetwork();
        closePad();
        closeUI();
        int execResult = launchExternalELF("mc0:/BOOT/BOOT.ELF");
        init_scr();
        scr_printf("\n\n\n  Failed to launch mc0:/BOOT/BOOT.ELF: %d\n", execResult);
        SleepThread();
      } else if (settingsResult == 4) {
        uiControlsScreen();
        settingsResult = 0;
      } else if (settingsResult == 5) {
        // Per-title launch options now live inside the one Settings menu.
        // The cover-art worker is already paused for the settings flow, so
        // title config reads/writes remain serialized with device access.
        if ((res = uiTitleOptionsLoop(curTarget)) < 0)
          return -1;
        settingsResult = 0;
      }
      if (settingsResult) {
        // Display was reinitialized: recompute the layout
        maxTitlesPerPage = getMaxTitlesPerPage();
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

// Draws a small bordered "button" pill with a label (for buttons that have
// no icon in the atlas). Returns the x coordinate right after the pill.
static int drawButtonPill(int x, int y, const char *label) {
  int w = (int)getLineWidth(label) + 8;
  int h = getFontLineHeight() + 1;
  gsKit_prim_sprite(gsGlobal, x, y, x + w, y + h, 0, HeaderTextColor);
  gsKit_prim_sprite(gsGlobal, x + 1, y + 1, x + w - 1, y + h - 1, 0, BGColor);
  drawTextWindow(x, y, x + w, y + h, 0, FontMainColor, ALIGN_CENTER, label);
  return x + w;
}

// Draws a button pill followed by its action label.
// Returns the x coordinate immediately after the group.
static int drawButtonHint(int x, int y, const char *button, const char *action) {
  x = drawButtonPill(x, y, button) + 4;
  drawText(x, y, 0, 0, 0, HeaderTextColor, action);
  return x + (int)getLineWidth(action);
}

static int getButtonHintWidth(const char *button, const char *action) {
  return (int)getLineWidth(button) + 8 + 4 + (int)getLineWidth(action);
}

// A controller glyph followed by its action label. These helpers keep every
// full-screen menu consistent with the icon-based title/options footers rather
// than spelling button names out as ordinary text.
typedef struct {
  IconType icon;
  const char *action;
} IconHint;

static int getIconHintWidth(const IconHint *hint) {
  return getIconWidth(hint->icon) + 4 + (int)getLineWidth(hint->action);
}

static int drawIconHintAt(int x, int y1, int y2, const IconHint *hint) {
  int iconWidth = getIconWidth(hint->icon);
  drawIconWindow(x, y1, x + iconWidth, y2, 0, FontMainColor,
                 ALIGN_VCENTER | ALIGN_LEFT, hint->icon);
  drawTextWindow(x + iconWidth + 4, y1, 0, y2, 0, HeaderTextColor,
                 ALIGN_VCENTER, hint->action);
  return x + getIconHintWidth(hint);
}

static void drawIconHintRow(int y1, int y2, const IconHint *hints, int count) {
  const int spacing = 14;
  int totalWidth = 0;
  for (int i = 0; i < count; i++)
    totalWidth += getIconHintWidth(&hints[i]);
  if (count > 1)
    totalWidth += spacing * (count - 1);

  gsKit_prim_sprite(gsGlobal, 0, y1, gsGlobal->Width, y2, 0, PanelStrongColor);

  int x = (gsGlobal->Width - totalWidth) / 2;
  for (int i = 0; i < count; i++) {
    x = drawIconHintAt(x, y1, y2, &hints[i]) + spacing;
  }
}

void drawTitleListFooter(int baseX) {
  (void)baseX;

  // One row, ordered by the user's primary workflow. Labels stay identical
  // in every video mode so the footer never changes terminology.
  const int spacing = (getUIScale() > 1.0f) ? 2 : 4;
  const IconHint play = {ICON_CROSS, "Play"};
  const IconHint search = {ICON_TRIANGLE, "Search"};
  const IconHint random = {ICON_SQUARE, "Random"};
  const IconHint favorite = {ICON_CIRCLE, "Fave"};
  const IconHint settings = {ICON_SELECT, "Settings"};

  int totalWidth = getIconHintWidth(&play) + getIconHintWidth(&search) +
                   getIconHintWidth(&random) + getIconHintWidth(&favorite) +
                   getButtonHintWidth("<", "View") +
                   getButtonHintWidth(">", "Info") +
                   getIconHintWidth(&settings) + spacing * 6;

  int y1 = gsGlobal->Height - footerHeight;
  int y2 = gsGlobal->Height;
  int x = (gsGlobal->Width - totalWidth) / 2;
  gsKit_prim_sprite(gsGlobal, 0, y1, gsGlobal->Width, y2, 0, PanelStrongColor);
  int pillY = y1 + ((footerHeight - getFontLineHeight() - 1) / 2);
  x = drawIconHintAt(x, y1, y2, &play) + spacing;
  x = drawIconHintAt(x, y1, y2, &search) + spacing;
  x = drawIconHintAt(x, y1, y2, &random) + spacing;
  x = drawIconHintAt(x, y1, y2, &favorite) + spacing;
  x = drawButtonHint(x, pillY, "<", "View") + spacing;
  x = drawButtonHint(x, pillY, ">", "Info") + spacing;
  drawIconHintAt(x, y1, y2, &settings);
}

// Copies text into out and adds an ellipsis when it cannot fit in maxWidth.
// This is deliberate truncation, not a draw-time clip, so no glyph can bleed
// beneath the cover card and the user still gets a visible continuation cue.
static void ellipsizeText(const char *text, int maxWidth, float scale, char *out, int outSize) {
  if (outSize <= 0)
    return;

  snprintf(out, outSize, "%s", text);
  if (getLineWidthScaled(out, scale) <= maxWidth)
    return;

  const char *ellipsis = "...";
  int len = strlen(out);
  int ellipsisWidth = (int)getLineWidthScaled(ellipsis, scale);
  while ((len > 0) && ((getLineWidthScaled(out, scale) + ellipsisWidth) > maxWidth))
    out[--len] = '\0';

  if (len + 3 < outSize)
    strcat(out, ellipsis);
}

// Draws the title list for the active view
void drawTitleView(Target **view, int viewTotal, const char *viewName, int selectedIdx, int maxTitlesPerPage, GSTEXTURE *selectedTitleCover) {
  int curPage = (viewTotal > 0) ? (selectedIdx / maxTitlesPerPage) : 0;
  float scale = getUIScale();
  float metaScale = scale * 0.82f;
  float headingScale = scale * 1.10f;
  int baseX = keepoutArea;
  int gap = (int)(12 * scale);
  int coverPad = (int)(10 * scale);
  int contentTop = headerHeight + (int)(6 * scale);
  int contentBottom = gsGlobal->Height - footerHeight - (int)(6 * scale);
  int listRight = coverArtX1 - coverPad - gap;
  int coverPanelLeft = coverArtX1 - coverPad;
  int coverPanelRight = coverArtX2 + coverPad;
  int coverPanelBottom = coverArtY2 + coverPad;
  if (coverPanelBottom > contentBottom)
    coverPanelBottom = contentBottom;

  // Draw the custom background first (dimmed so text stays readable).
  // Alpha blending is disabled for the same reason as cover art: PNG alpha
  // is stored inverted, and the background has nothing to blend with anyway.
  if (bgLoaded) {
    gsKit_TexManager_bind(gsGlobal, &bgTexture);
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_prim_sprite_texture(gsGlobal, &bgTexture, 0, 0, 0.0f, 0.0f, gsGlobal->Width, gsGlobal->Height, bgTexture.Width, bgTexture.Height, 0,
                              GS_SETREG_RGBA(0x38, 0x38, 0x38, 0x80));
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  }

  // Structured surfaces: a compact header, a dedicated list column, a cover
  // card, and one footer row. They also provide hard visual boundaries for
  // the clipping rules below.
  gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, headerHeight, 0, PanelStrongColor);
  gsKit_prim_sprite(gsGlobal, 0, headerHeight - 1, gsGlobal->Width, headerHeight, 0, AccentColor);
  gsKit_prim_sprite(gsGlobal, baseX - (int)(8 * scale), contentTop - (int)(4 * scale),
                    listRight + (int)(6 * scale), contentBottom, 0, PanelColor);
  gsKit_prim_sprite(gsGlobal, coverPanelLeft, coverArtY1 - coverPad,
                    coverPanelRight, coverPanelBottom, 0, PanelColor);

  // Shared-network status: UDPFS and FTP use the same PS2IP/SMAP interface
  // while the dashboard is open.
  int netOnline = (libraryTotal > 0);
  uint64_t netColor = netOnline ? GS_SETREG_RGBA(0x40, 0xC0, 0x40, 0x80) : GS_SETREG_RGBA(0xC0, 0x40, 0x40, 0x80);
  snprintf(lineBuffer, 255, "%s %s | FTP %s",
           (LAUNCHER_OPTIONS.udpfsIp[0] != '\0') ? LAUNCHER_OPTIONS.udpfsIp : "no IP",
           netOnline ? "online" : "offline",
           ftpIsBackgroundRunning() ? "on" : "off");
  int statusDot = (int)(6 * scale);
  int leftHeaderEnd = gsGlobal->Width * 42 / 100;
  int rightHeaderStart = gsGlobal->Width * 70 / 100;
  gsKit_prim_sprite(gsGlobal, baseX, (headerHeight - statusDot) / 2,
                    baseX + statusDot, (headerHeight + statusDot) / 2, 1, netColor);
  drawTextWindowScaled(baseX + statusDot + (int)(7 * scale), 0, leftHeaderEnd,
                       headerHeight, 1, FontMainColor, ALIGN_VCENTER, lineBuffer, metaScale);

  drawTextWindowScaled(leftHeaderEnd, 0, rightHeaderStart, headerHeight, 1,
                       ColorSelected, ALIGN_CENTER, viewName, headingScale);
  if (viewTotal > 0) {
    snprintf(lineBuffer, 255, "%d/%d pages  |  %d/%d titles",
             curPage + 1, DIV_ROUND(viewTotal, maxTitlesPerPage), selectedIdx + 1, viewTotal);
    drawTextWindowScaled(rightHeaderStart, 0, gsGlobal->Width - baseX, headerHeight, 1,
                         HeaderTextColor, ALIGN_VCENTER | ALIGN_RIGHT, lineBuffer, metaScale);
  }

  drawTitleListFooter(baseX);

  if (viewTotal == 0) {
    // Empty view: favorites/recents have no entries yet
    drawTextWindow(0, 0, gsGlobal->Width, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER,
                   "Nothing here yet\nAdd a title to Favorites from the All view");
    return;
  }

  // Draw title list. Every display string is measured and ellipsized before
  // rendering, so even an unusually long ISO name cannot enter the cover card.
  int pageEnd = (curPage + 1) * maxTitlesPerPage;
  if (pageEnd > viewTotal)
    pageEnd = viewTotal;

  int titleY = contentTop;
  int textX = baseX + (int)(8 * scale);
  int textWidth = listRight - textX - (int)(8 * scale);
  char rawTitle[255];
  char displayTitle[255];
  for (int i = curPage * maxTitlesPerPage; i < pageEnd; i++) {
    Target *curTitle = view[i];

    if (favoritesIsFavorite(curTitle))
      snprintf(rawTitle, sizeof(rawTitle), "* %s", curTitle->name);
    else
      snprintf(rawTitle, sizeof(rawTitle), "%s", curTitle->name);
    ellipsizeText(rawTitle, textWidth, scale, displayTitle, sizeof(displayTitle));

    if (i == selectedIdx) {
      gsKit_prim_sprite(gsGlobal, baseX - (int)(2 * scale), titleY - 1,
                        listRight, titleY + titleRowHeight - 1, 1, SelectionPanelColor);
      gsKit_prim_sprite(gsGlobal, baseX - (int)(2 * scale), titleY - 1,
                        baseX + (int)(2 * scale), titleY + titleRowHeight - 1, 2, AccentColor);
    }

    drawText(textX, titleY, 3, listRight - (int)(4 * scale), 0,
             (i == selectedIdx) ? ColorSelected : FontMainColor, displayTitle);
    titleY += titleRowHeight;
  }

  // Draw cover art placeholder/frame
  gsKit_prim_sprite(gsGlobal, coverArtX1 - 2, coverArtY1 - 2, coverArtX2 + 2, coverArtY2 + 2, 1, AccentColor);

  // Draw cover art if it exists
  if (selectedTitleCover != NULL) {
    // Temporaily disable alpha blending
    // Some PNGs require inverted alpha channel value to display properly
    // Since cover art has nothing to blend, we can bypass the issue altogether
    // Text and controller glyphs bind other textures after coverArtGet(). Bind
    // the selected cover again at the point of use so an eviction can never
    // leave this sprite reading a stale GS VRAM address.
    gsKit_TexManager_bind(gsGlobal, selectedTitleCover);
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

// Word-wraps text into out (inserting newlines) so each line fits maxWidth.
// Existing newlines are preserved.
static void wrapText(const char *text, int maxWidth, char *out, int outSize) {
  char word[128];
  char measure[256];
  int outLen = 0;
  int lineLen = 0; // Characters on the current output line
  int i = 0;

  while ((text[i] != '\0') && (outLen < outSize - 2)) {
    if (text[i] == '\n') {
      out[outLen++] = '\n';
      lineLen = 0;
      i++;
      continue;
    }
    if (text[i] == ' ') {
      i++;
      continue;
    }
    // Collect the next word
    int wordLen = 0;
    while ((text[i] != '\0') && (text[i] != ' ') && (text[i] != '\n') && (wordLen < 127))
      word[wordLen++] = text[i++];
    word[wordLen] = '\0';

    // Measure the current line plus this word
    int prefixLen = (lineLen < 200) ? lineLen : 200;
    memcpy(measure, &out[outLen - lineLen], prefixLen);
    measure[prefixLen] = '\0';
    if (lineLen > 0)
      strcat(measure, " ");
    strncat(measure, word, sizeof(measure) - strlen(measure) - 1);

    if ((lineLen > 0) && (getLineWidth(measure) > maxWidth)) {
      // Start a new line
      out[outLen++] = '\n';
      lineLen = 0;
    } else if (lineLen > 0) {
      out[outLen++] = ' ';
      lineLen++;
    }
    for (int j = 0; (j < wordLen) && (outLen < outSize - 2); j++) {
      out[outLen++] = word[j];
      lineLen++;
    }
  }
  out[outLen] = '\0';
}

// Per-title detail pane: shows the cover bigger plus description text from
// ART/<ID>_INFO.txt on the metadata device. Returns 1 if the user chose to
// launch the title, 0 to go back to the list.
static int uiDetailPane(TargetList *titles, Target *target) {
  static char infoRaw[1536];
  static char infoWrapped[2048];

  // Read the info file (worker is paused by the caller during this read)
  struct DeviceMapEntry *device = target->device;
  if (device->metadev)
    device = device->metadev;
  snprintf(lineBuffer, 255, "%s/ART/%s_INFO.txt", device->mountpoint, target->id);
  int infoLen = 0;
  FILE *file = fopen(lineBuffer, "rb");
  if (file != NULL) {
    infoLen = fread(infoRaw, 1, sizeof(infoRaw) - 1, file);
    if (infoLen < 0)
      infoLen = 0;
    fclose(file);
  }
  infoRaw[infoLen] = '\0';
  if (infoLen == 0)
    snprintf(infoRaw, sizeof(infoRaw), "No description available.\n\nAdd one on the server:\nART/%s_INFO.txt", target->id);

  // Resume async cover loads for the pane render loop (no more main-thread
  // file IO happens until the pane exits)
  coverArtResume();

  // Layout: enlarged cover on the left, wrapped text on the right
  int baseX = keepoutArea + 10;
  int coverH = (int)((coverArtY2 - coverArtY1) * 1.25f);
  int coverW = (int)((coverArtX2 - coverArtX1) * 1.25f);
  int coverY1 = headerHeight + getFontLineHeight();
  int textX = baseX + coverW + 15;
  int textW = gsGlobal->Width - keepoutArea - 10 - textX;
  wrapText(infoRaw, textW, infoWrapped, sizeof(infoWrapped));

  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);

    // Header: title name + ID
    snprintf(lineBuffer, 255, "%s\n%s", target->name, target->id);
    drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);

    // Cover (async; placeholder frame while loading)
    GSTEXTURE *cover = coverArtGet(titles, target);
    gsKit_prim_sprite(gsGlobal, baseX - 2, coverY1 - 2, baseX + coverW + 2, coverY1 + coverH + 2, 1, FontMainColor);
    if (cover != NULL) {
      gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
      gsKit_prim_sprite_texture(gsGlobal, cover, baseX, coverY1, 0.0f, 0.0f, baseX + coverW, coverY1 + coverH, cover->Width, cover->Height, 2,
                                FontMainColor);
      gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
    } else {
      gsKit_prim_sprite(gsGlobal, baseX, coverY1, baseX + coverW, coverY1 + coverH, 1, BGColor);
      drawTextWindow(baseX, coverY1, baseX + coverW, coverY1 + coverH, 1, FontMainColor, ALIGN_CENTER, coverArtStatusText());
    }

    // Description text
    drawText(textX, coverY1, 0, 0, gsGlobal->Height - footerHeight - coverY1, FontMainColor, infoWrapped);

    const IconHint detailHints[] = {
        {ICON_CROSS, "Play"},
        {ICON_CIRCLE, "Back"},
    };
    drawIconHintRow(gsGlobal->Height - footerHeight, gsGlobal->Height - 1,
                    detailHints, sizeof(detailHints) / sizeof(detailHints[0]));

    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    uiSyncFlip();

    int input = waitForInput(-1);
    if (input & PAD_CROSS)
      return 1;
    if (input & (PAD_CIRCLE | PAD_LEFT))
      return 0;
  }
}

// On-screen keyboard for the search filter (opened with Triangle).
// Returns 1 if a non-empty query was confirmed, 0 if cancelled.
static int uiSearchScreen() {
  static const char *kbRows[] = {"ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789 .-'"};
  const int rowCount = 4;
  int curRow = 0, curCol = 0;
  int baseX = keepoutArea + 10;

  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);

    drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_HCENTER, "Search");
    snprintf(lineBuffer, 255, "Search: %s_", searchQuery);
    int y = drawText(baseX, headerHeight + getFontLineHeight(), 0, 0, 0, FontMainColor, lineBuffer);
    y += getFontLineHeight();

    // Draw the keyboard grid
    int cellW = getLineWidth("W") + 14;
    for (int r = 0; r < rowCount; r++) {
      int x = baseX + 10;
      for (int c = 0; kbRows[r][c] != '\0'; c++) {
        char key[4];
        // Show space visibly
        if (kbRows[r][c] == ' ')
          snprintf(key, sizeof(key), "sp");
        else
          snprintf(key, sizeof(key), "%c", kbRows[r][c]);
        drawText(x, y, 0, 0, 0, (((r == curRow) && (c == curCol)) ? ColorSelected : FontMainColor), key);
        x += cellW;
      }
      y += getFontLineHeight() + 4;
    }

    const IconHint searchHints[] = {
        {ICON_CROSS, "Type"},
        {ICON_SQUARE, "Delete"},
        {ICON_START, "Search"},
        {ICON_CIRCLE, "Cancel"},
    };
    drawIconHintRow(gsGlobal->Height - footerHeight, gsGlobal->Height - 1,
                    searchHints, sizeof(searchHints) / sizeof(searchHints[0]));

    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    uiSyncFlip();

    int input = waitForInput(-1);
    int rowLen = strlen(kbRows[curRow]);
    if (input & PAD_UP) {
      curRow = (curRow - 1 + rowCount) % rowCount;
    } else if (input & PAD_DOWN) {
      curRow = (curRow + 1) % rowCount;
    } else if (input & PAD_LEFT) {
      curCol = (curCol - 1 + rowLen) % rowLen;
    } else if (input & PAD_RIGHT) {
      curCol = (curCol + 1) % rowLen;
    } else if (input & PAD_CROSS) {
      int len = strlen(searchQuery);
      if (len < (int)(sizeof(searchQuery) - 1)) {
        searchQuery[len] = kbRows[curRow][curCol];
        searchQuery[len + 1] = '\0';
      }
    } else if (input & PAD_SQUARE) {
      int len = strlen(searchQuery);
      if (len > 0)
        searchQuery[len - 1] = '\0';
    } else if (input & PAD_START) {
      if (searchQuery[0] != '\0')
        return 1;
    } else if (input & PAD_CIRCLE) {
      searchQuery[0] = '\0';
      return 0;
    }
    // Clamp the column when moving between rows of different lengths
    rowLen = strlen(kbRows[curRow]);
    if (curCol >= rowLen)
      curCol = rowLen - 1;
  }
}

// Background FTP status. UDPFS and ps2ftpd share one PS2IP stack, so leaving
// this screen returns directly to the live game browser; no IOP reset or ELF
// handoff is involved.
static int uiFtpSettings() {
  int baseX = keepoutArea + 10;

  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);

    drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_HCENTER, "Memory Card FTP");
    if (ftpIsBackgroundRunning()) {
      const char *ip = ftpGetBackgroundIP();
      if (ip[0] == '\0')
        ip = LAUNCHER_OPTIONS.udpfsIp;
      snprintf(lineBuffer, 255,
               "FTP server is running automatically\n\n"
               "ftp://%s\nMemory card: /mc/0/\n\n"
               "UDPFS game browsing and FTP are sharing the network.",
               ip);
    } else {
      snprintf(lineBuffer, 255,
               "Background FTP is not running (error %d)\n\n"
               "%s\n\n"
               "Game browsing remains available. Card Maintenance still\n"
               "provides the separate uLaunchELF recovery path.",
               ftpGetBackgroundError(), ftpGetLastDiagnostic());
    }
    drawTextWindow(baseX, headerHeight, gsGlobal->Width - baseX, gsGlobal->Height - footerHeight, 0,
                   ftpIsBackgroundRunning() ? FontMainColor : ErrorTextColor,
                   ALIGN_CENTER, lineBuffer);

    const IconHint backHint[] = {{ICON_CIRCLE, "Back to games"}};
    drawIconHintRow(gsGlobal->Height - footerHeight, gsGlobal->Height - 1,
                    backHint, sizeof(backHint) / sizeof(backHint[0]));
    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    uiSyncFlip();

    int input = waitForInput(-1);
    if (input & PAD_CIRCLE)
      return 0;
  }
}

// Writes (or removes, when configValue is NULL) the video option in the exact
// nhddl.yaml chosen by loadOptions(). This avoids accidentally writing a
// similarly named file on UDPFS while the dashboard boots from memory card.
static int persistVideoMode(const char *configValue) {
  char yamlPath[PATH_MAX + 1];
  char tempPath[PATH_MAX + 8];
  char line[512];
  size_t capacity = 1024;
  size_t len = 0;
  char *contents = malloc(capacity);
  if (contents == NULL)
    return -ENOMEM;

  if (OPTIONS_FILE_PATH[0] != '\0') {
    strlcpy(yamlPath, OPTIONS_FILE_PATH, sizeof(yamlPath));
  } else if (SELF_ELF_PATH[0] != '\0') {
    strlcpy(yamlPath, SELF_ELF_PATH, sizeof(yamlPath));
    char *slash = strrchr(yamlPath, '/');
    if (slash == NULL) {
      free(contents);
      return -ENOENT;
    }
    strlcpy(slash + 1, "nhddl.yaml", sizeof(yamlPath) - (slash + 1 - yamlPath));
  } else {
    free(contents);
    return -ENOENT;
  }

  FILE *file = fopen(yamlPath, "rb");
  if (file != NULL) {
    while (fgets(line, sizeof(line), file) != NULL) {
      char *key = line;
      while ((*key == ' ') || (*key == '\t'))
        key++;
      if (!strncmp(key, "video:", 6))
        continue;

      size_t lineLen = strlen(line);
      if (len + lineLen + 32 > capacity) {
        size_t newCapacity = capacity * 2;
        while (len + lineLen + 32 > newCapacity)
          newCapacity *= 2;
        char *larger = realloc(contents, newCapacity);
        if (larger == NULL) {
          fclose(file);
          free(contents);
          return -ENOMEM;
        }
        contents = larger;
        capacity = newCapacity;
      }
      memcpy(contents + len, line, lineLen);
      len += lineLen;
    }
    fclose(file);
  }

  if ((len > 0) && (contents[len - 1] != '\n'))
    contents[len++] = '\n';
  if (configValue != NULL)
    len += snprintf(contents + len, capacity - len, "video: %s\n", configValue);

  snprintf(tempPath, sizeof(tempPath), "%s.tmp", yamlPath);
  file = fopen(tempPath, "wb");
  if (file == NULL) {
    free(contents);
    return -EIO;
  }
  size_t written = fwrite(contents, 1, len, file);
  int closeResult = fclose(file);
  if ((written != len) || (closeResult != 0)) {
    remove(tempPath);
    free(contents);
    return -EIO;
  }

  // Prefer a single rename. Some PS2 filesystems cannot replace an existing
  // file, so fall back to a full direct rewrite without deleting the target.
  if (rename(tempPath, yamlPath) != 0) {
    file = fopen(yamlPath, "wb");
    if (file == NULL) {
      remove(tempPath);
      free(contents);
      return -EIO;
    }
    written = fwrite(contents, 1, len, file);
    closeResult = fclose(file);
    remove(tempPath);
    if ((written != len) || (closeResult != 0)) {
      free(contents);
      return -EIO;
    }
  }

  strlcpy(OPTIONS_FILE_PATH, yamlPath, sizeof(OPTIONS_FILE_PATH));
  free(contents);
  DPRINTF("Saved video mode to %s\n", yamlPath);
  return 0;
}

static int drawControlIconLine(int x, int y, IconType icon, const char *action) {
  IconHint hint = {icon, action};
  drawIconHintAt(x, y, y + getFontLineHeight() + 8, &hint);
  return y + getFontLineHeight() + 8;
}

static int drawControlIconPairLine(int x, int y, IconType first, IconType second, const char *action) {
  int rowHeight = getFontLineHeight() + 8;
  int firstWidth = getIconWidth(first);
  int secondWidth = getIconWidth(second);
  drawIconWindow(x, y, x + firstWidth, y + rowHeight, 0, FontMainColor,
                 ALIGN_VCENTER | ALIGN_LEFT, first);
  x += firstWidth + 3;
  drawIconWindow(x, y, x + secondWidth, y + rowHeight, 0, FontMainColor,
                 ALIGN_VCENTER | ALIGN_LEFT, second);
  x += secondWidth + 5;
  drawTextWindow(x, y, 0, y + rowHeight, 0, HeaderTextColor,
                 ALIGN_VCENTER, action);
  return y + rowHeight;
}

static int drawControlPillLine(int x, int y, const char *button, const char *action) {
  int rowHeight = getFontLineHeight() + 8;
  int pillY = y + ((rowHeight - getFontLineHeight() - 1) / 2);
  drawButtonHint(x, pillY, button, action);
  return y + rowHeight;
}

// Discoverability page for bindings deliberately omitted from the compact
// title-list footer. It uses the same controller glyphs and pills as the rest
// of the dashboard, so prompts do not fall back to plain button-name text.
static void uiControlsScreen() {
  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);

    int margin = keepoutArea;
    int gap = (int)(12 * getUIScale());
    int columnWidth = (gsGlobal->Width - margin * 2 - gap) / 2;
    int leftX = margin + (int)(8 * getUIScale());
    int rightX = margin + columnWidth + gap + (int)(8 * getUIScale());
    int panelTop = headerHeight + (int)(8 * getUIScale());
    int panelBottom = gsGlobal->Height - footerHeight - (int)(8 * getUIScale());

    gsKit_prim_sprite(gsGlobal, 0, 0, gsGlobal->Width, headerHeight, 0, PanelStrongColor);
    gsKit_prim_sprite(gsGlobal, 0, headerHeight - 1, gsGlobal->Width, headerHeight, 1, AccentColor);
    drawTextWindow(0, 0, gsGlobal->Width, headerHeight, 1, ColorSelected,
                   ALIGN_CENTER, "Controls & shortcuts");
    gsKit_prim_sprite(gsGlobal, margin, panelTop, margin + columnWidth,
                      panelBottom, 0, PanelColor);
    gsKit_prim_sprite(gsGlobal, margin + columnWidth + gap, panelTop,
                      gsGlobal->Width - margin, panelBottom, 0, PanelColor);

    int yLeft = panelTop + (int)(8 * getUIScale());
    int yRight = yLeft;
    yLeft = drawText(leftX, yLeft, 1, 0, 0, ColorSelected, "Navigation");
    yLeft += (int)(3 * getUIScale());
    yLeft = drawControlPillLine(leftX, yLeft, "UP / DOWN", "Browse titles");
    yLeft = drawControlPillLine(leftX, yLeft, "LEFT", "Switch view");
    yLeft = drawControlPillLine(leftX, yLeft, "RIGHT", "Game info");
    yLeft = drawControlIconPairLine(leftX, yLeft, ICON_L1, ICON_R1,
                                    "Previous / next page");
    yLeft = drawControlPillLine(leftX, yLeft, "L2 / R2", "Previous / next letter");
    drawControlPillLine(leftX, yLeft, "LEFT STICK", "Fast scroll");

    yRight = drawText(rightX, yRight, 1, 0, 0, ColorSelected, "Actions");
    yRight += (int)(3 * getUIScale());
    yRight = drawControlIconLine(rightX, yRight, ICON_CROSS, "Play title");
    yRight = drawControlIconLine(rightX, yRight, ICON_TRIANGLE, "Search titles");
    yRight = drawControlIconLine(rightX, yRight, ICON_SQUARE, "Random title");
    yRight = drawControlIconLine(rightX, yRight, ICON_CIRCLE, "Toggle favorite");
    yRight = drawControlIconLine(rightX, yRight, ICON_SELECT, "Settings + title options");
    drawControlIconLine(rightX, yRight, ICON_START, "Exit dashboard");

    const IconHint backHint[] = {{ICON_CIRCLE, "Back"}};
    drawIconHintRow(gsGlobal->Height - footerHeight, gsGlobal->Height - 1,
                    backHint, sizeof(backHint) / sizeof(backHint[0]));

    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    uiSyncFlip();

    int input = waitForInput(-1);
    if (input & PAD_CIRCLE)
      return;
  }
}

// Unified Settings screen (opened with Select from the title list): selected
// title options, video mode, controls reference, embedded FTP, and uLaunchELF
// maintenance. Chosen video modes apply immediately, then auto-revert unless
// confirmed within ~12 seconds so picking a mode the display can't show never
// strands the UI.
// Returns 1 if the display was reinitialized (caller must recompute layout),
// 2 for FTP settings, 3 for uLaunchELF card maintenance, 4 for controls, or 5
// for the selected title's launch options.
static int uiSettingsMenu(int hasSelectedTitle) {
  // Menu = optional title options + video modes + controls + FTP + maintenance.
  const int videoItemBase = hasSelectedTitle ? 1 : 0;
  const int titleOptionsItem = 0;
  const int controlsItem = videoItemBase + (int)VIDEO_MODES_TOTAL;
  const int ftpItem = controlsItem + 1;
  const int cardItem = ftpItem + 1;
  const int itemsTotal = cardItem + 1;
  int selected = hasSelectedTitle ? titleOptionsItem : videoItemBase;
  int reinited = 0;
  int baseX = keepoutArea + 10;

  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);

    drawTextWindow(baseX, headerHeight - getFontLineHeight(), gsGlobal->Width - baseX, 0, 0, HeaderTextColor, ALIGN_HCENTER, "Settings");
    int y = headerHeight + 2 * getFontLineHeight();
    if (hasSelectedTitle) {
      y = drawText(baseX, y, 0, 0, 0,
                   ((selected == titleOptionsItem) ? ColorSelected : FontMainColor),
                   "Selected title options");
      y += getFontLineHeight() / 2;
    }
    drawText(baseX, y, 0, 0, 0, HeaderTextColor, "Video mode:");
    y += getFontLineHeight();
    for (int i = 0; i < (int)VIDEO_MODES_TOTAL; i++) {
      snprintf(lineBuffer, 255, "%s%s", videoModes[i].label, ((videoModes[i].mode == LAUNCHER_OPTIONS.vmode) ? "  (current)" : ""));
      y = drawText(baseX + 20, y, 0, 0, 0,
                   (((videoItemBase + i) == selected) ? ColorSelected : FontMainColor),
                   lineBuffer);
    }
    y += getFontLineHeight() / 2;
    y = drawText(baseX, y, 0, 0, 0, ((selected == controlsItem) ? ColorSelected : FontMainColor), "Controls & shortcuts");
    snprintf(lineBuffer, 255, "FTP server (automatic: %s)", ftpIsBackgroundRunning() ? "running" : "failed");
    y = drawText(baseX, y, 0, 0, 0, ((selected == ftpItem) ? ColorSelected : FontMainColor), lineBuffer);
    y = drawText(baseX, y, 0, 0, 0, ((selected == cardItem) ? ColorSelected : FontMainColor), "Card Maintenance (uLaunchELF)");
    drawText(baseX, y + getFontLineHeight() / 2, 0, 0, 0, HeaderTextColor,
             "Video changes auto-revert unless confirmed.");
    const IconHint settingsHints[] = {
        {ICON_CROSS, "Select"},
        {ICON_CIRCLE, "Back"},
    };
    drawIconHintRow(gsGlobal->Height - footerHeight, gsGlobal->Height - 1,
                    settingsHints, sizeof(settingsHints) / sizeof(settingsHints[0]));

    gsKit_queue_exec(gsGlobal);
    gsKit_finish();
    uiSyncFlip();

    int input = waitForInput(-1);
    if (input & PAD_UP) {
      selected = (selected - 1 + itemsTotal) % itemsTotal;
    } else if (input & PAD_DOWN) {
      selected = (selected + 1) % itemsTotal;
    } else if (input & PAD_CIRCLE) {
      return reinited;
    } else if (input & PAD_CROSS) {
      if (hasSelectedTitle && (selected == titleOptionsItem))
        return 5;
      if (selected == controlsItem)
        return 4;
      if (selected == ftpItem)
        return 2;
      if (selected == cardItem)
        return 3;

      const int videoIndex = selected - videoItemBase;
      if ((videoIndex < 0) || (videoIndex >= (int)VIDEO_MODES_TOTAL))
        continue;
      if (videoModes[videoIndex].mode == LAUNCHER_OPTIONS.vmode)
        continue; // Already active

      // Apply the new mode immediately
      VModeType prevMode = LAUNCHER_OPTIONS.vmode;
      LAUNCHER_OPTIONS.vmode = videoModes[videoIndex].mode;
      uiInit();
      reinited = 1;

      // Confirmation countdown with auto-revert
      const int totalFrames = 12 * 60; // ~12 seconds (~10 on PAL)
      int confirmed = 0;
      for (int frames = totalFrames; frames > 0; frames--) {
        gsKit_clear(gsGlobal, BGColor);
        gsKit_TexManager_nextFrame(gsGlobal);
        snprintf(lineBuffer, 255, "%s", videoModes[videoIndex].label);
        drawTextWindow(0, gsGlobal->Height / 2 - 3 * getFontLineHeight(),
                       gsGlobal->Width, gsGlobal->Height / 2 - getFontLineHeight(),
                       0, FontMainColor, ALIGN_CENTER, lineBuffer);
        const IconHint keepHint[] = {{ICON_CROSS, "Keep this mode"}};
        drawIconHintRow(gsGlobal->Height / 2 - getFontLineHeight(),
                        gsGlobal->Height / 2 + getFontLineHeight(),
                        keepHint, sizeof(keepHint) / sizeof(keepHint[0]));
        snprintf(lineBuffer, 255, "Reverting in %d...", (frames / 60) + 1);
        drawTextWindow(0, gsGlobal->Height / 2 + getFontLineHeight(),
                       gsGlobal->Width, gsGlobal->Height / 2 + 3 * getFontLineHeight(),
                       0, FontMainColor, ALIGN_CENTER, lineBuffer);
        gsKit_queue_exec(gsGlobal);
        gsKit_finish();
        uiSyncFlip();

        int cInput = pollInput();
        // Ignore inputs for the first half second (the button press that
        // applied the mode may still be held down)
        if (frames > (totalFrames - 30))
          continue;
        if (cInput & PAD_CROSS) {
          confirmed = 1;
          break;
        }
        if (cInput & PAD_CIRCLE)
          break; // Revert immediately
      }

      if (confirmed) {
        int saveResult = persistVideoMode(videoModes[videoIndex].configValue);
        if (saveResult < 0) {
          while (1) {
            gsKit_clear(gsGlobal, BGColor);
            snprintf(lineBuffer, 255, "Video mode is active, but the setting could not be saved (%d).", saveResult);
            drawTextWindow(keepoutArea, headerHeight, gsGlobal->Width - keepoutArea,
                           gsGlobal->Height - footerHeight, 0, ErrorTextColor,
                           ALIGN_CENTER, lineBuffer);
            const IconHint backHint[] = {{ICON_CIRCLE, "Back"}};
            drawIconHintRow(gsGlobal->Height - footerHeight, gsGlobal->Height - 1,
                            backHint, sizeof(backHint) / sizeof(backHint[0]));
            gsKit_queue_exec(gsGlobal);
            gsKit_finish();
            uiSyncFlip();
            if (waitForInput(-1) & PAD_CIRCLE)
              break;
          }
        }
        return 1;
      }
      // Not confirmed: revert to the previous mode
      LAUNCHER_OPTIONS.vmode = prevMode;
      uiInit();
    }
  }
}

void drawTitleOptionsFooter(int baseX) {
  drawIconWindow(baseX, gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CROSS);
  drawTextWindow(baseX + 5 + getIconWidth(ICON_CROSS), gsGlobal->Height - 1 - footerHeight, 0, gsGlobal->Height, 0,
                 HeaderTextColor, ALIGN_VCENTER, "Toggle");

  drawIconWindow((gsGlobal->Width * 3 / 8) - getIconWidth(ICON_SQUARE), gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_SQUARE);
  drawTextWindow((gsGlobal->Width * 3 / 8) + 5, gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER,
                 "Test");

  drawIconWindow((gsGlobal->Width * 5 / 8), gsGlobal->Height - footerHeight, gsGlobal->Width - getLineWidth("Save") - 5, gsGlobal->Height, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_START);
  drawTextWindow((gsGlobal->Width * 5 / 8) + 5 + getIconWidth(ICON_START), gsGlobal->Height - 1 - footerHeight, gsGlobal->Width, gsGlobal->Height, 0,
                 HeaderTextColor, ALIGN_VCENTER, "Save");

  drawIconWindow(gsGlobal->Width - baseX - 5 - getIconWidth(ICON_CIRCLE) - getLineWidth("Back"), gsGlobal->Height - footerHeight,
                 gsGlobal->Width - baseX, gsGlobal->Height, 0, FontMainColor, ALIGN_VCENTER | ALIGN_LEFT, ICON_CIRCLE);
  drawTextWindow(0, gsGlobal->Height - 1 - footerHeight, gsGlobal->Width - baseX, gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER | ALIGN_RIGHT,
                 "Back");

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
    } else if (input & PAD_CIRCLE) {
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
    } else if (input & PAD_CIRCLE) {
      return 0;
    }

    // Ignore inputs when the argument is not initialized
    if (!curArgument)
      continue;

    if (input & PAD_CROSS) {
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
