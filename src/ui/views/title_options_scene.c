#include "ui/views/title_options_scene.h"
#include "backends/backends.h"
#include "config/arguments.h"
#include "config/neutrino_args.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/neutrino/neutrino_arg_editor.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/main_scene.h"
#include <libpad.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static View s_view;

typedef struct TitleOptionsSceneData {
  Target *target; // Selected title
  int isGlobal;
  char lineBuf[160];
  ArgumentList argList;        // PerTitle: global+title merged; Global: global.cnf only
  ArgumentList globalBaseline; // PerTitle: global snapshot for delta save; Global: unused
  NeutArgEditor neutEditor;
  int neutrinoDirty;
} TitleOptionsSceneData;
static TitleOptionsSceneData s_data = {0};
static TitleOptionsSelectorData s_selector_data = {0};

struct View *titleOptionsGetView(void);

#define POPUP_W 240
#define POPUP_X (SCENE_VW - POPUP_W) / 2
#define POPUP_Y 120
static const char *entries[] = {"Per-title options", "Global Neutrino options"};
#define ENTRY_COUNT sizeof(entries) / sizeof(char *)
static int s_menuSelection = 0;

static void titleOptionsSelectorModalDraw(View *v, int zOrder) {
  // Dynamic modal height (ENTRY_COUNT + header + spacing)
  const int POPUP_H = SCENE_KEEPOUT + (SCENE_ROW_SIZE * (ENTRY_COUNT + 2));
  const int vx1 = POPUP_X, vy1 = (SCENE_VH - POPUP_H) / 2;
  const int vx2 = POPUP_X + POPUP_W, vy2 = vy1 + POPUP_H;
  const int centerX = (vx1 + vx2) / 2;

  // Background
  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  // Header
  int y = vy1 + SCENE_KEEPOUT;
  fontRenderString(FONT_DEFAULT, centerX, y, FONT_ALIGN_HCENTER, 0, 0, zOrder, "Title options", HeaderTextColor);
  // Entries
  y += SCENE_ROW_SIZE * 1.5;
  for (int i = 0; i < ENTRY_COUNT; i++) {
    uint64_t col = (i == s_menuSelection) ? ColorSelected : FontMainColor;
    fontRenderString(FONT_DEFAULT, centerX, y, FONT_ALIGN_HCENTER, 0, 0, zOrder, entries[i], col);
    y += SCENE_ROW_SIZE;
  }
}

static ViewResult titleOptionsSelectorModalOnInput(View *v, int padInput) {
  TitleOptionsSelectorData *d = (TitleOptionsSelectorData *)v->userdata;
  if (padInput & (PAD_TRIANGLE))
    return ViewResult_Pop;

  if (!d || !d->target || !d->target->device)
    return ViewResult_Pop;

  if (padInput & (PAD_CIRCLE | PAD_CROSS)) {
    if (s_menuSelection == 0)
      s_data.isGlobal = 0;
    else if (s_menuSelection == 1)
      s_data.isGlobal = 1;

    s_data.target = d->target;
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
  s_view = (View){
      .type = ViewType_Modal,
      .userdata = &s_selector_data,
      .onEnter = NULL,
      .onLeave = NULL,
      .draw = titleOptionsSelectorModalDraw,
      .onInput = titleOptionsSelectorModalOnInput,
  };
  return &s_view;
}

//
// Child view
//

static void clearArgListContents(ArgumentList *l) {
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
  clearArgListContents(&d->argList);
  clearArgListContents(&d->globalBaseline);
  loadGlobalNeutrinoArguments(&d->argList, dev);
  struct BackendDevice *pathDev = pathRootForDevice(dev);
  neutArgEditorInit(&d->neutEditor, &d->argList, pathDev, NULL, NeutLayout_TitleHub);
  neutArgEditorSyncFromList(&d->neutEditor);
  neutArgEditorSetOnListMutated(&d->neutEditor, titleOptionsMarkNeutrinoDirty, d);
  d->neutrinoDirty = 0;
}

static void titleOptionsLoadMergedFromDisk(TitleOptionsSceneData *d) {
  Target *t = d->target;
  if (!t)
    return;
  clearArgListContents(&d->argList);
  clearArgListContents(&d->globalBaseline);
  loadGlobalNeutrinoArguments(&d->globalBaseline, t->device);
  ArgumentList titleOnly = {0};
  loadTitleNeutrinoArguments(&titleOnly, t);
  for (Argument *cur = d->globalBaseline.first; cur != NULL; cur = cur->next)
    appendArgumentCopy(&d->argList, cur);
  mergeArgumentLists(&d->argList, &titleOnly);
  freeArgumentListNodes(&titleOnly);
  struct BackendDevice *pathDev = pathRootForDevice(t->device);
  neutArgEditorInit(&d->neutEditor, &d->argList, pathDev, t, NeutLayout_TitleHub);
  neutArgEditorSyncFromList(&d->neutEditor);
  neutArgEditorSetOnListMutated(&d->neutEditor, titleOptionsMarkNeutrinoDirty, d);
  d->neutrinoDirty = 0;
}

static void titleOptionsOnEnter(View *v) {
  (void)v;
  TitleOptionsSceneData *d = &s_data;
  if (d->isGlobal) {
    if (!d->target->device) {
      clearArgListContents(&d->argList);
      clearArgListContents(&d->globalBaseline);
      return;
    }
    titleOptionsLoadGlobalFromDisk(d);
    return;
  }

  Target *t = d->target;
  if (!t) {
    clearArgListContents(&d->argList);
    clearArgListContents(&d->globalBaseline);
    return;
  }
  titleOptionsLoadMergedFromDisk(d);
}

static void titleOptionsDraw(View *v, int zOrder) {
  TitleOptionsSceneData *d = (TitleOptionsSceneData *)v->userdata;
  int topY = SCENE_KEEPOUT + SCENE_ROW_SIZE + 8;
  int botY = SCENE_FOOTER_TOP - SCENE_ROW_SIZE * 2;

  uiDrawRect(gsGlobal, 0, 0, SCENE_VW, SCENE_VH, zOrder, BGColor);
  if (d->isGlobal)
    fontRenderString(FONT_DEFAULT, SCENE_VW / 2, SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, "Global Neutrino configuration", HeaderTextColor);
  else {
    snprintf(d->lineBuf, sizeof(d->lineBuf), "%s", d->target->name ? d->target->name : "(no name)");
    fontRenderString(FONT_DEFAULT, SCENE_VW / 2, SCENE_KEEPOUT, FONT_ALIGN_HCENTER, 0, 0, zOrder, d->lineBuf, HeaderTextColor);
    if (d->target->path && d->target->device && d->target->device->mountpoint) {
      snprintf(d->lineBuf, sizeof(d->lineBuf), "%s%s", d->target->device->mountpoint, d->target->path);
      fontRenderString(FONT_DEFAULT, SCENE_VW / 2, botY, FONT_ALIGN_HCENTER, 0, 0, zOrder, d->lineBuf, HeaderTextColor);
      botY -= fontGetLineHeight(FONT_DEFAULT);
    }
    if (d->target->id) {
      fontRenderString(FONT_DEFAULT, SCENE_VW / 2, botY, FONT_ALIGN_HCENTER, 0, 0, zOrder, d->target->id, HeaderTextColor);
      botY -= fontGetLineHeight(FONT_DEFAULT);
    }
  }

  neutArgEditorDraw(&d->neutEditor, gsGlobal, zOrder + 1, SCENE_KEEPOUT, topY, SCENE_VW - SCENE_KEEPOUT, botY);
  int panel = neutArgEditorPanel(&d->neutEditor);
  if (neutArgEditorOnTitleHub(&d->neutEditor)) {
    uiDrawScenePrompt(gsGlobal, 0, zOrder + 2, HeaderTextColor, ICON_CROSS, "Toggle / open");
    uiDrawScenePrompt(gsGlobal, 1, zOrder + 2, HeaderTextColor, ICON_SQUARE, "Test");
    uiDrawScenePrompt(gsGlobal, 2, zOrder + 2, HeaderTextColor, ICON_TRIANGLE, "Discard");
    uiDrawScenePrompt(gsGlobal, 3, zOrder + 2, HeaderTextColor, ICON_CIRCLE, d->neutrinoDirty ? "Save & back" : "Back");
  } else if (panel == 3) {
    uiDrawScenePrompt(gsGlobal, 0, zOrder + 2, HeaderTextColor, ICON_CROSS, "Pick file");
    uiDrawScenePrompt(gsGlobal, 1, zOrder + 2, HeaderTextColor, ICON_TRIANGLE, "Clear");
    uiDrawScenePrompt(gsGlobal, 2, zOrder + 2, HeaderTextColor, ICON_CIRCLE, "Back");
  } else if (panel == 4) {
    uiDrawScenePrompt(gsGlobal, 0, zOrder + 2, HeaderTextColor, ICON_CROSS, "Toggle");
    uiDrawScenePrompt(gsGlobal, 3, zOrder + 2, HeaderTextColor, ICON_CIRCLE, "Back");
  } else {
    uiDrawScenePrompt(gsGlobal, 0, zOrder + 2, HeaderTextColor, ICON_CROSS, "Toggle");
    uiDrawScenePrompt(gsGlobal, 3, zOrder + 2, HeaderTextColor, ICON_CIRCLE, "Back");
  }
}

static ViewResult titleOptionsOnInput(View *v, int padInput) {
  TitleOptionsSceneData *d = (TitleOptionsSceneData *)v->userdata;
  if (d->isGlobal) {
    if (!d->target->device)
      return ViewResult_Pop;
    if ((padInput & PAD_TRIANGLE) && neutArgEditorOnTitleHub(&d->neutEditor)) {
      return ViewResult_Pop;
    }
    if (padInput & PAD_CIRCLE) {
      if (neutArgEditorPanel(&d->neutEditor) > 0)
        return neutArgEditorOnInput(&d->neutEditor, padInput);
      if (d->neutrinoDirty) {
        neutArgEditorApplyToList(&d->neutEditor);
        saveGlobalNeutrinoArguments(d->target->device, &d->argList);
        showOSD("Global Neutrino saved", 120);
        d->neutrinoDirty = 0;
      }
      return ViewResult_Pop;
    }
    if ((padInput & PAD_SQUARE) && neutArgEditorOnTitleHub(&d->neutEditor)) {
      neutArgEditorApplyToList(&d->neutEditor);
      uiTestLaunchWithInMemoryGlobalArgs(&d->argList);
      return ViewResult_Continue;
    }
    return neutArgEditorOnInput(&d->neutEditor, padInput);
  }

  Target *t = d->target;
  if (!t)
    return ViewResult_Pop;
  if ((padInput & PAD_TRIANGLE) && neutArgEditorOnTitleHub(&d->neutEditor)) {
    return ViewResult_Pop;
  }
  if (padInput & PAD_CIRCLE) {
    if (neutArgEditorOnTitleHub(&d->neutEditor)) {
      if (d->neutrinoDirty) {
        neutArgEditorApplyToList(&d->neutEditor);
        saveTitleNeutrinoArgumentsDelta(t, &d->argList, &d->globalBaseline);
        d->neutrinoDirty = 0;
      }
      return ViewResult_Pop;
    }
  }
  if ((padInput & PAD_SQUARE) && neutArgEditorOnTitleHub(&d->neutEditor)) {
    neutArgEditorApplyToList(&d->neutEditor);
    uiTestLaunchWithTitleArgs(t, &d->argList);
    return ViewResult_Continue;
  }
  return neutArgEditorOnInput(&d->neutEditor, padInput);
}

struct View *titleOptionsGetView(void) {
  s_view = (View){
      .type = ViewType_Scene,
      .userdata = &s_data,
      .onEnter = titleOptionsOnEnter,
      .onLeave = NULL,
      .draw = titleOptionsDraw,
      .onInput = titleOptionsOnInput,
  };
  return &s_view;
}
