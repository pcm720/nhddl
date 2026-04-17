#include "ui/views/osd_overlay.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/icons.h"
#include "ui/layout.h"
#include "ui/ui_shared.h"
#include "ui/view.h"
#include <stdint.h>

static View s_view;

static void osdOverlayDraw(View *v, int zOrder) {
  (void)v;
  const int vx1 = SCENE_KEEPOUT, vy1 = SCENE_CONTENT_BOTTOM, vx2 = SCENE_VW - SCENE_KEEPOUT, vy2 = (SCENE_VH - SCENE_KEEPOUT);
  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);
  const char *msg = getOSDMessage();
  if (msg)
    fontRenderInRect(FONT_DEFAULT, vx1, vy1, vx2, vy2, FONT_ALIGN_CENTER, zOrder, msg, HeaderTextColor);
}

struct View *osdOverlayGetView(void) {
  s_view = (View){
      .type = ViewType_Overlay,
      .userdata = NULL,
      .onEnter = NULL,
      .onLeave = NULL,
      .draw = osdOverlayDraw,
      .onInput = NULL,
  };
  return &s_view;
}
