#include "gui.h"
#include "common.h"
#include "launcher.h"
#include "options.h"
#include "pad.h"
#include <dmaKit.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <libpad.h>
#include <malloc.h>
#include <ps2sdkapi.h>
#include <stdio.h>

#define MAX_TITLES_PER_PAGE_NTSC 20
#define MAX_TITLES_PER_PAGE_PAL 25
#define MAX_ARGUMENTS 12
#define DIV_ROUND(n, d) (n + (d - 1)) / d

const char artPath[] = "/ART";

void init480p(GSGLOBAL *gsGlobal);
int uiLoop(struct TargetList *titles);
int uiTitleOptionsLoop(struct Target *title);
void uiLaunchTitle(struct Target *target, struct ArgumentList *arguments);
void drawTitleList(struct TargetList *titles, int selectedTitleIdx, GSTEXTURE *selectedTitleCover);
void drawArgumentList(struct ArgumentList *arguments, uint8_t compatModes, int selectedArgIdx);
void drawGameID(const char *game_id);

static GSGLOBAL *gsGlobal;
static GSFONTM *gsFontM;
static GSTEXTURE *coverTexture;
static int maxTitlesPerPage = MAX_TITLES_PER_PAGE_NTSC;
static char lineBuffer[255];

// Predefined colors
static const u64 WhiteFont = GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80);
static const u64 BlackBG = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x00);

void init480p(GSGLOBAL *gsGlobal) {
  gsGlobal->Mode = GS_MODE_DTV_480P;
  gsGlobal->Interlace = GS_NONINTERLACED;
  gsGlobal->Field = GS_FRAME;
  gsGlobal->Width = 640;
  gsGlobal->Height = 448;
}

int uiInit() {
  gsGlobal = gsKit_init_global();
  gsGlobal->PrimAlphaEnable = GS_SETTING_ON;

  if (gsGlobal->Mode == GS_MODE_PAL) {
    maxTitlesPerPage = MAX_TITLES_PER_PAGE_PAL;
  }
  // init480p(gsGlobal);

  gsFontM = gsKit_init_fontm();

  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

  // Initialize the DMAC
  int res;
  if ((res = dmaKit_chan_init(DMA_CHANNEL_GIF))) {
    printf("ERROR: failed to initlize DMAC: %d\n", res);
    return res;
  }

  // Init screen
  gsKit_init_screen(gsGlobal);
  gsKit_mode_switch(gsGlobal, GS_ONESHOT);
  // Upload font, set font spacing
  if ((res = gsKit_fontm_upload(gsGlobal, gsFontM))) {
    printf("ERROR: failed to upload FONTM: %d\n", res);
    return res;
  }
  gsFontM->Spacing = 0.65f;
  coverTexture = calloc(sizeof(GSTEXTURE), 1);
  coverTexture->Delayed = 1;

  // Init gamepad inputs
  gpadInit();
  return 0;
}

// Invalidates the current loaded texture and loads a new one
int loadCoverArt(char *titleID) {
  gsKit_TexManager_invalidate(gsGlobal, coverTexture);
  gsKit_TexManager_free(gsGlobal, coverTexture);
  int res = 0;
  // Reuse line buffer for building texture path
  snprintf(lineBuffer, 255, "%s%s/%s_COV.jpg", STORAGE_BASE_PATH, artPath, titleID);
  // Try to load JPEG first
  if (!(res = gsKit_texture_jpeg(gsGlobal, coverTexture, lineBuffer))) {
    return res;
  }
  // If failed, try to load PNG
  snprintf(lineBuffer, 255, "%s%s/%s_COV.png", STORAGE_BASE_PATH, artPath, titleID);
  return gsKit_texture_png(gsGlobal, coverTexture, lineBuffer);
}

// Closes gamepad driver and clears up gsKit
void uiCleanup() {
  gpadClose();
  gsKit_TexManager_free(gsGlobal, coverTexture);
  gsKit_free_fontm(gsGlobal, gsFontM);
  gsKit_deinit_global(gsGlobal);
}

// Main UI loop. Displays the target list.
int uiLoop(struct TargetList *titles) {
  int res = 0;
  if (gsGlobal == NULL) {
    if ((res = uiInit())) {
      printf("ERROR: failed to init UI: %d\n", res);
      goto exit;
    };
  }

  int isCoverUninitialized = 1;
  int selectedTitleIdx = 0;
  int input = 0;
  struct Target *curTarget = titles->first;

  // Get last played title and find it in the target list
  char *lastTitle = calloc(sizeof(char), PATH_MAX + 1);
  if (!getLastLaunchedTitle(lastTitle)) {
    while (curTarget != NULL) {
      if (!strcmp(lastTitle, curTarget->fullPath)) {
        selectedTitleIdx = curTarget->idx;
        break;
      }
      curTarget = curTarget->next;
    }
    // Reinitialize target if last played title couldn't be loaded
    if (curTarget == NULL) {
      curTarget = titles->first;
    }
  }
  free(lastTitle);

  // Load cover art
  isCoverUninitialized = loadCoverArt(curTarget->id);

  // Main UI loop
  while (1) {
    gsKit_clear(gsGlobal, BlackBG);

    // Reload target if index has changed
    if (curTarget->idx != selectedTitleIdx) {
      curTarget = getTargetByIdx(titles, selectedTitleIdx);
      isCoverUninitialized = loadCoverArt(curTarget->id);
    }

    // Draw title list
    if (!isCoverUninitialized)
      drawTitleList(titles, selectedTitleIdx, coverTexture);
    else
      drawTitleList(titles, selectedTitleIdx, NULL);

    gsKit_queue_exec(gsGlobal);
    gsKit_sync_flip(gsGlobal);
    gsKit_TexManager_nextFrame(gsGlobal);

    // Process user inputs
    input = getInput(-1);
    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      // Copy target, free title list and launch
      struct Target *target = copyTarget(curTarget);
      freeTargetList(titles);
      uiLaunchTitle(target, NULL);
      goto exit;
    } else if (input & PAD_UP) {
      // Point to the previous title
      if (selectedTitleIdx > 0)
        selectedTitleIdx--;
    } else if (input & PAD_DOWN) {
      // Advance to the next title
      if (selectedTitleIdx < titles->total - 1)
        selectedTitleIdx++;
    } else if (input & (PAD_RIGHT | PAD_R1)) {
      // Switch to the next page
      selectedTitleIdx += maxTitlesPerPage;
      if (selectedTitleIdx >= titles->total)
        selectedTitleIdx = titles->total - 1;
    } else if (input & (PAD_LEFT | PAD_L1)) {
      // Switch to the previous page
      selectedTitleIdx -= maxTitlesPerPage;
      if (selectedTitleIdx < 0)
        selectedTitleIdx = 0;
    } else if (input & PAD_TRIANGLE) {
      // Enter title options screen
      if ((res = uiTitleOptionsLoop(curTarget))) {
        // Something went wrong, main loop must exit immediately
        goto exit;
      }
    } else if (input & PAD_START) {
      // Quit
      break;
    }
  }

exit:
  uiCleanup();
  return res;
}

// Draws title list
void drawTitleList(struct TargetList *titles, int selectedTitleIdx, GSTEXTURE *selectedTitleCover) {
  int curPage = selectedTitleIdx / maxTitlesPerPage;

  // Draw header and footer
  gsFontM->Align = GSKIT_FALIGN_CENTER;
  gsKit_fontm_print_scaled(gsGlobal, gsFontM, gsGlobal->Width / 2, 20, 0, 0.6f, WhiteFont, "Title List");

  // Print page info
  gsFontM->Align = GSKIT_FALIGN_RIGHT;
  snprintf(lineBuffer, 255, "Page %d/%d\nTitle %d/%d", curPage + 1, DIV_ROUND(titles->total, maxTitlesPerPage), selectedTitleIdx + 1, titles->total);
  gsKit_fontm_print_scaled(gsGlobal, gsFontM, gsGlobal->Width - 25, 20, 0, 0.6f, WhiteFont, lineBuffer);

  gsFontM->Align = GSKIT_FALIGN_LEFT;
  gsKit_fontm_print_scaled(gsGlobal, gsFontM, 10, gsGlobal->Height - 50, 0, 0.6f, WhiteFont,
                           "Press \f0090 to launch the title, \f0097 to open launch options\nPress START to exit");

  // Draw title list
  struct Target *curTitle = titles->first;
  while (curTitle != NULL) {
    // Do not display titles before the current page
    if (curTitle->idx < maxTitlesPerPage * curPage) {
      goto next;
    }
    // Do not display titles beyond the current page
    if (curTitle->idx >= maxTitlesPerPage * (curPage + 1)) {
      break;
    }

    // Draw title ID for selected title
    if (selectedTitleIdx == curTitle->idx) {
      // Draw title ID
      gsKit_fontm_print_scaled(gsGlobal, gsFontM, gsGlobal->Width - (140 + 25.0f), 100.0f + 210, 0, 0.7f, WhiteFont, curTitle->id);
    }

    // Draw title name
    snprintf(lineBuffer, 255, "%s %s", ((selectedTitleIdx == curTitle->idx) ? "\efright" : " "), curTitle->name);
    gsKit_fontm_print_scaled(gsGlobal, gsFontM, 10, 50 + ((curTitle->idx % maxTitlesPerPage) * 15), 0, 0.6f, WhiteFont, lineBuffer);

  next:
    curTitle = curTitle->next;
  }

  // Draw cover art placeholder
  gsKit_prim_sprite(gsGlobal, gsGlobal->Width - (140 + 27.0f), 98.0f, gsGlobal->Width - 23.0f, 102.0f + 200, 0,
                    GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x00));
  gsKit_prim_line(gsGlobal, gsGlobal->Width - (140 + 23.0f), 102.0f, gsGlobal->Width - 27.0f, 98.0f + 200, 0, BlackBG);
  gsKit_prim_line(gsGlobal, gsGlobal->Width - 27.0f, 102.0f, gsGlobal->Width - (140 + 23.0f), 98.0f + 200, 0, BlackBG);

  // Draw cover art if it exists
  if (selectedTitleCover != NULL) {
    gsKit_TexManager_bind(gsGlobal, selectedTitleCover);
    gsKit_prim_sprite_texture(gsGlobal, selectedTitleCover, gsGlobal->Width - (selectedTitleCover->Width + 25.0f), 100.0f, 0.0f, 0.0f,
                              gsGlobal->Width - 25.0f, 100.0f + selectedTitleCover->Height, selectedTitleCover->Width, selectedTitleCover->Height, 1,
                              GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  }
}

int uiTitleOptionsLoop(struct Target *target) {
  int res = 0;
  struct ArgumentList *titleArguments = loadLaunchArgumentLists(target);

  uint8_t modes = 0;
  if (!strcmp(COMPAT_MODES_ARG, titleArguments->first->arg)) {
    modes = parseCompatModes(titleArguments->first->value);
  } else {
    // Insert compat mode flag if it doesn't exist
    // Assuming that compat mode flag always exists makes working with arguments much easier.
    insertCompatModeArg(titleArguments, modes);
  }

  // Indexes 0 through CM_NUM_MODES are reserved for compatibility modes
  int selectedArgIdx = 0;
  int totalIndexes = (titleArguments->total - 1) + (CM_NUM_MODES - 1);
  printf("total %d\n", totalIndexes);
  int input = 0;

  // Always start with the second element since the first
  // is guaranteed to be a compatibility mode flag
  struct Argument *curArgument = titleArguments->first->next;

  while (1) {
    gsKit_clear(gsGlobal, BlackBG);

    // Draw header and footer
    gsFontM->Align = GSKIT_FALIGN_CENTER;
    // Print title info
    snprintf(lineBuffer, 255, "%s\n%s", target->name, target->id);
    gsKit_fontm_print_scaled(gsGlobal, gsFontM, gsGlobal->Width / 2, 20, 0, 0.6f, WhiteFont, lineBuffer);
    gsKit_fontm_print_scaled(gsGlobal, gsFontM, gsGlobal->Width / 2, 60, 0, 0.6f, WhiteFont, "Compatibility modes");

    gsFontM->Align = GSKIT_FALIGN_LEFT;
    gsKit_fontm_print_scaled(gsGlobal, gsFontM, 10, gsGlobal->Height - 65, 0, 0.6f, WhiteFont,
                             "Press \f0090 to toggle options \n"
                             "Press \f0095 to launch the title without saving options\n"
                             "Press \f0097 to exit without saving, START to save options");

    drawArgumentList(titleArguments, modes, selectedArgIdx);

    gsKit_queue_exec(gsGlobal);
    gsKit_sync_flip(gsGlobal);
    gsKit_TexManager_nextFrame(gsGlobal);

    // Process user inputs
    input = getInput(-1);
    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      if (selectedArgIdx < CM_NUM_MODES) {
        // Change compat flag in bit mask and update argument value
        modes ^= COMPAT_MODE_MAP[selectedArgIdx].mode;
        storeCompatModes(titleArguments->first->value, modes);
        titleArguments->first->isGlobal = 0;
      } else {
        // Toggle argument
        curArgument->isDisabled = !curArgument->isDisabled;
      }
    } else if (input & PAD_UP) {
      // Point to the previous argument
      if (selectedArgIdx > 0) {
        selectedArgIdx--;
        if (selectedArgIdx >= CM_NUM_MODES)
          curArgument = curArgument->prev;
      }
    } else if (input & PAD_DOWN) {
      // Advance to the next argument, accounting for compatibility modes
      if (selectedArgIdx < totalIndexes) {
        selectedArgIdx++;
        if (selectedArgIdx > CM_NUM_MODES)
          curArgument = curArgument->next;
      }
    } else if (input & PAD_SQUARE) {
      // Launch title without saving arguments
      uiLaunchTitle(target, titleArguments);
      res = 1; // If this was somehow reached, something went terribly wrong
      goto exit;
    } else if (input & PAD_START) {
      updateTitleLaunchArguments(target, titleArguments);
      goto exit;
    } else if (input & PAD_TRIANGLE) {
      // Quit to title list
      goto exit;
    }
  }
exit:
  freeArgumentList(titleArguments);
  return res;
}

void drawArgumentList(struct ArgumentList *arguments, uint8_t compatModes, int selectedArgIdx) {
  int startY = 75;
  int idx = 0;

  // Draw compatibility modes

  // Indexes 0 through CM_NUM_MODES are reserved for compatibility modes
  for (idx = 0; idx < CM_NUM_MODES; idx++) {
    snprintf(lineBuffer, 255, "%s [%s] %s", ((selectedArgIdx == idx) ? "\efright" : " "), ((compatModes & COMPAT_MODE_MAP[idx].mode) ? "x" : " "),
             COMPAT_MODE_MAP[idx].name);
    gsKit_fontm_print_scaled(gsGlobal, gsFontM, 10, startY + (idx * 15), 0, 0.6f, WhiteFont, lineBuffer);
  }

  // Draw other arguments
  if (arguments->total == 1) {
    // First argument is always compatibility mode flags
    return;
  }

  // Always start with the second element since the first
  // is guaranteed to be a compatibility mode flag
  struct Argument *argument = arguments->first->next;
  int curPage = (selectedArgIdx - CM_NUM_MODES) / MAX_ARGUMENTS;
  // Advance start Y offset and add some space after compatibility modes
  startY += (CM_NUM_MODES * 15) + 10;
  idx = 0; // Reset index

  gsFontM->Align = GSKIT_FALIGN_CENTER;
  gsKit_fontm_print_scaled(gsGlobal, gsFontM, gsGlobal->Width / 2, startY, 0, 0.6f, WhiteFont, "Launch arguments");

  gsFontM->Align = GSKIT_FALIGN_RIGHT;
  snprintf(lineBuffer, 255, "Page %d/%d", curPage + 1, DIV_ROUND(arguments->total - 1, MAX_ARGUMENTS));
  gsKit_fontm_print_scaled(gsGlobal, gsFontM, gsGlobal->Width - 25, startY, 0, 0.6f, WhiteFont, lineBuffer);

  gsFontM->Align = GSKIT_FALIGN_LEFT;

  while (argument != NULL) {
    // Do not display arguments before the current page
    if (idx < MAX_ARGUMENTS * curPage) {
      idx++;
      goto next;
    }
    // Do not display arguments beyond the current page
    if (idx >= MAX_ARGUMENTS * (curPage + 1)) {
      break;
    }

    // Draw argument
    snprintf(lineBuffer, 255, "%s %s[%s] %s: %s", (((selectedArgIdx - CM_NUM_MODES) == idx) ? "\efright" : " "), ((argument->isGlobal) ? "(g)" : ""),
             ((argument->isDisabled) ? " " : "x"), argument->arg, argument->value);
    // Increment index for Y coordinate because 'Launch arguments' string occupies space for index 5
    gsKit_fontm_print_scaled(gsGlobal, gsFontM, 10, startY + (((idx % MAX_ARGUMENTS) + 1) * 15), 0, 0.6f, WhiteFont, lineBuffer);

    idx++;
  next:
    argument = argument->next;
  }
}

// Displays Game ID and launches the title
void uiLaunchTitle(struct Target *target, struct ArgumentList *arguments) {
  // Initialize arugments if not set
  if (arguments == NULL) {
    arguments = loadLaunchArgumentLists(target);
  }

  gsKit_clear(gsGlobal, BlackBG);

  // Draw screen with GameID and title parameters
  gsFontM->Align = GSKIT_FALIGN_CENTER;
  snprintf(lineBuffer, 255, "Launching\n%s\n%s\n%s", target->name, target->id, target->fullPath);
  gsKit_fontm_print_scaled(gsGlobal, gsFontM, gsGlobal->Width / 2, gsGlobal->Height / 2 - 30, 0, 0.6f, WhiteFont, lineBuffer);
  drawGameID(target->id);

  gsKit_queue_exec(gsGlobal);
  gsKit_sync_flip(gsGlobal);
  gsKit_TexManager_nextFrame(gsGlobal);

  // Wait a litle bit, cleanup the UI and launch title
  sleep(2);
  uiCleanup();
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
    for (int ii = 7; ii >= 0; ii--) {
      int x = xstart + (i * 16 + ((7 - ii) * 2));
      int x1 = x + 1;

      gsKit_prim_sprite(gsGlobal, x, ystart, x1, ystart + height, 0, GS_SETREG_RGBA(0xFF, 0x00, 0xFF, 0x00));

      uint32_t color = (data[i] >> ii) & 1 ? GS_SETREG_RGBA(0x00, 0xFF, 0xFF, 0x00) : GS_SETREG_RGBA(0xFF, 0xFF, 0x00, 0x00);
      gsKit_prim_sprite(gsGlobal, x1, ystart, x1 + 1, ystart + height, 0, color);
    }
  }
}