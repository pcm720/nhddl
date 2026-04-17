#include "ui/views/view_options_popup.h"
#include "backends/backends.h"
#include "devices/utils.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/main_scene.h"
#include <libpad.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define POPUP_Y 120
#define POPUP_W 240
#define POPUP_X (SCENE_VW - POPUP_W) / 2
#define INDICATOR_R 5 // Device filter indicator radius
static const char *entries[] = {"Devices", "Favorites only", "Favorites first", "Sort"};
#define ENTRY_COUNT sizeof(entries) / sizeof(char *)

static View s_view;
static int s_viewOptionsSelection;
static uint32_t s_savedFilterMask;

// Device filter subview
static void deviceFilterPopupDraw(View *v, int zOrder);
static ViewResult deviceFilterPopupOnInput(View *v, int padInput);

static void viewOptionsPopupOnEnter(View *v) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  s_viewOptionsSelection = 0;
}

static void viewOptionsPopupOnLeave(View *v) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  if (!d)
    return;
}

static void viewOptionsPopupDraw(View *v, int zOrder) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  // Dynamic modal height (ENTRY_COUNT + header + spacing)
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (ENTRY_COUNT + 2));
  const int vx1 = POPUP_X, vy1 = POPUP_Y, vx2 = POPUP_X + POPUP_W, vy2 = POPUP_Y + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;
  const int leftX = vx1 + SCENE_KEEPOUT;
  const int rightX = vx2 - SCENE_KEEPOUT;
  static char lineBuf[50] = {0};

  // Background
  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  // Header
  fontRenderString(FONT_DEFAULT, centerX, vy1 + SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, "View options", HeaderTextColor);
  // Entries
  int entryY = vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE;
  for (int i = 0; i < ENTRY_COUNT; i++) {
    char *val = NULL;
    switch (i) {
    case 0:
      if (d->filterDeviceMask == 0) {
        snprintf(lineBuf, sizeof(lineBuf), "All");
      } else {
        int sel = 0;
        for (int i = 0; i < getBackendDeviceCount(); i++)
          if (d->filterDeviceMask & (1 << i))
            sel++;
        snprintf(lineBuf, sizeof(lineBuf), "%d", sel);
      }
      val = lineBuf;
      break;
    case 1:
      val = d->favoritesOnly ? "Yes" : "No";
      break;
    case 2:
      val = d->favoritesOnTop ? "Yes" : "No";
      break;
    case 3:
      val = d->sortAscending ? "Ascending" : "Descending";
      break;
    default:
      val = "Unknown";
      break;
    }

    uint64_t col = (i == s_viewOptionsSelection) ? ColorSelected : FontMainColor;
    fontRenderInRect(FONT_DEFAULT, leftX, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, entries[i], col);
    fontRenderInRect(FONT_DEFAULT, leftX, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, val, col);

    entryY += SCENE_ROW_SIZE;
  }
}

static void applyOptionChange(MainSceneData *d, int dir) {
  if (!d)
    return;
  if (s_viewOptionsSelection == 1) {
    d->favoritesOnly = !d->favoritesOnly;
    d->listDirty = 1;
  } else if (s_viewOptionsSelection == 2) {
    d->favoritesOnTop = !d->favoritesOnTop;
    d->listDirty = 1;
  } else if (s_viewOptionsSelection == 3) {
    // Sort: wrap around (LEFT/RIGHT cycle ascending <-> descending)
    d->sortAscending = !d->sortAscending;
    d->listDirty = 1;
  }
  // Row 0 (filter) is handled in onInput
}

static ViewResult viewOptionsPopupOnInput(View *v, int padInput) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  if (padInput & PAD_CIRCLE)
    return ViewResult_Pop;
  if (padInput & PAD_CROSS) {
    if (s_viewOptionsSelection == 0 && d) {
      s_savedFilterMask = d ? d->filterDeviceMask : 0;
      v->draw = deviceFilterPopupDraw;
      v->onInput = deviceFilterPopupOnInput;
    }
    return ViewResult_Continue;
  }
  if (!d)
    return ViewResult_Continue;
  if (padInput & PAD_UP) {
    s_viewOptionsSelection--;
    if (s_viewOptionsSelection < 0)
      s_viewOptionsSelection = ENTRY_COUNT - 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    s_viewOptionsSelection++;
    if (s_viewOptionsSelection >= ENTRY_COUNT)
      s_viewOptionsSelection = 0;
    return ViewResult_Continue;
  }
  if (padInput & PAD_LEFT) {
    applyOptionChange(d, -1);
    return ViewResult_Continue;
  }
  if (padInput & PAD_RIGHT) {
    applyOptionChange(d, 1);
    return ViewResult_Continue;
  }
  return ViewResult_Continue;
}

struct View *viewOptionsPopupGetView(void) {
  s_view = (View){
      .type = ViewType_Modal,
      .userdata = NULL,
      .onEnter = viewOptionsPopupOnEnter,
      .onLeave = viewOptionsPopupOnLeave,
      .draw = viewOptionsPopupDraw,
      .onInput = viewOptionsPopupOnInput,
  };
  return &s_view;
}

//
// Device filter subview
//

static void formatDeviceName(struct BackendDevice *dev, char *buf, size_t bufSize) {
  if (!dev || !buf || bufSize == 0)
    return;
  char *typeStr = getDeviceString(dev->type);
  snprintf(buf, bufSize, "%s %d", typeStr, (int)dev->index);
}

static void deviceFilterPopupDraw(View *v, int zOrder) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  // Dynamic height
  int n = getBackendDeviceCount();
  int itemCount = (n > ENTRY_COUNT) ? (n + 1) : ENTRY_COUNT;
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (itemCount + 2)) + POPUP_PROMPT_ROW_OFFSET;
  const int vx1 = POPUP_X, vy1 = POPUP_Y, vx2 = POPUP_X + POPUP_W, vy2 = POPUP_Y + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;
  const int leftX = vx1 + SCENE_KEEPOUT;
  const int rightX = vx2 - SCENE_KEEPOUT;

  // Background
  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  // Header
  fontRenderString(FONT_DEFAULT, centerX, vy1 + SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, "Filter devices", HeaderTextColor);
  // Devices
  int entryY = vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE;
  static char nameBuf[24];
  for (int i = 0; i < (n + 1); i++) {
    int on = 0;
    const char *label;
    if (i == 0) {
      on = (d && d->filterDeviceMask == 0) ? 1 : 0;
      label = "All devices";
    } else {
      int devIdx = i - 1;
      on = (d && (d->filterDeviceMask & (1u << (unsigned)devIdx))) ? 1 : 0;
      struct BackendDevice *dev = getBackendDeviceAt(devIdx);
      formatDeviceName(dev, nameBuf, sizeof(nameBuf));
      label = nameBuf;
    }
    uint64_t col = (i == s_viewOptionsSelection) ? ColorSelected : FontMainColor;
    uiDrawSelectionIndicator(gsGlobal, leftX + INDICATOR_R, (entryY + SCENE_ROW_SIZE / 2), INDICATOR_R, zOrder, col, on);
    fontRenderInRect(FONT_DEFAULT, leftX + INDICATOR_R * 4, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1,
                     label, col);
    entryY += SCENE_ROW_SIZE;
  }

  uiDrawModalPrompt(gsGlobal, vx1, vx2, 0, vy2, zOrder, HeaderTextColor, ICON_CROSS, "Toggle");
  uiDrawModalPrompt(gsGlobal, vx1, vx2, 1, vy2, zOrder, HeaderTextColor, ICON_CIRCLE, "Back");
}

static ViewResult deviceFilterPopupOnInput(View *v, int padInput) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  int n = getBackendDeviceCount();
  int totalRows = 1 + n;

  if (padInput & PAD_CIRCLE) {
    s_viewOptionsSelection = 0;
    v->draw = viewOptionsPopupDraw;
    v->onInput = viewOptionsPopupOnInput;
    if (d->filterDeviceMask != s_savedFilterMask)
      d->listDirty = 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_CROSS && d) {
    if (s_viewOptionsSelection == 0)
      d->filterDeviceMask = 0;
    else {
      int devIdx = s_viewOptionsSelection - 1;
      if (devIdx >= 0 && devIdx < n)
        d->filterDeviceMask ^= (1u << (unsigned)devIdx);
    }
    return ViewResult_Continue;
  }

  if (padInput & PAD_UP) {
    s_viewOptionsSelection--;
    if (s_viewOptionsSelection < 0)
      s_viewOptionsSelection = totalRows - 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    s_viewOptionsSelection++;
    if (s_viewOptionsSelection >= totalRows)
      s_viewOptionsSelection = 0;
    return ViewResult_Continue;
  }
  return ViewResult_Continue;
}
