#include "ui/views/menu_scene.h"
#include "backends/backends.h"
#include "backends/cache.h"
#include "config/arguments.h"
#include "config/config.h"
#include "devices/devices.h"
#include "devices/utils.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/title_list.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/exit_modal.h"
#include "ui/views/file_selector_scene.h"
#include "ui/views/main_scene.h"
#include "ui/views/title_options_scene.h"
#include "ui/worker/worker.h"
#include <libpad.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INDICATOR_R 5 // Device selector indicator radius

static View s_view;
static char s_osdBuf[96];
static int s_menuSelection = 0;
static uint32_t enabledDeviceTypes = 0;
static uint32_t s_loadedDeviceTypes = 0;
static uint32_t s_deviceMaskAtSubmenuOpen = 0;
static DeviceType deviceTypes[MAX_DEVICES] = {0};
static uint8_t deviceCount = 0;
static VModeType s_pendingVMode = VMode_NONE;
static int s_pendingWidescreen = 0;
static int s_pendingProbeDelay = 0;
static int s_pendingAutolaunch = 0;
static char s_pendingNeutrinoPath[PATH_MAX + 1];

typedef enum {
  MenuSubview_Main = 0,
  MenuSubview_Devices,
  MenuSubview_NHDDLOptions,
  MenuSubview_VideoMode,
  MenuSubview_ProbeDelay,
  MenuSubview_Autolaunch,
} MenuSubview;

static MenuSubview s_subview = MenuSubview_Main;

static void deviceSelectorPopupDraw(View *v, int zOrder);
static ViewResult deviceSelectorPopupOnInput(View *v, int padInput);
static void optionsPopupDraw(View *v, int zOrder);
static ViewResult optionsPopupOnInput(View *v, int padInput);
static void videoModePopupDraw(View *v, int zOrder);
static ViewResult videoModePopupOnInput(View *v, int padInput);
static void probeDelayPopupDraw(View *v, int zOrder);
static ViewResult probeDelayPopupOnInput(View *v, int padInput);
static void autolaunchPopupDraw(View *v, int zOrder);
static ViewResult autolaunchPopupOnInput(View *v, int padInput);
static void menuDraw(View *v, int zOrder);

static const int s_probeDelayChoices[] = {0, 3, 5, 10, 15, 20};
static const int s_autolaunchChoices[] = {0, 3, 5, 10, 15, 30};
#define PROBE_DELAY_COUNT ((int)(sizeof(s_probeDelayChoices) / sizeof(s_probeDelayChoices[0])))
#define AUTOLAUNCH_COUNT ((int)(sizeof(s_autolaunchChoices) / sizeof(s_autolaunchChoices[0])))

static void menuClearArgList(ArgumentList *l) {
  Argument *cur = l->first;
  while (cur != NULL) {
    Argument *next = cur->next;
    if (cur->arg)
      free(cur->arg);
    if (cur->value)
      free(cur->value);
    free(cur);
    cur = next;
  }
  l->first = NULL;
  l->last = NULL;
  l->total = 0;
}

static int findChoiceIndex(const int *choices, int count, int value) {
  for (int i = 0; i < count; i++) {
    if (choices[i] == value)
      return i;
  }
  return 0;
}

static const char *videoModeToLabel(VModeType v) {
  switch (v) {
  case VMode_480i:
    return "480i";
  case VMode_576i:
    return "576i";
  case VMode_480p:
    return "480p";
  case VMode_576p:
    return "576p";
  case VMode_720p:
    return "720p";
  default:
    return "System default";
  }
}

static void trimPathMiddle(const char *src, char *out, size_t outSize) {
  if (!src || !src[0]) {
    snprintf(out, outSize, "(not set)");
    return;
  }
  size_t len = strlen(src);
  if (len < outSize) {
    snprintf(out, outSize, "%s", src);
    return;
  }
  if (outSize < 8) {
    out[0] = '\0';
    return;
  }
  size_t head = (outSize - 4) / 2;
  size_t tail = outSize - 4 - head;
  snprintf(out, outSize, "%.*s...%s", (int)head, src, src + len - tail);
}

static void onNeutrinoPathSelected(const char *path, void *userdata) {
  (void)userdata;
  if (!path || !path[0])
    return;
  snprintf(s_pendingNeutrinoPath, sizeof(s_pendingNeutrinoPath), "%s", path);
  showOSD("Neutrino path updated", 120);
}

static void refreshLoadedDeviceTypes(void) {
  s_loadedDeviceTypes = 0;
  for (int i = 0; i < deviceCount; i++) {
    if (isDeviceLoaded(deviceTypes[i]))
      s_loadedDeviceTypes |= deviceTypes[i];
  }
}

static void applyAndSaveSettings(MainSceneData *d) {
  DeviceType prevMask = getEnabledDevices();
  setEnabledDevices(enabledDeviceTypes);
  setVMode(s_pendingVMode);
  setWidescreen(s_pendingWidescreen);
  setProbeDelay(s_pendingProbeDelay);
  setAutolaunchTimeout(s_pendingAutolaunch);
  setNeutrinoPath(s_pendingNeutrinoPath);

  if (d && prevMask != enabledDeviceTypes) {
    d->rebuildSelectionTarget = NULL;
    mainSceneBeginBackendResync(d);
    workerEnqueueApplyEnabledDevices();
  }
  refreshLoadedDeviceTypes();
  workerEnqueueSaveConfig();
  showOSD("Settings saved", 120);
}

static void menuOnEnter(View *v) {
  int coldOpen = (v->draw == menuDraw);
  enabledDeviceTypes = getEnabledDevices();
  s_loadedDeviceTypes = 0;
  deviceCount = getSupportedDeviceTypesArray(deviceTypes, MAX_DEVICES);
  for (int i = 0; i < deviceCount; i++) {
    if (isDeviceLoaded(deviceTypes[i]))
      s_loadedDeviceTypes |= deviceTypes[i];
  }
  // Unsaved NHDDL options edits (incl. Neutrino path from file picker) must survive popping the file selector.
  if (coldOpen) {
    s_pendingVMode = getVMode();
    s_pendingWidescreen = getWidescreen();
    s_pendingProbeDelay = getProbeDelay();
    s_pendingAutolaunch = getAutolaunchTimeout();
    snprintf(s_pendingNeutrinoPath, sizeof(s_pendingNeutrinoPath), "%s", getNeutrinoPath());
    s_menuSelection = 0;
    s_subview = MenuSubview_Main;
  }
}

#define POPUP_W 240
#define POPUP_X (SCENE_VW - POPUP_W) / 2
static const char *entries[] = {"Enabled devices", "NHDDL options", "Save settings", "Reload list", "Invalidate cache", "Exit"};
#define ENTRY_COUNT sizeof(entries) / sizeof(char *)
#define MAIN_MENU_NHDDL_OPTIONS 1
#define MAIN_MENU_GLOBAL_NEUTRINO 2
static const char *optionsEntries[] = {"Video mode", "Probe delay", "Autolaunch", "Neutrino path"};
#define OPTIONS_COUNT ((int)(sizeof(optionsEntries) / sizeof(optionsEntries[0])))

static void menuDraw(View *v, int zOrder) {
  (void)v;
  // Dynamic modal height (ENTRY_COUNT + header + spacing)
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (ENTRY_COUNT + 2));
  const int vx1 = POPUP_X, vy1 = (SCENE_VH - POPUP_H) / 2;
  const int vx2 = POPUP_X + POPUP_W, vy2 = vy1 + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;

  // Background
  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  // Header
  fontRenderString(FONT_DEFAULT, centerX, vy1 + SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, "Main menu", HeaderTextColor);
  // Entries
  int optionY = vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE * 1.5;
  for (int i = 0; i < ENTRY_COUNT; i++) {
    uint64_t col = (i == s_menuSelection) ? ColorSelected : FontMainColor;
    fontRenderString(FONT_DEFAULT, centerX, optionY, FONT_ALIGN_HCENTER, 0, 0, zOrder, entries[i], col);
    optionY += SCENE_ROW_SIZE;
  }
}

static ViewResult menuOnInput(View *v, int padInput) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  if (d->exitRequested)
    return ViewResult_Pop;

  if (padInput & PAD_CIRCLE)
    return ViewResult_Pop;
  if (padInput & PAD_UP) {
    s_menuSelection--;
    if (s_menuSelection < 0)
      s_menuSelection = ENTRY_COUNT - 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    s_menuSelection++;
    if (s_menuSelection == ENTRY_COUNT)
      s_menuSelection = 0;
    return ViewResult_Continue;
  }
  if (padInput & PAD_CROSS) {
    switch (s_menuSelection) {
    case 0:
      s_deviceMaskAtSubmenuOpen = enabledDeviceTypes;
      s_menuSelection = 0;
      s_subview = MenuSubview_Devices;
      v->draw = deviceSelectorPopupDraw;
      v->onInput = deviceSelectorPopupOnInput;
      break;
    case MAIN_MENU_NHDDL_OPTIONS:
      s_menuSelection = 0;
      s_subview = MenuSubview_NHDDLOptions;
      v->draw = optionsPopupDraw;
      v->onInput = optionsPopupOnInput;
      break;
    case 2:
      applyAndSaveSettings(d);
      break;
    case 3:
      d->rebuildSelectionTarget = NULL;
      mainSceneBeginBackendResync(d);
      workerEnqueueRescanDevices();
      showOSD("Reloading...", 120);
      break;
    case 4:
      workerEnqueueInvalidateCache();
      showOSD("Cache invalidated", 120);
      break;
    case 5:
      // Exit
      exitModalGetView()->userdata = d;
      viewStackPush(viewStack, exitModalGetView());
      break;
    }
  }
  return ViewResult_Continue;
}

static void menuOnLeave(View *v) { (void)v; }

struct View *menuGetView(void) {
  s_view = (View){
      .type = ViewType_Modal,
      .userdata = NULL,
      .onEnter = menuOnEnter,
      .onLeave = menuOnLeave,
      .draw = menuDraw,
      .onInput = menuOnInput,
  };
  return &s_view;
}

//
// Device selection subview
//

static void deviceSelectorPopupDraw(View *v, int zOrder) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  // Dynamic height
  int itemCount = (deviceCount > ENTRY_COUNT) ? deviceCount : ENTRY_COUNT;
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (itemCount + 2)) + POPUP_PROMPT_ROW_OFFSET;
  const int vx1 = POPUP_X, vy1 = (SCENE_VH - POPUP_H) / 2;
  const int vx2 = POPUP_X + POPUP_W, vy2 = vy1 + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;
  const int leftX = vx1 + SCENE_KEEPOUT;
  const int rightX = vx2 - SCENE_KEEPOUT;

  // Background
  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  // Header
  fontRenderString(FONT_DEFAULT, centerX, vy1 + SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, "Devices", HeaderTextColor);
  // Devices
  int entryY = vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE;
  for (int i = 0; i < deviceCount; i++) {
    uint64_t col = (i == s_menuSelection) ? ColorSelected : FontMainColor;
    uiDrawSelectionIndicator(gsGlobal, leftX + INDICATOR_R, (entryY + SCENE_ROW_SIZE / 2), INDICATOR_R, zOrder, col,
                             (enabledDeviceTypes & deviceTypes[i]));
    fontRenderInRect(FONT_DEFAULT, leftX + INDICATOR_R * 4, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1,
                     getDeviceString(deviceTypes[i]), col);
    entryY += SCENE_ROW_SIZE;
  }

  uiDrawModalPrompt(gsGlobal, vx1, vx2, 0, vy2, zOrder, HeaderTextColor, ICON_CROSS, "Toggle");
  uiDrawModalPrompt(gsGlobal, vx1, vx2, 1, vy2, zOrder, HeaderTextColor, ICON_CIRCLE, "Back");
}

static ViewResult deviceSelectorPopupOnInput(View *v, int padInput) {
  if (padInput & PAD_CIRCLE) {
    MainSceneData *d = (MainSceneData *)v->userdata;
    if (enabledDeviceTypes != s_deviceMaskAtSubmenuOpen) {
      setEnabledDevices(enabledDeviceTypes);
      if (d) {
        d->rebuildSelectionTarget = NULL;
        mainSceneBeginBackendResync(d);
        workerEnqueueApplyEnabledDevices();
        showOSD("Updating devices...", 120);
      }
      refreshLoadedDeviceTypes();
    }
    s_subview = MenuSubview_Main;
    v->draw = menuDraw;
    v->onInput = menuOnInput;

    return ViewResult_Continue;
  }
  if (padInput & PAD_CROSS) {
    if (deviceTypes[s_menuSelection] == Device_MMCE)
      enabledDeviceTypes &= ~Device_MX4SIO;
    else if (deviceTypes[s_menuSelection] == Device_MX4SIO)
      enabledDeviceTypes &= ~Device_MMCE;

    enabledDeviceTypes ^= deviceTypes[s_menuSelection];
    return ViewResult_Continue;
  }

  if (padInput & PAD_UP) {
    s_menuSelection--;
    if (s_menuSelection < 0)
      s_menuSelection = deviceCount - 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    s_menuSelection++;
    if (s_menuSelection >= deviceCount)
      s_menuSelection = 0;
    return ViewResult_Continue;
  }
  return ViewResult_Continue;
}

static void optionsPopupDraw(View *v, int zOrder) {
  (void)v;
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (OPTIONS_COUNT + 2));
  const int vx1 = POPUP_X, vy1 = (SCENE_VH - POPUP_H) / 2;
  const int vx2 = POPUP_X + POPUP_W, vy2 = vy1 + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;
  const int leftX = vx1 + SCENE_KEEPOUT;
  const int rightX = vx2 - SCENE_KEEPOUT;

  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  fontRenderString(FONT_DEFAULT, centerX, vy1 + SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, "NHDDL options", HeaderTextColor);

  int entryY = vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE;
  char valueBuf[128];
  for (int i = 0; i < OPTIONS_COUNT; i++) {
    const char *value = "";
    if (i == 0) {
      snprintf(valueBuf, sizeof(valueBuf), "%s, WS: %s", videoModeToLabel(s_pendingVMode), s_pendingWidescreen ? "On" : "Off");
      value = valueBuf;
    } else if (i == 1) {
      snprintf(valueBuf, sizeof(valueBuf), "%d s", s_pendingProbeDelay);
      value = valueBuf;
    } else if (i == 2) {
      if (s_pendingAutolaunch > 0)
        snprintf(valueBuf, sizeof(valueBuf), "%d s", s_pendingAutolaunch);
      else
        snprintf(valueBuf, sizeof(valueBuf), "Off");
      value = valueBuf;
    } else if (i == 3) {
      trimPathMiddle(s_pendingNeutrinoPath, valueBuf, sizeof(valueBuf));
      value = valueBuf;
    }
    uint64_t col = (i == s_menuSelection) ? ColorSelected : FontMainColor;
    fontRenderInRect(FONT_DEFAULT, leftX, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1,
                     optionsEntries[i], col);
    fontRenderInRect(FONT_DEFAULT, leftX, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder + 1, value, col);
    entryY += SCENE_ROW_SIZE;
  }

  uiDrawModalPrompt(gsGlobal, vx1, vx2, 0, vy2, zOrder, HeaderTextColor, ICON_CROSS, "Edit");
  uiDrawModalPrompt(gsGlobal, vx1, vx2, 1, vy2, zOrder, HeaderTextColor, ICON_CIRCLE, "Back");
}

static ViewResult optionsPopupOnInput(View *v, int padInput) {
  if (padInput & PAD_CIRCLE) {
    s_subview = MenuSubview_Main;
    s_menuSelection = MAIN_MENU_NHDDL_OPTIONS;
    v->draw = menuDraw;
    v->onInput = menuOnInput;
    return ViewResult_Continue;
  }
  if (padInput & PAD_UP) {
    s_menuSelection--;
    if (s_menuSelection < 0)
      s_menuSelection = OPTIONS_COUNT - 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    s_menuSelection++;
    if (s_menuSelection >= OPTIONS_COUNT)
      s_menuSelection = 0;
    return ViewResult_Continue;
  }
  if (padInput & PAD_CROSS) {
    if (s_menuSelection == 0) {
      s_subview = MenuSubview_VideoMode;
      s_menuSelection = 0;
      v->draw = videoModePopupDraw;
      v->onInput = videoModePopupOnInput;
    } else if (s_menuSelection == 1) {
      s_subview = MenuSubview_ProbeDelay;
      s_menuSelection = findChoiceIndex(s_probeDelayChoices, PROBE_DELAY_COUNT, s_pendingProbeDelay);
      v->draw = probeDelayPopupDraw;
      v->onInput = probeDelayPopupOnInput;
    } else if (s_menuSelection == 2) {
      s_subview = MenuSubview_Autolaunch;
      s_menuSelection = findChoiceIndex(s_autolaunchChoices, AUTOLAUNCH_COUNT, s_pendingAutolaunch);
      v->draw = autolaunchPopupDraw;
      v->onInput = autolaunchPopupOnInput;
    } else if (s_menuSelection == 3) {
      fileSelectorConfigure(s_pendingNeutrinoPath, onNeutrinoPathSelected, NULL);
      viewStackPush(viewStack, fileSelectorGetView());
    }
  }
  return ViewResult_Continue;
}

static void videoModePopupDraw(View *v, int zOrder) {
  (void)v;
  static const char *rows[] = {"NTSC (480i)", "PAL (576i)", "480p", "720p", "Widescreen"};
  const int rowCount = (int)(sizeof(rows) / sizeof(rows[0]));
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (rowCount + 2)) + POPUP_PROMPT_ROW_OFFSET;
  const int vx1 = POPUP_X, vy1 = (SCENE_VH - POPUP_H) / 2;
  const int vx2 = POPUP_X + POPUP_W, vy2 = vy1 + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;
  const int leftX = vx1 + SCENE_KEEPOUT;
  const int rightX = vx2 - SCENE_KEEPOUT;

  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  fontRenderString(FONT_DEFAULT, centerX, vy1 + SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, "Video mode", HeaderTextColor);

  int entryY = vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE;
  for (int i = 0; i < rowCount; i++) {
    int on = 0;
    if (i < 4)
      on = (s_pendingVMode == (VModeType[]){VMode_480i, VMode_576i, VMode_480p, VMode_720p}[i]);
    else
      on = s_pendingWidescreen;
    uint64_t col = (i == s_menuSelection) ? ColorSelected : FontMainColor;
    uiDrawSelectionIndicator(gsGlobal, leftX + INDICATOR_R, entryY + SCENE_ROW_SIZE / 2, INDICATOR_R, zOrder, col, on);
    fontRenderInRect(FONT_DEFAULT, leftX + INDICATOR_R * 4, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1,
                     rows[i], col);
    entryY += SCENE_ROW_SIZE;
  }

  uiDrawModalPrompt(gsGlobal, vx1, vx2, 0, vy2, zOrder, HeaderTextColor, ICON_CROSS, "Set");
  uiDrawModalPrompt(gsGlobal, vx1, vx2, 1, vy2, zOrder, HeaderTextColor, ICON_CIRCLE, "Back");
}

static ViewResult videoModePopupOnInput(View *v, int padInput) {
  if (padInput & PAD_CIRCLE) {
    s_subview = MenuSubview_NHDDLOptions;
    s_menuSelection = 0;
    v->draw = optionsPopupDraw;
    v->onInput = optionsPopupOnInput;
    return ViewResult_Continue;
  }
  if (padInput & PAD_UP) {
    s_menuSelection--;
    if (s_menuSelection < 0)
      s_menuSelection = 4;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    s_menuSelection++;
    if (s_menuSelection > 4)
      s_menuSelection = 0;
    return ViewResult_Continue;
  }
  if (padInput & (PAD_CROSS | PAD_LEFT | PAD_RIGHT)) {
    if (s_menuSelection < 4) {
      static const VModeType modes[] = {VMode_480i, VMode_576i, VMode_480p, VMode_720p};
      s_pendingVMode = modes[s_menuSelection];
    } else {
      s_pendingWidescreen = !s_pendingWidescreen;
    }
  }
  return ViewResult_Continue;
}

static void drawChoicesPopup(const char *title, const int *values, int count, int selected, int zOrder) {
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (count + 2)) + POPUP_PROMPT_ROW_OFFSET;
  const int vx1 = POPUP_X, vy1 = (SCENE_VH - POPUP_H) / 2;
  const int vx2 = POPUP_X + POPUP_W, vy2 = vy1 + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;
  const int leftX = vx1 + SCENE_KEEPOUT;
  const int rightX = vx2 - SCENE_KEEPOUT;

  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  fontRenderString(FONT_DEFAULT, centerX, vy1 + SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, title, HeaderTextColor);

  int entryY = vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE;
  for (int i = 0; i < count; i++) {
    char line[32];
    if (values[i] > 0)
      snprintf(line, sizeof(line), "%d s", values[i]);
    else
      snprintf(line, sizeof(line), "Off");
    uint64_t col = (i == selected) ? ColorSelected : FontMainColor;
    uiDrawSelectionIndicator(gsGlobal, leftX + INDICATOR_R, entryY + SCENE_ROW_SIZE / 2, INDICATOR_R, zOrder, col, i == selected);
    fontRenderInRect(FONT_DEFAULT, leftX + INDICATOR_R * 4, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1,
                     line, col);
    entryY += SCENE_ROW_SIZE;
  }
  uiDrawModalPrompt(gsGlobal, vx1, vx2, 0, vy2, zOrder, HeaderTextColor, ICON_CROSS, "Set");
  uiDrawModalPrompt(gsGlobal, vx1, vx2, 1, vy2, zOrder, HeaderTextColor, ICON_CIRCLE, "Back");
}

static void probeDelayPopupDraw(View *v, int zOrder) {
  (void)v;
  drawChoicesPopup("Probe delay", s_probeDelayChoices, PROBE_DELAY_COUNT, s_menuSelection, zOrder);
}

static ViewResult probeDelayPopupOnInput(View *v, int padInput) {
  if (padInput & PAD_CIRCLE) {
    s_subview = MenuSubview_NHDDLOptions;
    s_menuSelection = 1;
    v->draw = optionsPopupDraw;
    v->onInput = optionsPopupOnInput;
    return ViewResult_Continue;
  }
  if (padInput & PAD_UP) {
    s_menuSelection--;
    if (s_menuSelection < 0)
      s_menuSelection = PROBE_DELAY_COUNT - 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    s_menuSelection++;
    if (s_menuSelection >= PROBE_DELAY_COUNT)
      s_menuSelection = 0;
    return ViewResult_Continue;
  }
  if (padInput & PAD_CROSS)
    s_pendingProbeDelay = s_probeDelayChoices[s_menuSelection];
  return ViewResult_Continue;
}

static void autolaunchPopupDraw(View *v, int zOrder) {
  (void)v;
  drawChoicesPopup("Autolaunch", s_autolaunchChoices, AUTOLAUNCH_COUNT, s_menuSelection, zOrder);
}

static ViewResult autolaunchPopupOnInput(View *v, int padInput) {
  if (padInput & PAD_CIRCLE) {
    s_subview = MenuSubview_NHDDLOptions;
    s_menuSelection = 2;
    v->draw = optionsPopupDraw;
    v->onInput = optionsPopupOnInput;
    return ViewResult_Continue;
  }
  if (padInput & PAD_UP) {
    s_menuSelection--;
    if (s_menuSelection < 0)
      s_menuSelection = AUTOLAUNCH_COUNT - 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    s_menuSelection++;
    if (s_menuSelection >= AUTOLAUNCH_COUNT)
      s_menuSelection = 0;
    return ViewResult_Continue;
  }
  if (padInput & PAD_CROSS)
    s_pendingAutolaunch = s_autolaunchChoices[s_menuSelection];
  return ViewResult_Continue;
}
