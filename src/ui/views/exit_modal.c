#include "ui/views/exit_modal.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include "ui/views/main_scene.h"
#include <libpad.h>
#include <stdint.h>

static View s_view;

static void exitModalDraw(View *v, int zOrder) {
  int centerX = SCENE_VW / 2;
  int centerY = SCENE_VH / 2;
  // Background
  gsKit_prim_sprite(gsGlobal, 0.0f, 0.0f, (float)gsGlobal->Width, (float)gsGlobal->Height, zOrder, BGColor);
  // Scene content
  fontRenderString(FONT_DEFAULT, centerX, centerY, FONT_ALIGN_CENTER, 0, 0, zOrder + 1, "Exit?", HeaderTextColor);
  uiDrawScenePrompt(gsGlobal, 0, zOrder + 1, HeaderTextColor, ICON_CROSS, "Yes");
  uiDrawScenePrompt(gsGlobal, 3, zOrder + 1, HeaderTextColor, ICON_CIRCLE, "No");
}

static ViewResult exitModalOnInput(View *v, int padInput) {
  MainSceneData *d = (MainSceneData *)v->userdata;
  if (padInput & PAD_CROSS) {
    d->exitRequested = 1;
    return ViewResult_Pop;
  }
  if (padInput & PAD_CIRCLE)
    return ViewResult_Pop;
  return ViewResult_Continue;
}

struct View *exitModalGetView(void) {
  s_view = (View){
      .type = ViewType_Scene,
      .userdata = NULL,
      .onEnter = NULL,
      .onLeave = NULL,
      .draw = exitModalDraw,
      .onInput = exitModalOnInput,
  };
  return &s_view;
}
