#include "ui/views/menu_scene.h"
#include "backends/backends.h"
#include "backends/cache.h"
#include "config/config.h"
#include "devices/devices.h"
#include "devices/pad.h"
#include "devices/utils.h"
#include "ui/draw.h"
#include "ui/folder_view.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/popup_list.h"
#include "ui/strings.h"
#include "ui/ui_context.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/exit_modal.h"
#include "ui/views/file_selector_scene.h"
#include "ui/views/main_scene.h"
#include "ui/worker/worker.h"
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static View s_view;
static int s_menuSelection = 0;
static uint32_t enabledDeviceTypes = 0;
static uint32_t s_deviceMaskAtSubmenuOpen = 0;
static DeviceType deviceTypes[MAX_DEVICES] = {0};
static uint8_t deviceCount = 0;
static VModeType s_pendingVMode = VMode_NONE;
static int s_pendingWidescreen = 0;
static int s_pendingFolderView = 0;
static int s_pendingProbeDelay = 0;
static int s_pendingAutolaunch = 0;
static char s_pendingNeutrinoPath[PATH_MAX + 1];
static char s_pendingLang[8];

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
static void languagePopupDraw(View *v, int zOrder);
static ViewResult languagePopupOnInput(View *v, int padInput);
static void menuDraw(View *v, int zOrder);

static const int s_probeDelayChoices[] = {0, 3, 5, 10, 15, 20};
static const int s_autolaunchChoices[] = {0, 3, 5, 10, 15, 30};
#define PROBE_DELAY_COUNT ((int)(sizeof(s_probeDelayChoices) / sizeof(s_probeDelayChoices[0])))
#define AUTOLAUNCH_COUNT ((int)(sizeof(s_autolaunchChoices) / sizeof(s_autolaunchChoices[0])))

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
    return uiStr(UI_STR_NTSC_480I);
  case VMode_576i:
    return uiStr(UI_STR_PAL_576I);
  case VMode_480p:
    return uiStr(UI_STR_MODE_480P);
  case VMode_576p:
    return uiStr(UI_STR_MODE_576P);
  case VMode_720p:
    return uiStr(UI_STR_MODE_720P);
  default:
    return uiStr(UI_STR_SYSTEM_DEFAULT);
  }
}

static void onNeutrinoPathSelected(const char *path, void *userdata) {
  if (!path || !path[0])
    return;
  snprintf(s_pendingNeutrinoPath, sizeof(s_pendingNeutrinoPath), "%s", path);
  showOSD(uiStr(UI_STR_NEUTRINO_PATH_UPDATED), 120);
}

static void applyAndSaveSettings(MainSceneData *d) {
  UiContext *ctx = uiGetContext();
  DeviceType prevMask = getEnabledDevices();
  ctx->enabledDevices = enabledDeviceTypes;
  setEnabledDevices(enabledDeviceTypes);
  if (s_pendingVMode == VMode_720p)
    s_pendingWidescreen = 1;
  setVMode(s_pendingVMode);
  setWidescreen(s_pendingWidescreen);
  setFolderView(s_pendingFolderView);
  setProbeDelay(s_pendingProbeDelay);
  setAutolaunchTimeout(s_pendingAutolaunch);
  setNeutrinoPath(s_pendingNeutrinoPath);
  setLang(s_pendingLang);
  setUiLanguageByCode(s_pendingLang);
  uiApplyDisplaySettings();

  if (d && d->folderMode != s_pendingFolderView) {
    d->folderMode = s_pendingFolderView;
    if (d->folderMode)
      folderViewResetRoot(&d->folderView);
    d->selectedIndex = 0;
    d->scrollOffset = 0;
    d->listDirty = 1;
  }

  if (d && prevMask != enabledDeviceTypes) {
    d->rebuildSelectionTarget = NULL;
    mainSceneBeginBackendResync(d);
    ctx->pendingSaveAfterRescan = 1;
    if (workerRequest(WorkerJob_Rescan) != 0) {
      ctx->pendingSaveAfterRescan = 0;
      ctx->listUpdating = 0;
      showOSD(uiStr(UI_STR_RESCAN_BUSY), 120);
    } else {
      showOSD(uiStr(UI_STR_UPDATING_DEVICES), 120);
    }
    return;
  }

  if (workerRequest(WorkerJob_SaveConfig) != 0)
    showOSD(uiStr(UI_STR_SAVE_BUSY), 120);
}

static void menuOnEnter(View *v) {
  UiContext *ctx = uiGetContext();
  int coldOpen = (v->draw == menuDraw);
  enabledDeviceTypes = getEnabledDevices();
  ctx->enabledDevices = enabledDeviceTypes;
  deviceCount = getSupportedDeviceTypesArray(deviceTypes, MAX_DEVICES);
  // Unsaved NHDDL options edits (incl. Neutrino path from file picker) must survive popping the file selector.
  if (coldOpen) {
    s_pendingVMode = getVMode();
    s_pendingWidescreen = getWidescreen();
    s_pendingFolderView = getFolderView();
    s_pendingProbeDelay = getProbeDelay();
    s_pendingAutolaunch = getAutolaunchTimeout();
    snprintf(s_pendingNeutrinoPath, sizeof(s_pendingNeutrinoPath), "%s", getNeutrinoPath());
    snprintf(s_pendingLang, sizeof(s_pendingLang), "%s", getLang());
    s_menuSelection = 0;
  }
}

#define POPUP_W 240
#define POPUP_X (SCENE_VW - POPUP_W) / 2
#define ENTRY_COUNT 6
#define MAIN_MENU_NHDDL_OPTIONS 1

static const char *mainMenuEntry(int i) {
  switch (i) {
  case 0:
    return uiStr(UI_STR_ENABLED_DEVICES);
  case 1:
    return uiStr(UI_STR_NHDDL_OPTIONS);
  case 2:
    return uiStr(UI_STR_SAVE_AND_APPLY);
  case 3:
    return uiStr(UI_STR_RELOAD_LIST);
  case 4:
    return uiStr(UI_STR_INVALIDATE_CACHE);
  default:
    return uiStr(UI_STR_EXIT);
  }
}

static void menuDraw(View *v, int zOrder) {
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (ENTRY_COUNT + 2));
  const int vx1 = POPUP_X, vy1 = (SCENE_VH - POPUP_H) / 2;
  const int vx2 = POPUP_X + POPUP_W, vy2 = vy1 + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;

  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  fontRenderString(FONT_DEFAULT, centerX, vy1 + SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, uiStr(UI_STR_MAIN_MENU), HeaderTextColor);
  int optionY = vy1 + SCENE_KEEPOUT + SCENE_ROW_SIZE * 1.5;
  for (int i = 0; i < ENTRY_COUNT; i++) {
    uint64_t col = (i == s_menuSelection) ? ColorSelected : FontMainColor;
    fontRenderString(FONT_DEFAULT, centerX, optionY, FONT_ALIGN_HCENTER, 0, 0, zOrder, mainMenuEntry(i), col);
    optionY += SCENE_ROW_SIZE;
  }
}

static ViewResult menuOnInput(View *v, int padInput) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  UiContext *ctx = uiGetContext();
  if (ctx->exitRequested)
    return ViewResult_Pop;

  if (padInput & PAD_TRIANGLE)
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
  if (padInput & PAD_SELECT_ACTION) {
    switch (s_menuSelection) {
    case 0:
      s_deviceMaskAtSubmenuOpen = enabledDeviceTypes;
      s_menuSelection = 0;
      v->draw = deviceSelectorPopupDraw;
      v->onInput = deviceSelectorPopupOnInput;
      break;
    case MAIN_MENU_NHDDL_OPTIONS:
      s_menuSelection = 0;
      v->draw = optionsPopupDraw;
      v->onInput = optionsPopupOnInput;
      break;
    case 2:
      applyAndSaveSettings(d);
      break;
    case 3:
      d->rebuildSelectionTarget = NULL;
      mainSceneBeginBackendResync(d);
      if (workerRequest(WorkerJob_Rescan) != 0) {
        ctx->listUpdating = 0;
        showOSD(uiStr(UI_STR_RELOAD_BUSY), 120);
      } else {
        showOSD(uiStr(UI_STR_RELOADING), 120);
      }
      break;
    case 4:
      if (workerRequest(WorkerJob_InvalidateCache) != 0)
        showOSD(uiStr(UI_STR_INVALIDATE_BUSY), 120);
      break;
    case 5:
      exitModalGetView()->userdata = d;
      viewStackPush(viewStack, exitModalGetView());
      break;
    }
  }
  return ViewResult_Continue;
}

static void menuOnLeave(View *v) {
  // Revert selected language when leaving the menu without Save and apply.
  setUiLanguageByCode(getLang());
}

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

static PopupListItem s_deviceItems[MAX_DEVICES];

static int deviceSelectorIsOn(int index, void *userdata) {
  if (index < 0 || index >= deviceCount)
    return 0;
  return (enabledDeviceTypes & deviceTypes[index]) != 0;
}

static void deviceSelectorActivate(int index, View *v, void *userdata) {
  if (index < 0 || index >= deviceCount)
    return;
  if (deviceTypes[index] == Device_MMCE)
    enabledDeviceTypes &= ~Device_MX4SIO;
  else if (deviceTypes[index] == Device_MX4SIO)
    enabledDeviceTypes &= ~Device_MMCE;
  enabledDeviceTypes ^= deviceTypes[index];
  uiGetContext()->enabledDevices = enabledDeviceTypes;
}

static ViewResult deviceSelectorOnBack(View *v, void *userdata) {
  MainSceneData *d = (MainSceneData *)userdata;
  UiContext *ctx = uiGetContext();
  if (enabledDeviceTypes != s_deviceMaskAtSubmenuOpen) {
    ctx->enabledDevices = enabledDeviceTypes;
    setEnabledDevices(enabledDeviceTypes);
    if (d) {
      d->rebuildSelectionTarget = NULL;
      mainSceneBeginBackendResync(d);
      if (workerRequest(WorkerJob_Rescan) != 0) {
        ctx->listUpdating = 0;
        showOSD(uiStr(UI_STR_UPDATE_BUSY), 120);
      } else {
        showOSD(uiStr(UI_STR_UPDATING_DEVICES), 120);
      }
    }
  }
  v->draw = menuDraw;
  v->onInput = menuOnInput;
  return ViewResult_Continue;
}

static PopupListDesc makeDeviceSelectorDesc(View *v) {
  for (int i = 0; i < deviceCount; i++) {
    s_deviceItems[i].label = getDeviceString(deviceTypes[i]);
    s_deviceItems[i].getValue = NULL;
    s_deviceItems[i].onActivate = deviceSelectorActivate;
    s_deviceItems[i].onAdjust = NULL;
  }
  PopupListDesc desc = {
      .title = uiStr(UI_STR_DEVICES),
      .style = PopupList_Choices,
      .items = s_deviceItems,
      .count = deviceCount,
      .selection = &s_menuSelection,
      .userdata = v ? v->userdata : NULL,
      .isOn = deviceSelectorIsOn,
      .onBack = deviceSelectorOnBack,
  };
  return desc;
}

static void deviceSelectorPopupDraw(View *v, int zOrder) {
  PopupListDesc desc = makeDeviceSelectorDesc(v);
  popupListDraw(&desc, zOrder);
}

static ViewResult deviceSelectorPopupOnInput(View *v, int padInput) {
  PopupListDesc desc = makeDeviceSelectorDesc(v);
  return popupListOnInput(&desc, v, padInput);
}

//
// NHDDL options + sub-popups via PopupList
//

static ViewResult optionsBackToMain(View *v, void *userdata) {
  s_menuSelection = MAIN_MENU_NHDDL_OPTIONS;
  v->draw = menuDraw;
  v->onInput = menuOnInput;
  return ViewResult_Continue;
}

static ViewResult choiceBackToOptions(View *v, void *userdata) {
  int restoreSel = (int)(intptr_t)userdata;
  s_menuSelection = restoreSel;
  v->draw = optionsPopupDraw;
  v->onInput = optionsPopupOnInput;
  return ViewResult_Continue;
}

static void optionsGetValue(int index, char *buf, size_t bufSize, void *userdata) {
  switch (index) {
  case 0:
    snprintf(buf, bufSize, "%s, %s", videoModeToLabel(s_pendingVMode), s_pendingWidescreen ? "16:9" : "4:3");
    break;
  case 1:
    if (s_pendingProbeDelay > 0)
      snprintf(buf, bufSize, "%d s", s_pendingProbeDelay);
    else
      snprintf(buf, bufSize, "%s", uiStr(UI_STR_OFF));
    break;
  case 2:
    if (s_pendingAutolaunch > 0)
      snprintf(buf, bufSize, "%d s", s_pendingAutolaunch);
    else
      snprintf(buf, bufSize, "%s", uiStr(UI_STR_OFF));
    break;
  case 3:
    uiTrimPathMiddle(s_pendingNeutrinoPath, buf, bufSize, uiStr(UI_STR_NOT_SET));
    break;
  case 4: {
    int idx = 0;
    for (int i = 0; i < uiLanguageCount(); i++) {
      if (!strcmp(uiLanguageCode(i), s_pendingLang)) {
        idx = i;
        break;
      }
    }
    snprintf(buf, bufSize, "%s", uiLanguageName(idx));
    break;
  }
  case 5:
    snprintf(buf, bufSize, "%s", s_pendingFolderView ? uiStr(UI_STR_YES) : uiStr(UI_STR_NO));
    break;
  default:
    buf[0] = '\0';
    break;
  }
}

static void optionsActivate(int index, View *v, void *userdata) {
  if (index == 0) {
    s_menuSelection = 0;
    v->draw = videoModePopupDraw;
    v->onInput = videoModePopupOnInput;
  } else if (index == 1) {
    s_menuSelection = findChoiceIndex(s_probeDelayChoices, PROBE_DELAY_COUNT, s_pendingProbeDelay);
    v->draw = probeDelayPopupDraw;
    v->onInput = probeDelayPopupOnInput;
  } else if (index == 2) {
    s_menuSelection = findChoiceIndex(s_autolaunchChoices, AUTOLAUNCH_COUNT, s_pendingAutolaunch);
    v->draw = autolaunchPopupDraw;
    v->onInput = autolaunchPopupOnInput;
  } else if (index == 3) {
    fileSelectorConfigure(s_pendingNeutrinoPath, onNeutrinoPathSelected, NULL, NULL);
    fileSelectorEnableNeutrinoHeadings();
    viewStackPush(viewStack, fileSelectorGetView());
  } else if (index == 4) {
    s_menuSelection = 0;
    for (int i = 0; i < uiLanguageCount(); i++) {
      if (!strcmp(uiLanguageCode(i), s_pendingLang)) {
        s_menuSelection = i;
        break;
      }
    }
    v->draw = languagePopupDraw;
    v->onInput = languagePopupOnInput;
  }
}

static void optionsAdjust(int index, int dir, void *userdata) {
  if (index == 5)
    s_pendingFolderView = !s_pendingFolderView;
}

static PopupListItem s_optionsItems[6];

static PopupListDesc makeOptionsDesc(void) {
  s_optionsItems[0] = (PopupListItem){uiStr(UI_STR_VIDEO_MODE), optionsGetValue, optionsActivate, NULL};
  s_optionsItems[1] = (PopupListItem){uiStr(UI_STR_PROBE_DELAY), optionsGetValue, optionsActivate, NULL};
  s_optionsItems[2] = (PopupListItem){uiStr(UI_STR_AUTOLAUNCH), optionsGetValue, optionsActivate, NULL};
  s_optionsItems[3] = (PopupListItem){uiStr(UI_STR_NEUTRINO_PATH), optionsGetValue, optionsActivate, NULL};
  s_optionsItems[4] = (PopupListItem){uiStr(UI_STR_LANGUAGE), optionsGetValue, optionsActivate, NULL};
  s_optionsItems[5] = (PopupListItem){uiStr(UI_STR_FOLDER_VIEW), optionsGetValue, NULL, optionsAdjust};
  PopupListDesc desc = {
      .title = uiStr(UI_STR_NHDDL_OPTIONS),
      .style = PopupList_ValueRows,
      .items = s_optionsItems,
      .count = 6,
      .selection = &s_menuSelection,
      .userdata = NULL,
      .isOn = NULL,
      .onBack = optionsBackToMain,
  };
  return desc;
}

static void optionsPopupDraw(View *v, int zOrder) {
  PopupListDesc desc = makeOptionsDesc();
  popupListDraw(&desc, zOrder);
}

static ViewResult optionsPopupOnInput(View *v, int padInput) {
  PopupListDesc desc = makeOptionsDesc();
  return popupListOnInput(&desc, v, padInput);
}

static const VModeType s_vmodeChoices[] = {VMode_NONE, VMode_480i, VMode_576i, VMode_480p, VMode_576p, VMode_720p};
#define VMODE_RES_COUNT 6
#define VMODE_CHOICE_COUNT 7

static int videoModeIsOn(int index, void *userdata) {
  if (index < VMODE_RES_COUNT)
    return s_pendingVMode == s_vmodeChoices[index];
  return s_pendingWidescreen;
}

static void videoModeActivate(int index, View *v, void *userdata) {
  if (index < VMODE_RES_COUNT) {
    s_pendingVMode = s_vmodeChoices[index];
    if (s_pendingVMode == VMode_720p)
      s_pendingWidescreen = 1;
  } else if (s_pendingVMode != VMode_720p) {
    s_pendingWidescreen = !s_pendingWidescreen;
  }
}

static PopupListItem s_videoModeItems[VMODE_CHOICE_COUNT];

static PopupListDesc makeVideoModeDesc(void) {
  s_videoModeItems[0] = (PopupListItem){uiStr(UI_STR_SYSTEM_DEFAULT), NULL, videoModeActivate, NULL};
  s_videoModeItems[1] = (PopupListItem){uiStr(UI_STR_NTSC_480I), NULL, videoModeActivate, NULL};
  s_videoModeItems[2] = (PopupListItem){uiStr(UI_STR_PAL_576I), NULL, videoModeActivate, NULL};
  s_videoModeItems[3] = (PopupListItem){uiStr(UI_STR_MODE_480P), NULL, videoModeActivate, NULL};
  s_videoModeItems[4] = (PopupListItem){uiStr(UI_STR_MODE_576P), NULL, videoModeActivate, NULL};
  s_videoModeItems[5] = (PopupListItem){uiStr(UI_STR_MODE_720P), NULL, videoModeActivate, NULL};
  s_videoModeItems[6] = (PopupListItem){uiStr(UI_STR_WIDESCREEN), NULL, videoModeActivate, NULL};
  PopupListDesc desc = {
      .title = uiStr(UI_STR_VIDEO_MODE),
      .style = PopupList_Choices,
      .items = s_videoModeItems,
      .count = VMODE_CHOICE_COUNT,
      .selection = &s_menuSelection,
      .userdata = (void *)(intptr_t)0,
      .isOn = videoModeIsOn,
      .onBack = choiceBackToOptions,
  };
  return desc;
}

static void videoModePopupDraw(View *v, int zOrder) {
  PopupListDesc desc = makeVideoModeDesc();
  popupListDraw(&desc, zOrder);
}

static ViewResult videoModePopupOnInput(View *v, int padInput) {
  PopupListDesc desc = makeVideoModeDesc();
  return popupListOnInput(&desc, v, padInput);
}

static void probeDelayActivate(int index, View *v, void *userdata) {
  if (index >= 0 && index < PROBE_DELAY_COUNT)
    s_pendingProbeDelay = s_probeDelayChoices[index];
}

static void autolaunchActivate(int index, View *v, void *userdata) {
  if (index >= 0 && index < AUTOLAUNCH_COUNT)
    s_pendingAutolaunch = s_autolaunchChoices[index];
}

static PopupListItem s_probeDelayItems[PROBE_DELAY_COUNT];
static PopupListItem s_autolaunchItems[AUTOLAUNCH_COUNT];

static const char *probeDelayLabel(int i) {
  switch (i) {
  case 0:
    return uiStr(UI_STR_OFF);
  case 1:
    return uiStr(UI_STR_SEC_3);
  case 2:
    return uiStr(UI_STR_SEC_5);
  case 3:
    return uiStr(UI_STR_SEC_10);
  case 4:
    return uiStr(UI_STR_SEC_15);
  default:
    return uiStr(UI_STR_SEC_20);
  }
}

static const char *autolaunchLabel(int i) {
  switch (i) {
  case 0:
    return uiStr(UI_STR_OFF);
  case 1:
    return uiStr(UI_STR_SEC_3);
  case 2:
    return uiStr(UI_STR_SEC_5);
  case 3:
    return uiStr(UI_STR_SEC_10);
  case 4:
    return uiStr(UI_STR_SEC_15);
  default:
    return uiStr(UI_STR_SEC_30);
  }
}

static PopupListDesc makeProbeDelayDesc(void) {
  for (int i = 0; i < PROBE_DELAY_COUNT; i++) {
    s_probeDelayItems[i].label = probeDelayLabel(i);
    s_probeDelayItems[i].getValue = NULL;
    s_probeDelayItems[i].onActivate = probeDelayActivate;
    s_probeDelayItems[i].onAdjust = NULL;
  }
  PopupListDesc desc = {
      .title = uiStr(UI_STR_PROBE_DELAY),
      .style = PopupList_Choices,
      .items = s_probeDelayItems,
      .count = PROBE_DELAY_COUNT,
      .selection = &s_menuSelection,
      .userdata = (void *)(intptr_t)1,
      .isOn = NULL,
      .onBack = choiceBackToOptions,
  };
  return desc;
}

static PopupListDesc makeAutolaunchDesc(void) {
  for (int i = 0; i < AUTOLAUNCH_COUNT; i++) {
    s_autolaunchItems[i].label = autolaunchLabel(i);
    s_autolaunchItems[i].getValue = NULL;
    s_autolaunchItems[i].onActivate = autolaunchActivate;
    s_autolaunchItems[i].onAdjust = NULL;
  }
  PopupListDesc desc = {
      .title = uiStr(UI_STR_AUTOLAUNCH),
      .style = PopupList_Choices,
      .items = s_autolaunchItems,
      .count = AUTOLAUNCH_COUNT,
      .selection = &s_menuSelection,
      .userdata = (void *)(intptr_t)2,
      .isOn = NULL,
      .onBack = choiceBackToOptions,
  };
  return desc;
}

static void probeDelayPopupDraw(View *v, int zOrder) {
  PopupListDesc desc = makeProbeDelayDesc();
  popupListDraw(&desc, zOrder);
}

static ViewResult probeDelayPopupOnInput(View *v, int padInput) {
  PopupListDesc desc = makeProbeDelayDesc();
  return popupListOnInput(&desc, v, padInput);
}

static void autolaunchPopupDraw(View *v, int zOrder) {
  PopupListDesc desc = makeAutolaunchDesc();
  popupListDraw(&desc, zOrder);
}

static ViewResult autolaunchPopupOnInput(View *v, int padInput) {
  PopupListDesc desc = makeAutolaunchDesc();
  return popupListOnInput(&desc, v, padInput);
}

#define LANG_ITEM_MAX 16
static PopupListItem s_languageItems[LANG_ITEM_MAX];

static int languageIsOn(int index, void *userdata) {
  if (index < 0 || index >= uiLanguageCount())
    return 0;
  return !strcmp(uiLanguageCode(index), s_pendingLang);
}

static void languageActivate(int index, View *v, void *userdata) {
  if (index < 0 || index >= uiLanguageCount())
    return;
  snprintf(s_pendingLang, sizeof(s_pendingLang), "%s", uiLanguageCode(index));
  setUiLanguageByCode(s_pendingLang);
}

static PopupListDesc makeLanguageDesc(void) {
  int n = uiLanguageCount();
  if (n > LANG_ITEM_MAX)
    n = LANG_ITEM_MAX;
  for (int i = 0; i < n; i++) {
    s_languageItems[i].label = uiLanguageName(i);
    s_languageItems[i].getValue = NULL;
    s_languageItems[i].onActivate = languageActivate;
    s_languageItems[i].onAdjust = NULL;
  }
  PopupListDesc desc = {
      .title = uiStr(UI_STR_LANGUAGE),
      .style = PopupList_Choices,
      .items = s_languageItems,
      .count = n,
      .selection = &s_menuSelection,
      .userdata = (void *)(intptr_t)4,
      .isOn = languageIsOn,
      .onBack = choiceBackToOptions,
  };
  return desc;
}

static void languagePopupDraw(View *v, int zOrder) {
  PopupListDesc desc = makeLanguageDesc();
  popupListDraw(&desc, zOrder);
}

static ViewResult languagePopupOnInput(View *v, int padInput) {
  PopupListDesc desc = makeLanguageDesc();
  return popupListOnInput(&desc, v, padInput);
}
