#include "ui/views/view_options_popup.h"
#include "backends/backends.h"
#include "devices/utils.h"
#include "ui/font.h"
#include "ui/popup_list.h"
#include "ui/strings.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/main_scene.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static View s_view;
static int s_viewOptionsSelection;
static uint32_t s_savedFilterMask;

static void deviceFilterPopupDraw(View *v, int zOrder);
static ViewResult deviceFilterPopupOnInput(View *v, int padInput);
static void viewOptionsPopupDraw(View *v, int zOrder);
static ViewResult viewOptionsPopupOnInput(View *v, int padInput);

static void viewOptionsPopupOnEnter(View *v) {
  s_viewOptionsSelection = 0;
}

static void viewOptionsPopupOnLeave(View *v) { }

static void viewOptionsGetValue(int index, char *buf, size_t bufSize, void *userdata) {
  MainSceneData *d = (MainSceneData *)userdata;
  if (!d) {
    buf[0] = '\0';
    return;
  }
  switch (index) {
  case 0:
    if (d->filterDeviceMask == 0) {
      snprintf(buf, bufSize, "%s", uiStr(UI_STR_ALL));
    } else {
      int sel = 0;
      for (int i = 0; i < getBackendDeviceCount(); i++)
        if (d->filterDeviceMask & (1 << i))
          sel++;
      snprintf(buf, bufSize, "%d", sel);
    }
    break;
  case 1:
    snprintf(buf, bufSize, "%s", d->favoritesOnly ? uiStr(UI_STR_YES) : uiStr(UI_STR_NO));
    break;
  case 2:
    snprintf(buf, bufSize, "%s", d->favoritesOnTop ? uiStr(UI_STR_YES) : uiStr(UI_STR_NO));
    break;
  case 3:
    snprintf(buf, bufSize, "%s", d->sortAscending ? uiStr(UI_STR_ASCENDING) : uiStr(UI_STR_DESCENDING));
    break;
  default:
    buf[0] = '\0';
    break;
  }
}

static void viewOptionsActivate(int index, View *v, void *userdata) {
  MainSceneData *d = (MainSceneData *)userdata;
  if (index == 0 && d) {
    s_savedFilterMask = d->filterDeviceMask;
    s_viewOptionsSelection = 0;
    v->draw = deviceFilterPopupDraw;
    v->onInput = deviceFilterPopupOnInput;
  }
}

static void viewOptionsAdjust(int index, int dir, void *userdata) {
  MainSceneData *d = (MainSceneData *)userdata;
  if (!d)
    return;
  if (index == 1) {
    d->favoritesOnly = !d->favoritesOnly;
    d->listDirty = 1;
  } else if (index == 2) {
    d->favoritesOnTop = !d->favoritesOnTop;
    d->listDirty = 1;
  } else if (index == 3) {
    d->sortAscending = !d->sortAscending;
    d->listDirty = 1;
  }
}

static PopupListItem s_viewOptionsItems[4];

static PopupListDesc makeViewOptionsDesc(MainSceneData *d) {
  s_viewOptionsItems[0] = (PopupListItem){uiStr(UI_STR_DEVICES), viewOptionsGetValue, viewOptionsActivate, NULL};
  s_viewOptionsItems[1] = (PopupListItem){uiStr(UI_STR_FAVORITES_ONLY), viewOptionsGetValue, NULL, viewOptionsAdjust};
  s_viewOptionsItems[2] = (PopupListItem){uiStr(UI_STR_FAVORITES_FIRST), viewOptionsGetValue, NULL, viewOptionsAdjust};
  s_viewOptionsItems[3] = (PopupListItem){uiStr(UI_STR_SORT), viewOptionsGetValue, NULL, viewOptionsAdjust};
  PopupListDesc desc = {
      .title = uiStr(UI_STR_VIEW_OPTIONS),
      .style = PopupList_ValueRows,
      .items = s_viewOptionsItems,
      .count = 4,
      .selection = &s_viewOptionsSelection,
      .userdata = d,
      .isOn = NULL,
      .onBack = NULL,
  };
  return desc;
}

static void viewOptionsPopupDraw(View *v, int zOrder) {
  PopupListDesc desc = makeViewOptionsDesc((MainSceneData *)v->userdata);
  popupListDraw(&desc, zOrder);
}

static ViewResult viewOptionsPopupOnInput(View *v, int padInput) {
  PopupListDesc desc = makeViewOptionsDesc((MainSceneData *)v->userdata);
  return popupListOnInput(&desc, v, padInput);
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

static PopupListItem s_filterItems[MAX_DEVICES + 1];
static char s_filterLabels[MAX_DEVICES + 1][24];

static void formatDeviceName(struct BackendDevice *dev, char *buf, size_t bufSize) {
  if (!dev || !buf || bufSize == 0)
    return;
  char *typeStr = getDeviceString(dev->type);
  snprintf(buf, bufSize, "%s %d", typeStr, (int)dev->index);
}

static int deviceFilterIsOn(int index, void *userdata) {
  MainSceneData *d = (MainSceneData *)userdata;
  if (!d)
    return 0;
  if (index == 0)
    return d->filterDeviceMask == 0;
  int n = getBackendDeviceCount();
  int devIdx = index - 1;
  if (devIdx < 0 || devIdx >= n)
    return 0;
  return (d->filterDeviceMask & (1u << (unsigned)devIdx)) != 0;
}

static void deviceFilterActivate(int index, View *v, void *userdata) {
  MainSceneData *d = (MainSceneData *)userdata;
  if (!d)
    return;
  int n = getBackendDeviceCount();
  if (index == 0) {
    d->filterDeviceMask = 0;
    return;
  }
  int devIdx = index - 1;
  if (devIdx >= 0 && devIdx < n)
    d->filterDeviceMask ^= (1u << (unsigned)devIdx);
}

static ViewResult deviceFilterOnBack(View *v, void *userdata) {
  MainSceneData *d = (MainSceneData *)userdata;
  s_viewOptionsSelection = 0;
  v->draw = viewOptionsPopupDraw;
  v->onInput = viewOptionsPopupOnInput;
  if (d && d->filterDeviceMask != s_savedFilterMask)
    d->listDirty = 1;
  return ViewResult_Continue;
}

static PopupListDesc makeDeviceFilterDesc(MainSceneData *d) {
  int n = getBackendDeviceCount();
  int count = 1 + n;
  if (count > MAX_DEVICES + 1)
    count = MAX_DEVICES + 1;

  snprintf(s_filterLabels[0], sizeof(s_filterLabels[0]), "%s", uiStr(UI_STR_ALL_DEVICES));
  s_filterItems[0].label = s_filterLabels[0];
  s_filterItems[0].getValue = NULL;
  s_filterItems[0].onActivate = deviceFilterActivate;
  s_filterItems[0].onAdjust = NULL;

  for (int i = 1; i < count; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i - 1);
    formatDeviceName(dev, s_filterLabels[i], sizeof(s_filterLabels[i]));
    s_filterItems[i].label = s_filterLabels[i];
    s_filterItems[i].getValue = NULL;
    s_filterItems[i].onActivate = deviceFilterActivate;
    s_filterItems[i].onAdjust = NULL;
  }

  PopupListDesc desc = {
      .title = uiStr(UI_STR_FILTER_DEVICES),
      .style = PopupList_Choices,
      .items = s_filterItems,
      .count = count,
      .selection = &s_viewOptionsSelection,
      .userdata = d,
      .isOn = deviceFilterIsOn,
      .onBack = deviceFilterOnBack,
  };
  return desc;
}

static void deviceFilterPopupDraw(View *v, int zOrder) {
  PopupListDesc desc = makeDeviceFilterDesc((MainSceneData *)v->userdata);
  popupListDraw(&desc, zOrder);
}

static ViewResult deviceFilterPopupOnInput(View *v, int padInput) {
  PopupListDesc desc = makeDeviceFilterDesc((MainSceneData *)v->userdata);
  return popupListOnInput(&desc, v, padInput);
}
