#include "ui/popup_list.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/scale.h"
#include "ui/ui_shared.h"
#include "devices/pad.h"
#include <stdio.h>
#include <string.h>

#define POPUP_MIN_W 240
#define VALUE_BUF_SIZE 128

static int textWidthV(const char *s) {
  if (!s || !s[0])
    return 0;
  return scaleUnscaleX(fontCalcDimensions(FONT_DEFAULT, s));
}

static int clampInt(int v, int lo, int hi) {
  if (v < lo)
    return lo;
  if (v > hi)
    return hi;
  return v;
}

static void measureValueRows(const PopupListDesc *desc, int *outMaxLabel, int *outMaxValue, int *outTitleW) {
  int maxLabel = 0, maxValue = 0, titleW = 0;
  char buf[VALUE_BUF_SIZE];

  if (desc->title && desc->title[0])
    titleW = textWidthV(desc->title);

  for (int i = 0; i < desc->count; i++) {
    const PopupListItem *it = &desc->items[i];
    int lw = textWidthV(it->label);
    if (lw > maxLabel)
      maxLabel = lw;
    if (it->getValue) {
      buf[0] = '\0';
      it->getValue(i, buf, sizeof(buf), desc->userdata);
      int vw = textWidthV(buf);
      if (vw > maxValue)
        maxValue = vw;
    }
  }
  *outMaxLabel = maxLabel;
  *outMaxValue = maxValue;
  *outTitleW = titleW;
}

static void measureChoices(const PopupListDesc *desc, int *outMaxLabel, int *outTitleW) {
  int maxLabel = 0, titleW = 0;
  if (desc->title && desc->title[0])
    titleW = textWidthV(desc->title);
  for (int i = 0; i < desc->count; i++) {
    int lw = textWidthV(desc->items[i].label);
    if (lw > maxLabel)
      maxLabel = lw;
  }
  *outMaxLabel = maxLabel;
  *outTitleW = titleW;
}

static void layoutPopup(const PopupListDesc *desc, int *vx1, int *vy1, int *vx2, int *vy2, int *leftX, int *midX, int *rightX) {
  const int gap = SCENE_KEEPOUT;
  const int maxW = SCENE_VW - 2 * SCENE_KEEPOUT;
  int contentW;
  int maxLabel = 0, maxValue = 0, titleW = 0;

  if (desc->style == PopupList_ValueRows) {
    measureValueRows(desc, &maxLabel, &maxValue, &titleW);
    contentW = maxLabel + gap + maxValue;
  } else {
    measureChoices(desc, &maxLabel, &titleW);
    contentW = UI_SEL_LABEL_INDENT + maxLabel;
  }
  if (titleW > contentW)
    contentW = titleW;

  int popupW = clampInt(contentW + 2 * SCENE_KEEPOUT, POPUP_MIN_W, maxW);
  int headerRows = (desc->title != NULL) ? 1 : 0;
  int popupH = SCENE_KEEPOUT * 2 + SCENE_ROW_SIZE * (headerRows + desc->count);

  *vx1 = (SCENE_VW - popupW) / 2;
  *vy1 = (SCENE_VH - popupH) / 2;
  *vx2 = *vx1 + popupW;
  *vy2 = *vy1 + popupH;
  *leftX = *vx1 + SCENE_KEEPOUT;
  *rightX = *vx2 - SCENE_KEEPOUT;
  if (desc->style == PopupList_ValueRows)
    *midX = *leftX + maxLabel + gap;
  else
    *midX = *leftX + UI_SEL_LABEL_INDENT;
}

void popupListDraw(const PopupListDesc *desc, int zOrder) {
  if (!desc || !desc->items || desc->count <= 0 || !desc->selection)
    return;

  int vx1, vy1, vx2, vy2, leftX, midX, rightX;
  layoutPopup(desc, &vx1, &vy1, &vx2, &vy2, &leftX, &midX, &rightX);
  int centerX = (vx1 + vx2) / 2;
  int sel = *desc->selection;
  if (sel < 0)
    sel = 0;
  if (sel >= desc->count)
    sel = desc->count - 1;

  uiDrawRectRounded(gsGlobal, vx1, vy1, vx2, vy2, POPUP_ROUNDRECT_RADIUS, zOrder, ModalBGColor);

  int entryY = vy1 + SCENE_KEEPOUT;
  if (desc->title != NULL) {
    if (desc->title[0])
      fontRenderString(FONT_DEFAULT, centerX, entryY, FONT_ALIGN_HCENTER, 0, 0, zOrder, desc->title, HeaderTextColor);
    entryY += SCENE_ROW_SIZE;
  }

  char valueBuf[VALUE_BUF_SIZE];
  for (int i = 0; i < desc->count; i++) {
    const PopupListItem *it = &desc->items[i];
    uint64_t col = (i == sel) ? ColorSelected : FontMainColor;

    if (desc->style == PopupList_Choices) {
      int on = desc->isOn ? desc->isOn(i, desc->userdata) : (i == sel);
      uiDrawChoiceRow(gsGlobal, leftX, rightX, entryY, SCENE_ROW_SIZE, zOrder, col, on, it->label);
    } else {
      fontRenderInRect(FONT_DEFAULT, leftX, entryY, midX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, it->label, col);
      if (it->getValue) {
        valueBuf[0] = '\0';
        it->getValue(i, valueBuf, sizeof(valueBuf), desc->userdata);
        fontRenderInRect(FONT_DEFAULT, midX, entryY, rightX, entryY + SCENE_ROW_SIZE, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder + 1, valueBuf,
                         col);
      }
    }
    entryY += SCENE_ROW_SIZE;
  }
}

ViewResult popupListOnInput(const PopupListDesc *desc, View *v, int padInput) {
  if (!desc || !desc->items || desc->count <= 0 || !desc->selection)
    return ViewResult_Continue;

  if (padInput & PAD_TRIANGLE) {
    if (desc->onBack)
      return desc->onBack(v, desc->userdata);
    return ViewResult_Pop;
  }

  int *sel = desc->selection;
  if (padInput & PAD_UP) {
    (*sel)--;
    if (*sel < 0)
      *sel = desc->count - 1;
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    (*sel)++;
    if (*sel >= desc->count)
      *sel = 0;
    return ViewResult_Continue;
  }

  int idx = *sel;
  if (idx < 0 || idx >= desc->count)
    return ViewResult_Continue;

  const PopupListItem *it = &desc->items[idx];
  if ((padInput & PAD_SELECT_ACTION) && it->onActivate) {
    it->onActivate(idx, v, desc->userdata);
    return ViewResult_Continue;
  }
  if ((padInput & (PAD_LEFT | PAD_RIGHT)) && it->onAdjust) {
    int dir = (padInput & PAD_RIGHT) ? 1 : -1;
    it->onAdjust(idx, dir, desc->userdata);
    return ViewResult_Continue;
  }
  // Choices often toggle/set on Left/Right via onActivate as well (video widescreen).
  if (desc->style == PopupList_Choices && (padInput & (PAD_LEFT | PAD_RIGHT)) && it->onActivate) {
    it->onActivate(idx, v, desc->userdata);
    return ViewResult_Continue;
  }
  return ViewResult_Continue;
}
