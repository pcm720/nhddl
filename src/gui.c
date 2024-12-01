#include "gui.h"
#include "common.h"
#include "gui_graphics.h"
#include "launcher.h"
#include "options.h"
#include "pad.h"
#include <dmaKit.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <libpad.h>
#include <malloc.h>
#include <ps2sdkapi.h>
#include <stdint.h>
#include <stdio.h>

#define DIV_ROUND(n, d) (n + (d - 1)) / d

// Assuming 140x200 cover art
#define COVER_ART_RES_W 140
#define COVER_ART_RES_H 200

int uiLoop(TargetList *titles);
int uiTitleOptionsLoop(Target *title);
void drawTitleList(TargetList *titles, int selectedTitleIdx, int maxTitlesPerPage, GSTEXTURE *selectedTitleCover);
void drawArgumentList(ArgumentList *arguments, uint8_t compatModes, int selectedArgIdx);
void uiLaunchTitle(Target *target, ArgumentList *arguments);
void drawGameID(const char *game_id);

GSGLOBAL *gsGlobal;
static GSTEXTURE *coverTexture;
static char lineBuffer[255];

// Path relative to storage device mountpoint.
// Used to load cover art
static const char artPath[] = "/ART";

// Predefined colors
// static const uint64_t ColorWhite = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
static const uint64_t ColorBlack = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);
static const uint64_t ColorSelected = GS_SETREG_RGBA(0x00, 0x72, 0xA0, 0x80);
static const uint64_t ColorGrey = GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80);

static const uint64_t FontMainColor = ColorGrey;
static const uint64_t BGColor = ColorBlack;
static const uint64_t HeaderTextColor = GS_SETREG_RGBA(0x60, 0x60, 0x60, 0x80);

// Cover art sprite coordinates
// Initialized during uiInit from screen width and height
static int coverArtX2;
static int coverArtY2;
static int coverArtX1;
static int coverArtY1;

static const int headerHeight = 30;
static const int footerHeight = 40;

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
  gsGlobal->DoubleBuffering = GS_SETTING_OFF;
  // Setup TEST register to ignore fully transparent pixels
  gsGlobal->Test->ATST = 7;    // Set alpha test method to NOTEQUAL (pixels with A not equal to AREF pass)
  gsGlobal->Test->AREF = 0x00; // Set reference value to 0x00 (transparent)
  gsGlobal->Test->AFAIL = 0;   // Don't update buffers when test fails

  if (LAUNCHER_OPTIONS.is480pEnabled) {
    printf("Starting UI in progressive mode\n");
    init480p(gsGlobal);
  }

  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);

  // Initialize the DMAC
  int res;
  if ((res = dmaKit_chan_init(DMA_CHANNEL_GIF))) {
    printf("ERROR: Failed to initlize DMAC: %d\n", res);
    return res;
  }

  // Init screen
  gsKit_vram_clear(gsGlobal);
  gsKit_init_screen(gsGlobal);
  gsKit_TexManager_init(gsGlobal);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_mode_switch(gsGlobal, GS_ONESHOT);
  gsKit_clear(gsGlobal, BGColor);

  // Initialize font
  if (initFont()) {
    printf("ERROR: Failed to initialize font\n");
    return -1;
  };

  // Init cover texture
  coverTexture = calloc(sizeof(GSTEXTURE), 1);
  coverArtX2 = (gsGlobal->Width - 10);
  coverArtY2 = (gsGlobal->Height / 2) + (COVER_ART_RES_H / 2);
  coverArtX1 = coverArtX2 - COVER_ART_RES_W;
  coverArtY1 = coverArtY2 - COVER_ART_RES_H;
  coverTexture->Delayed = 1;

  // Init gamepad inputs
  initPad();
  return 0;
}

// Invalidates currently loaded texture and loads a new one
int loadCoverArt(char *titlePath, char *titleID) {
  // Reuse line buffer for building texture path
  // Get device mountpoint into the buffer
  int pathSize = 5;
  if (titlePath[4] == ':') {
    strncpy(lineBuffer, titlePath, 5);
  } else { // Handle numbered devices
    strncpy(lineBuffer, titlePath, 6);
    pathSize = 6;
  }

  // Append cover art path to the mountpoint
  snprintf(lineBuffer + pathSize, 255 - pathSize, "%s/%s_COV.png", artPath, titleID);
  // Upload new texture
  gsKit_TexManager_invalidate(gsGlobal, coverTexture);
  if (gsKit_texture_png(gsGlobal, coverTexture, lineBuffer)) {
    return -1;
  }
  gsKit_TexManager_bind(gsGlobal, coverTexture);
  // Free memory after the texture has been uploaded
  free(coverTexture->Mem);
  coverTexture->Mem = NULL;
  return 0;
}

// Closes gamepad driver, frees textures and deinits gsKit
void closeUI() {
  closePad();
  gsKit_vram_clear(gsGlobal);
  closeFont();
  free(coverTexture);
  gsKit_deinit_global(gsGlobal);
}

// Main UI loop. Displays the target list.
int uiLoop(TargetList *titles) {
  int res = 0;
  if ((gsGlobal == NULL) && (res = uiInit(0))) {
    printf("ERROR: Failed to init UI: %d\n", res);
    goto exit;
  }

  int isCoverUninitialized = 1;
  int selectedTitleIdx = 0;
  int input = 0;
  int maxTitlesPerPage = (gsGlobal->Height - (headerHeight + footerHeight + 10)) / getFontLineHeight();
  Target *curTarget = titles->first;

  // Get last launched title and find it in the target list
  char *lastTitle = calloc(sizeof(char), PATH_MAX + 1);
  if (!getLastLaunchedTitle(lastTitle)) {
    int mountpointLen;
    while (curTarget != NULL) {
      // Compare paths without the mountpoint
      mountpointLen = 5;
      if (curTarget->fullPath[5] == ':') {
        mountpointLen = 6;
      }

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

  // Load cover art
  isCoverUninitialized = loadCoverArt(curTarget->fullPath, curTarget->id);

  // Main UI loop
  while (1) {
    gsKit_clear(gsGlobal, BGColor);
    gsKit_TexManager_nextFrame(gsGlobal);

    // Reload target if index has changed
    if (curTarget->idx != selectedTitleIdx) {
      curTarget = getTargetByIdx(titles, selectedTitleIdx);
      isCoverUninitialized = loadCoverArt(curTarget->fullPath, curTarget->id);
    }

    // Draw title list
    if (!isCoverUninitialized)
      drawTitleList(titles, selectedTitleIdx, maxTitlesPerPage, coverTexture);
    else
      drawTitleList(titles, selectedTitleIdx, maxTitlesPerPage, NULL);

    gsKit_queue_exec(gsGlobal);
    gsKit_sync_flip(gsGlobal);

    // Process user inputs
    input = getInput(-1);
    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      // Copy target, free title list and launch
      Target *target = copyTarget(curTarget);
      freeTargetList(titles);
      uiLaunchTitle(target, NULL);
      // Something went wrong, main loop must exit immediately
      return -1;
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
        return -1;
      }
    } else if (input & PAD_START) {
      // Quit
      break;
    }
  }

exit:
  closeUI();
  return res;
}

void drawTitleListFooter() {
  drawIconWindow(10, gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CIRCLE);
  drawIconWindow(10 + getIconWidth(ICON_CIRCLE), gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CROSS);
  drawTextWindow(15 + getIconWidth(ICON_CIRCLE) + getIconWidth(ICON_CROSS), gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, HeaderTextColor,
                 ALIGN_VCENTER, "Launch title");

  drawIconWindow(0, gsGlobal->Height - footerHeight, gsGlobal->Width - getLineWidth("Exit") - 5, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER,
                 ICON_START);
  drawTextWindow(5 + getIconWidth(ICON_START), gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0, HeaderTextColor, ALIGN_CENTER,
                 "Exit");

  drawIconWindow(gsGlobal->Width - 15 - getIconWidth(ICON_TRIANGLE) - getLineWidth("Title options"), gsGlobal->Height - footerHeight,
                 gsGlobal->Width - 10, gsGlobal->Height, 0, FontMainColor, ALIGN_VCENTER | ALIGN_LEFT, ICON_TRIANGLE);
  drawTextWindow(0, gsGlobal->Height - footerHeight, gsGlobal->Width - 10, gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER | ALIGN_RIGHT,
                 "Title options");
}

// Draws title list
void drawTitleList(TargetList *titles, int selectedTitleIdx, int maxTitlesPerPage, GSTEXTURE *selectedTitleCover) {
  int curPage = selectedTitleIdx / maxTitlesPerPage;

  // Draw header and footer
  int titleY = headerHeight;
  drawTextWindow(10, headerHeight - getFontLineHeight(), coverArtX2, 0, 0, HeaderTextColor, ALIGN_HCENTER, "Title List");
  snprintf(lineBuffer, 255, "Page %d/%d\nTitle %d/%d", curPage + 1, DIV_ROUND(titles->total, maxTitlesPerPage), selectedTitleIdx + 1, titles->total);
  drawTextWindow(10, headerHeight - getFontLineHeight(), coverArtX2, 0, 0, HeaderTextColor, ALIGN_RIGHT, lineBuffer);

  drawTitleListFooter();

  // Draw title list
  Target *curTitle = titles->first;

  titleY += getFontLineHeight() / 2;
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
      // Draw title ID and device type under the cover art
      drawTextWindow(coverArtX1,
                     drawTextWindow(coverArtX1, coverArtY2 + 5, coverArtX2, 0, 0, FontMainColor, ALIGN_HCENTER,
                                    curTitle->id), // Use y coordinate return by title ID drawing function as an argument
                     coverArtX2, 0, 0, FontMainColor, ALIGN_HCENTER, modeToString(curTitle->deviceType));
    }

    // Draw title name
    titleY = drawText(10, titleY, 0, coverArtX1, 0, ((selectedTitleIdx == curTitle->idx) ? ColorSelected : FontMainColor), curTitle->name);

  next:
    curTitle = curTitle->next;
  }

  // Draw cover art placeholder/frame
  gsKit_prim_sprite(gsGlobal, coverArtX1 - 2, coverArtY1 - 2, coverArtX2 + 2, coverArtY2 + 2, 1, FontMainColor);

  // Draw cover art if it exists
  if (selectedTitleCover != NULL) {
    // Temproraily disable alpha blending
    // Some PNGs require inverted alpha channel value to display properly
    // Since cover art has nothing to blend, we can bypass the issue altogether
    gsGlobal->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_prim_sprite_texture(gsGlobal, selectedTitleCover, coverArtX1, coverArtY1, 0.0f, 0.0f, coverArtX2, coverArtY2, selectedTitleCover->Width,
                              selectedTitleCover->Height, 2, FontMainColor);
    gsGlobal->PrimAlphaEnable = GS_SETTING_ON;
  } else {
    gsKit_prim_sprite(gsGlobal, coverArtX1, coverArtY1, coverArtX2, coverArtY2, 1, BGColor);
    drawTextWindow(coverArtX1, coverArtY1, coverArtX2, coverArtY2, 1, FontMainColor, ALIGN_CENTER, "No cover art");
  }
}

void drawTitleOptionsFooter() {
  drawIconWindow(10, gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CIRCLE);
  drawIconWindow(10 + getIconWidth(ICON_CIRCLE), gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, FontMainColor, ALIGN_CENTER, ICON_CROSS);
  drawTextWindow(15 + getIconWidth(ICON_CIRCLE) + getIconWidth(ICON_CROSS), gsGlobal->Height - footerHeight, 0, gsGlobal->Height, 0, HeaderTextColor,
                 ALIGN_VCENTER, "Toggle");

  drawIconWindow((gsGlobal->Width * 3 / 8) - getIconWidth(ICON_SQUARE), gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_SQUARE);
  drawTextWindow((gsGlobal->Width * 3 / 8) + 5, gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER,
                 "Test");

  drawIconWindow((gsGlobal->Width * 5 / 8), gsGlobal->Height - footerHeight, gsGlobal->Width - getLineWidth("Save") - 5, gsGlobal->Height, 0,
                 FontMainColor, ALIGN_VCENTER, ICON_START);
  drawTextWindow((gsGlobal->Width * 5 / 8) + 5 + getIconWidth(ICON_START), gsGlobal->Height - footerHeight, gsGlobal->Width, gsGlobal->Height, 0,
                 HeaderTextColor, ALIGN_VCENTER, "Save");

  drawIconWindow(gsGlobal->Width - 15 - getIconWidth(ICON_TRIANGLE) - getLineWidth("Cancel"), gsGlobal->Height - footerHeight, gsGlobal->Width - 10,
                 gsGlobal->Height, 0, FontMainColor, ALIGN_VCENTER | ALIGN_LEFT, ICON_TRIANGLE);
  drawTextWindow(0, gsGlobal->Height - footerHeight, gsGlobal->Width - 10, gsGlobal->Height, 0, HeaderTextColor, ALIGN_VCENTER | ALIGN_RIGHT,
                 "Cancel");
}

// Title options screen handler
int uiTitleOptionsLoop(Target *target) {
  int res = 0;
  uint8_t modes = 0;

  // Load arguments from config files
  ArgumentList *titleArguments = loadLaunchArgumentLists(target);

  // Parse compatibility modes
  if ((titleArguments->total != 0) && !strcmp(COMPAT_MODES_ARG, titleArguments->first->arg)) {
    modes = parseCompatModes(titleArguments->first->value);
  } else {
    // Insert compat mode flag if it doesn't exist
    // Assuming that compat mode flag always exists makes working with arguments much easier.
    insertCompatModeArg(titleArguments, modes);
  }

  // Indexes 0 through CM_NUM_MODES are reserved for compatibility modes
  int selectedArgIdx = 0;
  int totalIndexes = (titleArguments->total - 1) + (CM_NUM_MODES - 1);
  int input = 0;

  // Always start with the second element since the first
  // is guaranteed to be a compatibility mode flag
  Argument *curArgument = titleArguments->first->next;

  while (1) {
    gsKit_clear(gsGlobal, BGColor);

    // Draw header
    snprintf(lineBuffer, 255, "%s\n%s", target->name, target->id);
    drawTextWindow(10, headerHeight-getFontLineHeight(), coverArtX2, 0, 0, HeaderTextColor, ALIGN_HCENTER, lineBuffer);
    drawTextWindow(10, headerHeight+1.5*getFontLineHeight(), coverArtX2, 0, 0, FontMainColor, ALIGN_HCENTER, "Compatibility modes");

    // Draw footer
    drawTitleOptionsFooter();

    drawArgumentList(titleArguments, modes, selectedArgIdx);

    gsKit_queue_exec(gsGlobal);
    gsKit_sync_flip(gsGlobal);

    // Process user inputs
    input = getInput(-1);
    if (input & (PAD_CROSS | PAD_CIRCLE)) {
      if (selectedArgIdx < CM_NUM_MODES) {
        // Change compat flag in bit mask and update argument value
        modes ^= COMPAT_MODE_MAP[selectedArgIdx].mode;
        storeCompatModes(titleArguments->first, modes);
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

// Draws title arguments
void drawArgumentList(ArgumentList *arguments, uint8_t compatModes, int selectedArgIdx) {
  int startY = headerHeight+2.5*getFontLineHeight();
  int idx = 0;

  // Draw compatibility modes

  // Indexes 0 through CM_NUM_MODES are reserved for compatibility modes
  for (idx = 0; idx < CM_NUM_MODES; idx++) {
    if (compatModes & COMPAT_MODE_MAP[idx].mode) {
      drawIconWindow(10, startY, 20, startY + getFontLineHeight(), 0, FontMainColor, ALIGN_CENTER, ICON_ENABLED);
    }
    startY = drawText(15+getIconWidth(ICON_ENABLED), startY, 0, 0, 0, ((selectedArgIdx == idx) ? ColorSelected : FontMainColor), COMPAT_MODE_MAP[idx].name);
  }

  // Draw other arguments
  if (arguments->total == 1) {
    // First argument is always compatibility mode flags
    return;
  }

  // Advance start Y offset and add some space after compatibility modes
  startY += 10;
  idx = 0; // Reset index

  startY = drawTextWindow(10, startY, coverArtX2, 0, 0, FontMainColor, ALIGN_CENTER, "Launch arguments");

  // Set number of elements per page according to line height and available screen height
  int maxArguments = (gsGlobal->Height - startY - footerHeight) / getFontLineHeight();
  int curPage = (selectedArgIdx - (int)CM_NUM_MODES) / maxArguments;

  snprintf(lineBuffer, 255, "Page %d/%d", curPage + 1, DIV_ROUND(arguments->total - 1, maxArguments));
  startY = drawTextWindow(10, startY - getFontLineHeight(), coverArtX2, 0, 0, HeaderTextColor, ALIGN_RIGHT, lineBuffer);

  // Always start with the second element since the first
  // is guaranteed to be a compatibility mode flag
  Argument *argument = arguments->first->next;
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
      drawIconWindow(10, startY, 20, startY + getFontLineHeight(), 0, FontMainColor, ALIGN_CENTER, ICON_ENABLED);

    snprintf(lineBuffer, 255, "%s%s%s %s", ((argument->isGlobal) ? "[G] " : ""), argument->arg, (!strlen(argument->value)) ? "" : ":",
             argument->value);
    // Increment index for Y coordinate because 'Launch arguments' string occupies space for index 5
    startY = drawText(15+getIconWidth(ICON_ENABLED), startY, 0, 0, 0, (((selectedArgIdx - (int)CM_NUM_MODES) == idx) ? ColorSelected : FontMainColor), lineBuffer);

    idx++;
  next:
    argument = argument->next;
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
  gsKit_sync_flip(gsGlobal);

  // Wait a litle bit, cleanup the UI and launch title
  sleep(2);
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
    for (int ii = 7; ii >= 0; ii--) {
      int x = xstart + (i * 16 + ((7 - ii) * 2));
      int x1 = x + 1;

      gsKit_prim_sprite(gsGlobal, x, ystart, x1, ystart + height, 0, GS_SETREG_RGBA(0xFF, 0x00, 0xFF, 0x80));

      uint32_t color = (data[i] >> ii) & 1 ? GS_SETREG_RGBA(0x00, 0xFF, 0xFF, 0x80) : GS_SETREG_RGBA(0xFF, 0xFF, 0x00, 0x80);
      gsKit_prim_sprite(gsGlobal, x1, ystart, x1 + 1, ystart + height, 0, color);
    }
  }
}
