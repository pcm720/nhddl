#include "ui/views/title_options_scene.h"
#include "backends/backends.h"
#include "config/arguments.h"
#include "config/neutrino_args.h"
#include "devices/pad.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/neutrino/neutrino_arg_editor.h"
#include "ui/ui_context.h"
#include "ui/strings.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/main_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static View s_selectorView;
static View s_editorView;

typedef struct TitleOptionsSceneData {
  Target *target; // Selected title
  int isGlobal;
  char lineBuf[160];
  ArgumentList argList;        // PerTitle: global+title merged; Global: global.cnf only
  ArgumentList globalBaseline; // PerTitle: global snapshot for delta save; Global: unused
  NeutrinoArgEditor neutrinoEditor;
  int neutrinoDirty;
} TitleOptionsSceneData;
static TitleOptionsSceneData s_data = {0};
static TitleOptionsSelectorData s_selector_data = {0};
// Skip disk reload when re-entering after file picker (keeps panel + picked paths).
static int s_argsLoaded = 0;

struct View *titleOptionsGetView(void);

#define POPUP_W 240
#define POPUP_X (SCENE_VW - POPUP_W) / 2
#define ENTRY_COUNT 2
static int s_menuSelection = 0;

static const char *selectorEntry(int i) {
  if (i == 0)
    return uiStr(UI_STR_PER_TITLE_OPTIONS);
  return uiStr(UI_STR_GLOBAL_NEUTRINO_OPTIONS);
}

static void titleOptionsSelectorModalDraw(View *v, int zOrder) {
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (ENTRY_COUNT + 2));
  const int vx1 = POPUP_X, vy1 = (SCENE_VH - POPUP_H) / 2;
  const int vx2 = POPUP_X + POPUP_W, vy2 = vy1 + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;

  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  int y = vy1 + SCENE_KEEPOUT;
  fontRenderString(FONT_DEFAULT, centerX, y, FONT_ALIGN_HCENTER, 0, 0, zOrder, uiStr(UI_STR_TITLE_OPTIONS), HeaderTextColor);
  y += SCENE_ROW_SIZE * 1.5;
  for (int i = 0; i < ENTRY_COUNT; i++) {
    uint64_t col = (i == s_menuSelection) ? ColorSelected : FontMainColor;
    fontRenderString(FONT_DEFAULT, centerX, y, FONT_ALIGN_HCENTER, 0, 0, zOrder, selectorEntry(i), col);
    y += SCENE_ROW_SIZE;
  }
}

static ViewResult titleOptionsSelectorModalOnInput(View *v, int padInput) {
  TitleOptionsSelectorData *d = (TitleOptionsSelectorData *)v->userdata;
  Target *selected = uiGetContext()->selectedTitle;
  if (d && d->target)
    selected = d->target;
  if (padInput & (PAD_TRIANGLE))
    return ViewResult_Pop;

  if (!selected || !selected->device)
    return ViewResult_Pop;

  if (padInput & PAD_SELECT_ACTION) {
    if (s_menuSelection == 0)
      s_data.isGlobal = 0;
    else if (s_menuSelection == 1)
      s_data.isGlobal = 1;

    s_data.target = selected;
    s_argsLoaded = 0;
    viewStackPush(viewStack, titleOptionsGetView());
    return ViewResult_Continue;
  }
  if (padInput & PAD_UP) {
    s_menuSelection--;
    if (s_menuSelection < 0)
      s_menuSelection = ENTRY_COUNT - 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    s_menuSelection++;
    if (s_menuSelection >= ENTRY_COUNT)
      s_menuSelection = 0;
    return ViewResult_Continue;
  }
  return ViewResult_Continue;
}

struct View *titleOptionsSelectorGetView(void) {
  s_selectorView = (View){
      .type = ViewType_Modal,
      .userdata = &s_selector_data,
      .onEnter = NULL,
      .onLeave = NULL,
      .draw = titleOptionsSelectorModalDraw,
      .onInput = titleOptionsSelectorModalOnInput,
  };
  return &s_selectorView;
}

//
// Child view
//

static void titleOptionsMarkNeutrinoDirty(void *userdata) {
  TitleOptionsSceneData *d = (TitleOptionsSceneData *)userdata;
  if (d)
    d->neutrinoDirty = 1;
}

static struct BackendDevice *pathRootForDevice(struct BackendDevice *dev) {
  if (dev && dev->metadev)
    return dev->metadev;
  return dev;
}

static void titleOptionsLoadGlobalFromDisk(TitleOptionsSceneData *d) {
  struct BackendDevice *dev = d->target->device;
  if (!dev)
    return;
  freeArgumentListNodes(&d->argList);
  freeArgumentListNodes(&d->globalBaseline);
  loadGlobalNeutrinoArguments(&d->argList, dev);
  struct BackendDevice *pathDev = pathRootForDevice(dev);
  neutrinoArgEditorInit(&d->neutrinoEditor, &d->argList, pathDev, NULL, NeutrinoLayout_TitleHub);
  neutrinoArgEditorSyncFromList(&d->neutrinoEditor);
  neutrinoArgEditorSetOnListMutated(&d->neutrinoEditor, titleOptionsMarkNeutrinoDirty, d);
  d->neutrinoDirty = 0;
}

static void titleOptionsLoadMergedFromDisk(TitleOptionsSceneData *d) {
  Target *t = d->target;
  if (!t)
    return;
  freeArgumentListNodes(&d->argList);
  freeArgumentListNodes(&d->globalBaseline);
  loadGlobalNeutrinoArguments(&d->globalBaseline, t->device);
  ArgumentList titleOnly = {0};
  loadTitleNeutrinoArguments(&titleOnly, t);
  for (Argument *cur = d->globalBaseline.first; cur != NULL; cur = cur->next)
    appendArgumentCopy(&d->argList, cur);
  mergeArgumentLists(&d->argList, &titleOnly);
  freeArgumentListNodes(&titleOnly);
  struct BackendDevice *pathDev = pathRootForDevice(t->device);
  neutrinoArgEditorInit(&d->neutrinoEditor, &d->argList, pathDev, t, NeutrinoLayout_TitleHub);
  neutrinoArgEditorSyncFromList(&d->neutrinoEditor);
  neutrinoArgEditorSetOnListMutated(&d->neutrinoEditor, titleOptionsMarkNeutrinoDirty, d);
  d->neutrinoDirty = 0;
}

static void titleOptionsOnEnter(View *v) {
  TitleOptionsSceneData *d = &s_data;
  if (s_argsLoaded)
    return;

  if (d->isGlobal) {
    if (!d->target->device) {
      freeArgumentListNodes(&d->argList);
      freeArgumentListNodes(&d->globalBaseline);
      return;
    }
    titleOptionsLoadGlobalFromDisk(d);
    s_argsLoaded = 1;
    return;
  }

  Target *t = d->target;
  if (!t) {
    freeArgumentListNodes(&d->argList);
    freeArgumentListNodes(&d->globalBaseline);
    return;
  }
  titleOptionsLoadMergedFromDisk(d);
  s_argsLoaded = 1;
}

static void titleOptionsDraw(View *v, int zOrder) {
  TitleOptionsSceneData *d = (TitleOptionsSceneData *)v->userdata;
  int lineH = uiFontLineHeight();
  int isTop = (viewStackTop(viewStack) == v);

  uiDrawRect(gsGlobal, 0, 0, SCENE_VW, SCENE_VH, zOrder, BGColor);

  if (d->isGlobal)
    fontRenderString(FONT_DEFAULT, SCENE_VW / 2, SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, uiStr(UI_STR_GLOBAL_NEUTRINO_CONFIG),
                     HeaderTextColor);
  else
    fontRenderString(FONT_DEFAULT, SCENE_VW / 2, SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder,
                     d->target && d->target->name ? d->target->name : uiStr(UI_STR_NO_NAME), HeaderTextColor);

  if (!isTop)
    return;

  int titleBottom = SCENE_KEEPOUT + lineH;
  int metaTop = SCENE_CONTENT_BOTTOM;
  if (!d->isGlobal && d->target) {
    int metaY = SCENE_CONTENT_BOTTOM - lineH;
    if (d->target->path && d->target->device && d->target->device->mountpoint) {
      snprintf(d->lineBuf, sizeof(d->lineBuf), "%s%s", d->target->device->mountpoint, d->target->path);
      fontRenderString(FONT_DEFAULT, SCENE_VW / 2, metaY, FONT_ALIGN_HCENTER, 0, 0, zOrder, d->lineBuf, HeaderTextColor);
      metaY -= lineH;
    }
    if (d->target->id) {
      fontRenderString(FONT_DEFAULT, SCENE_VW / 2, metaY, FONT_ALIGN_HCENTER, 0, 0, zOrder, d->target->id, HeaderTextColor);
      metaY -= lineH;
    }
    metaTop = metaY + lineH;
  }

  const int G = SCENE_KEEPOUT;
  int topY = titleBottom + G;
  int botY = metaTop - G;
  if (botY < topY + lineH * 3)
    botY = topY + lineH * 3;

  const char *panelTitle = neutrinoArgEditorPanelTitle(&d->neutrinoEditor);
  if (!panelTitle && neutrinoArgEditorOnTitleHub(&d->neutrinoEditor))
    panelTitle = d->isGlobal ? uiStr(UI_STR_GLOBAL_OPTIONS) : uiStr(UI_STR_PER_TITLE_OPTIONS);

  int panelW = uiContentPanelWidth();
  int vx1 = uiContentPanelLeft();
  int vx2 = vx1 + panelW;
  int vy1 = topY;
  int vy2 = botY;
  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);

  int listTop = vy1 + SCENE_KEEPOUT;
  if (panelTitle) {
    fontRenderString(FONT_DEFAULT, (vx1 + vx2) / 2, listTop, FONT_ALIGN_HCENTER, 0, 0, zOrder + 1, panelTitle, HeaderTextColor);
    listTop += lineH;
  }
  neutrinoArgEditorDraw(&d->neutrinoEditor, gsGlobal, zOrder + 1, vx1, listTop, vx2, vy2 - SCENE_KEEPOUT);

  int panel = neutrinoArgEditorPanel(&d->neutrinoEditor);
  if (neutrinoArgEditorOnTitleHub(&d->neutrinoEditor)) {
    uiDrawScenePrompt(gsGlobal, 0, zOrder + 2, HeaderTextColor, ICON_TRIANGLE, uiStr(UI_STR_SAVE_AND_BACK));
    uiDrawScenePrompt(gsGlobal, 1, zOrder + 2, HeaderTextColor, ICON_START, uiStr(UI_STR_DISCARD));
    uiDrawScenePrompt(gsGlobal, 2, zOrder + 2, HeaderTextColor, ICON_SQUARE, uiStr(UI_STR_TEST));
    uiDrawScenePrompt2(gsGlobal, 3, zOrder + 2, HeaderTextColor, ICON_CROSS, ICON_CIRCLE, uiStr(UI_STR_TOGGLE_OPEN));
  } else if (panel == 3) {
    uiDrawScenePrompt(gsGlobal, 0, zOrder + 2, HeaderTextColor, ICON_TRIANGLE, uiStr(UI_STR_BACK));
    uiDrawScenePrompt(gsGlobal, 3, zOrder + 2, HeaderTextColor, ICON_SQUARE, uiStr(UI_STR_CLEAR));
  } else {
    uiDrawScenePrompt(gsGlobal, 0, zOrder + 2, HeaderTextColor, ICON_TRIANGLE, uiStr(UI_STR_BACK));
    uiDrawScenePrompt2(gsGlobal, 3, zOrder + 2, HeaderTextColor, ICON_CROSS, ICON_CIRCLE, uiStr(UI_STR_TOGGLE));
  }
}

static ViewResult leaveTitleOptionsToList(void) {
  s_argsLoaded = 0;
  // Editor is pushed on top of the Per-title/Global selector, leave both to return to the title list.
  viewStackPop(viewStack); // editor
  viewStackPop(viewStack); // selector
  return ViewResult_Continue;
}

static ViewResult titleOptionsOnInput(View *v, int padInput) {
  TitleOptionsSceneData *d = (TitleOptionsSceneData *)v->userdata;
  if (d->isGlobal) {
    if (!d->target->device)
      return leaveTitleOptionsToList();
    if ((padInput & PAD_START) && neutrinoArgEditorOnTitleHub(&d->neutrinoEditor))
      return leaveTitleOptionsToList();
    if ((padInput & PAD_TRIANGLE) && neutrinoArgEditorOnTitleHub(&d->neutrinoEditor)) {
      if (d->neutrinoDirty) {
        neutrinoArgEditorApplyToList(&d->neutrinoEditor);
        saveGlobalNeutrinoArguments(d->target->device, &d->argList);
        showOSD(uiStr(UI_STR_GLOBAL_NEUTRINO_SAVED), 120);
        d->neutrinoDirty = 0;
      }
      return leaveTitleOptionsToList();
    }
    if ((padInput & PAD_SQUARE) && neutrinoArgEditorOnTitleHub(&d->neutrinoEditor)) {
      neutrinoArgEditorApplyToList(&d->neutrinoEditor);
      uiTestLaunchWithInMemoryGlobalArgs(&d->argList);
      return ViewResult_Continue;
    }
    return neutrinoArgEditorOnInput(&d->neutrinoEditor, padInput);
  }

  Target *t = d->target;
  if (!t)
    return leaveTitleOptionsToList();
  if ((padInput & PAD_START) && neutrinoArgEditorOnTitleHub(&d->neutrinoEditor))
    return leaveTitleOptionsToList();
  if ((padInput & PAD_TRIANGLE) && neutrinoArgEditorOnTitleHub(&d->neutrinoEditor)) {
    if (d->neutrinoDirty) {
      neutrinoArgEditorApplyToList(&d->neutrinoEditor);
      saveTitleNeutrinoArgumentsDelta(t, &d->argList, &d->globalBaseline);
      d->neutrinoDirty = 0;
    }
    return leaveTitleOptionsToList();
  }
  if ((padInput & PAD_SQUARE) && neutrinoArgEditorOnTitleHub(&d->neutrinoEditor)) {
    neutrinoArgEditorApplyToList(&d->neutrinoEditor);
    uiTestLaunchWithTitleArgs(t, &d->argList);
    return ViewResult_Continue;
  }
  return neutrinoArgEditorOnInput(&d->neutrinoEditor, padInput);
}

struct View *titleOptionsGetView(void) {
  s_editorView = (View){
      .type = ViewType_Scene,
      .userdata = &s_data,
      .onEnter = titleOptionsOnEnter,
      .onLeave = NULL,
      .draw = titleOptionsDraw,
      .onInput = titleOptionsOnInput,
  };
  return &s_editorView;
}
