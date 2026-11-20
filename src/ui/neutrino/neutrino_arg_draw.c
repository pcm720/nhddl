#include "backends/backends.h"
#include "backends/target.h"
#include "config/arguments.h"
#include "config/common.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/neutrino/neutrino_arg_editor_priv.h"
#include "ui/strings.h"
#include "ui/ui_shared.h"
#include <gsKit.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static const char *sectionTitle(int id) {
  switch (id) {
  case 0:
    return neutrinoStr(UI_N_STR_SECTION_GC);
  case 1:
    return neutrinoStr(UI_N_STR_SECTION_GSM);
  case 2:
    return neutrinoStr(UI_N_STR_SECTION_PATHS);
  case 3:
    return neutrinoStr(UI_N_STR_SECTION_FLAGS);
  case 4:
    return neutrinoStr(UI_N_STR_SECTION_ARGS);
  case 5:
    return neutrinoStr(UI_N_STR_SECTION_RAW);
  default:
    return "";
  }
}

static const char *gcLabel(int idx) {
  if (idx < 0 || idx >= NEUTRINO_GC_OPTION_COUNT)
    return "";
  return neutrinoStr((NeutrinoStrId)(UI_N_STR_GC_IOP_FAST_READS + idx));
}

static const char *gsmLabel(int idx) {
  if (idx < 0 || idx >= NEUTRINO_GSM_OPTION_COUNT)
    return "";
  return neutrinoStr((NeutrinoStrId)(UI_N_STR_GSM_FP1 + idx));
}

// Show mount-prefixed paths for device emulation
static void formatPathArgDisplay(const NeutrinoArgEditor *ed, const char *value, char *out, size_t outSz) {
  const char *empty = neutrinoStr(UI_N_STR_NONE);
  if (!value || !value[0]) {
    uiTrimPathMiddle(NULL, out, outSz, empty);
    return;
  }
  if (strchr(value, ':')) {
    uiTrimPathMiddle(value, out, outSz, empty);
    return;
  }
  const char *mount = (ed && ed->pathRootDevice && ed->pathRootDevice->mountpoint) ? ed->pathRootDevice->mountpoint : NULL;
  if (!mount || !mount[0]) {
    uiTrimPathMiddle(value, out, outSz, empty);
    return;
  }
  char full[PATH_MAX];
  joinDevicePath(full, sizeof(full), mount, value);
  uiTrimPathMiddle(full[0] ? full : value, out, outSz, empty);
}

void neutrinoArgEditorClampScroll(NeutrinoArgEditor *ed, int visibleRows) {
  if (ed->cursor < ed->scroll)
    ed->scroll = ed->cursor;
  if (ed->cursor >= ed->scroll + visibleRows)
    ed->scroll = ed->cursor - visibleRows + 1;
  if (ed->scroll < 0)
    ed->scroll = 0;
  int maxScroll = ed->rowCount - visibleRows;
  if (maxScroll < 0)
    maxScroll = 0;
  if (ed->scroll > maxScroll)
    ed->scroll = maxScroll;
}

static int pathKeyColumnWidth(void) {
  return fontCalcDimensions(FONT_DEFAULT, NEUTRINO_PATH_ATA0ID) + SCENE_KEEPOUT;
}

static int rawKeyColumnWidth(const NeutrinoArgEditor *ed) {
  int maxW = fontCalcDimensions(FONT_DEFAULT, "-?");
  char key[80];
  for (int j = 0; j < ed->rawArgCount; j++) {
    Argument *ra = ed->rawArgs[j];
    if (!ra || !ra->arg)
      continue;
    snprintf(key, sizeof(key), "-%s", ra->arg);
    int w = fontCalcDimensions(FONT_DEFAULT, key);
    if (w > maxW)
      maxW = w;
  }
  return maxW + SCENE_KEEPOUT;
}

const char *neutrinoArgEditorPanelTitle(const NeutrinoArgEditor *ed) {
  if (!ed || ed->panel <= 0)
    return NULL;
  switch (ed->panel) {
  case 1:
    return sectionTitle(0);
  case 2:
    return sectionTitle(1);
  case 3:
    return sectionTitle(2);
  case 4:
    return sectionTitle(5);
  default:
    return NULL;
  }
}

void neutrinoArgEditorDraw(NeutrinoArgEditor *ed, GSGLOBAL *gs, int zOrder, int vx1, int vy1, int vx2, int vy2) {
  int lineH = uiFontLineHeight();
  int visibleRows = (vy2 - vy1) / lineH;
  if (visibleRows < 1)
    visibleRows = 1;
  if (visibleRows > NEUTRINO_ARG_VISIBLE_ROWS_MAX)
    visibleRows = NEUTRINO_ARG_VISIBLE_ROWS_MAX;

  neutrinoArgEditorClampScroll(ed, visibleRows);

  int leftX = vx1 + SCENE_KEEPOUT;
  int rightX = vx2 - SCENE_KEEPOUT;
  int pathKeyW = pathKeyColumnWidth();
  int rawKeyW = rawKeyColumnWidth(ed);
  int y = vy1;
  for (int i = ed->scroll; i < ed->rowCount && i < ed->scroll + visibleRows; i++) {
    uint64_t col = (i == ed->cursor) ? ColorSelected : FontMainColor;
    unsigned char k = ed->rowKind[i];
    unsigned char s = ed->rowSub[i];

    if (k == RK_SECTION) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, sectionTitle((int)s),
                       HeaderTextColor);
    } else if (k == RK_GC) {
      int on = (ed->gcState & NEUTRINO_GC_OPTIONS[s].bit) ? 1 : 0;
      uiDrawChoiceRow(gs, leftX, rightX, y, lineH, zOrder, col, on, gcLabel((int)s));
    } else if (k == RK_GSM) {
      int on = (ed->gsmState & NEUTRINO_GSM_OPTIONS[s].bit) ? 1 : 0;
      uiDrawChoiceRow(gs, leftX, rightX, y, lineH, zOrder, col, on, gsmLabel((int)s));
    } else if (k == RK_PATH) {
      const char *pn = neutrinoPathArgName((int)s);
      Argument *a = getArgument(ed->list, pn);
      char disp[120];
      formatPathArgDisplay(ed, a && a->value && !a->isDisabled ? a->value : NULL, disp, sizeof(disp));
      int valueX = leftX + pathKeyW;
      if (valueX > rightX - 8)
        valueX = leftX + (rightX - leftX) / 3;
      fontRenderInRect(FONT_DEFAULT, leftX, y, valueX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, pn, col);
      fontRenderInRect(FONT_DEFAULT, valueX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, disp, col);
    } else if (k == RK_FLAG_LOGO) {
      uiDrawChoiceRow(gs, leftX, rightX, y, lineH, zOrder, col, ed->logoOn, neutrinoStr(UI_N_STR_FLAG_LOGO));
    } else if (k == RK_FLAG_DBC) {
      uiDrawChoiceRow(gs, leftX, rightX, y, lineH, zOrder, col, ed->dbcOn, neutrinoStr(UI_N_STR_FLAG_DBC));
    } else if (k == RK_MENU_GC) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, neutrinoStr(UI_N_STR_MENU_GC),
                       col);
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, ">", HeaderTextColor);
    } else if (k == RK_MENU_GSM) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, neutrinoStr(UI_N_STR_MENU_GSM),
                       col);
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, ">", HeaderTextColor);
    } else if (k == RK_MENU_PATHS) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, neutrinoStr(UI_N_STR_MENU_PATHS),
                       col);
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, ">", HeaderTextColor);
    } else if (k == RK_MENU_RAW) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, neutrinoStr(UI_N_STR_MENU_RAW),
                       col);
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, ">", HeaderTextColor);
    } else if (k == RK_RAW_NONE) {
      fontRenderInRect(FONT_DEFAULT, leftX + UI_SEL_LABEL_INDENT, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder,
                       neutrinoStr(UI_N_STR_NO_ADDITIONAL_ARGS), FontMainColor);
    } else if (k == RK_RAW_ARG && (int)s >= 0 && (int)s < ed->rawArgCount) {
      Argument *ra = ed->rawArgs[(int)s];
      if (ra) {
        int en = !ra->isDisabled;
        uiDrawSelectionIndicator(gs, leftX + UI_SEL_INDICATOR_R, uiSelectionIndicatorCenterY(y, lineH), UI_SEL_INDICATOR_R, zOrder, col, en);
        char key[80];
        char valdisp[120];
        snprintf(key, sizeof(key), "-%s", ra->arg ? ra->arg : "?");
        uiTrimPathMiddle(ra->value && ra->value[0] ? ra->value : NULL, valdisp, sizeof(valdisp), neutrinoStr(UI_N_STR_NONE));
        int valueX = leftX + UI_SEL_LABEL_INDENT + rawKeyW;
        if (valueX > rightX - 8)
          valueX = leftX + UI_SEL_LABEL_INDENT + (rightX - leftX - UI_SEL_LABEL_INDENT) / 3;
        fontRenderInRect(FONT_DEFAULT, leftX + UI_SEL_LABEL_INDENT, y, valueX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, key, col);
        fontRenderInRect(FONT_DEFAULT, valueX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, valdisp, col);
      }
    } else if (k == RK_TITLE_FAV && ed->titleTarget) {
      int on = (ed->titleTarget->flags & TitleFlag_Favorite) ? 1 : 0;
      uiDrawChoiceRow(gs, leftX, rightX, y, lineH, zOrder, col, on, neutrinoStr(UI_N_STR_FAVORITE));
    } else if (k == RK_TITLE_FAKE && ed->titleTarget) {
      int on = (ed->titleTarget->flags & TitleFlag_FakeDEV9) ? 1 : 0;
      uiDrawChoiceRow(gs, leftX, rightX, y, lineH, zOrder, col, on, neutrinoStr(UI_N_STR_FAKE_DEV9));
    }
    y += lineH;
  }
}
