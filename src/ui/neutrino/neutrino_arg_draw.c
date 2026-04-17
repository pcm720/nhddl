#include "ui/neutrino/neutrino_arg_editor_priv.h"
#include "backends/target.h"
#include "config/arguments.h"
#include "ui/draw.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/ui_shared.h"
#include <gsKit.h>
#include <stdio.h>
#include <string.h>

static const char *sectionTitle(int id) {
  switch (id) {
  case 0:
    return "Compatibility (-gc)";
  case 1:
    return "GSM (-gsm)";
  case 2:
    return "Paths (device)";
  case 3:
    return "Flags";
  case 4:
    return "Neutrino arguments";
  case 5:
    return "Additional arguments (raw)";
  default:
    return "";
  }
}

static void trimPathDisplay(const char *path, char *out, size_t outSz) {
  if (!path || !path[0]) {
    snprintf(out, outSz, "(none)");
    return;
  }
  size_t len = strlen(path);
  if (len < outSz) {
    snprintf(out, outSz, "%s", path);
    return;
  }
  if (outSz < 8) {
    out[0] = '\0';
    return;
  }
  size_t head = (outSz - 4) / 2;
  size_t tail = outSz - 4 - head;
  snprintf(out, outSz, "%.*s...%s", (int)head, path, path + len - tail);
}

void neutArgEditorClampScroll(NeutArgEditor *ed, int visibleRows) {
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

void neutArgEditorDraw(NeutArgEditor *ed, GSGLOBAL *gs, int zOrder, int vx1, int vy1, int vx2, int vy2) {
  int lineH = fontGetLineHeight(FONT_DEFAULT);
  if (lineH <= 0)
    lineH = SCENE_ROW_SIZE;
  int visibleRows = (vy2 - vy1) / lineH;
  if (visibleRows < 1)
    visibleRows = 1;
  if (visibleRows > NEUT_ARG_VISIBLE_ROWS_MAX)
    visibleRows = NEUT_ARG_VISIBLE_ROWS_MAX;

  neutArgEditorClampScroll(ed, visibleRows);

  int leftX = vx1 + SCENE_KEEPOUT;
  int rightX = vx2 - SCENE_KEEPOUT;
  int y = vy1;
  for (int i = ed->scroll; i < ed->rowCount && i < ed->scroll + visibleRows; i++) {
    uint64_t col = (i == ed->cursor) ? ColorSelected : FontMainColor;
    unsigned char k = ed->rowKind[i];
    unsigned char s = ed->rowSub[i];

    if (k == RK_SECTION) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, sectionTitle((int)s), HeaderTextColor);
    } else if (k == RK_GC) {
      int on = (ed->gcState & NEUT_GC_OPTIONS[s].bit) ? 1 : 0;
      uiDrawSelectionIndicator(gs, leftX + 5, y + lineH / 2, 5, zOrder, col, on);
      fontRenderInRect(FONT_DEFAULT, leftX + 24, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, NEUT_GC_OPTIONS[s].label, col);
    } else if (k == RK_GSM) {
      int on = (ed->gsmState & NEUT_GSM_OPTIONS[s].bit) ? 1 : 0;
      uiDrawSelectionIndicator(gs, leftX + 5, y + lineH / 2, 5, zOrder, col, on);
      fontRenderInRect(FONT_DEFAULT, leftX + 24, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, NEUT_GSM_OPTIONS[s].label, col);
    } else if (k == RK_PATH) {
      char line[200];
      const char *pn = neutPathArgName((int)s);
      Argument *a = getArgument(ed->list, pn);
      char disp[120];
      trimPathDisplay(a && a->value && !a->isDisabled ? a->value : NULL, disp, sizeof(disp));
      snprintf(line, sizeof(line), "%s  %s", pn, disp);
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, line, col);
    } else if (k == RK_FLAG_LOGO) {
      uiDrawSelectionIndicator(gs, leftX + 5, y + lineH / 2, 5, zOrder, col, ed->logoOn);
      fontRenderInRect(FONT_DEFAULT, leftX + 24, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, "PS2 logo (-logo)", col);
    } else if (k == RK_FLAG_DBC) {
      uiDrawSelectionIndicator(gs, leftX + 5, y + lineH / 2, 5, zOrder, col, ed->dbcOn);
      fontRenderInRect(FONT_DEFAULT, leftX + 24, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, "Debug colors (-dbc)", col);
    } else if (k == RK_MENU_GC) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, "Compatibility (-gc)", col);
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, ">", HeaderTextColor);
    } else if (k == RK_MENU_GSM) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, "GSM (-gsm)", col);
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, ">", HeaderTextColor);
    } else if (k == RK_MENU_PATHS) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, "Device emulation (paths)", col);
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, ">", HeaderTextColor);
    } else if (k == RK_MENU_RAW) {
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, "Additional arguments (raw)", col);
      fontRenderInRect(FONT_DEFAULT, leftX, y, rightX, y + lineH, FONT_ALIGN_RIGHT | FONT_ALIGN_VCENTER, zOrder, ">", HeaderTextColor);
    } else if (k == RK_RAW_NONE) {
      fontRenderInRect(FONT_DEFAULT, leftX + 24, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder, "(no additional arguments)", FontMainColor);
    } else if (k == RK_RAW_ARG && (int)s >= 0 && (int)s < ed->rawArgCount) {
      Argument *ra = ed->rawArgs[(int)s];
      if (ra) {
        int en = !ra->isDisabled;
        uiDrawSelectionIndicator(gs, leftX + 5, y + lineH / 2, 5, zOrder, col, en);
        char line[200];
        char valdisp[120];
        trimPathDisplay(ra->value && ra->value[0] ? ra->value : NULL, valdisp, sizeof(valdisp));
        snprintf(line, sizeof(line), "-%s  %s", ra->arg ? ra->arg : "?", valdisp);
        fontRenderInRect(FONT_DEFAULT, leftX + 24, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, line, col);
      }
    } else if (k == RK_TITLE_FAV && ed->titleTarget) {
      int on = (ed->titleTarget->flags & TitleFlag_Favorite) ? 1 : 0;
      uiDrawSelectionIndicator(gs, leftX + 5, y + lineH / 2, 5, zOrder, col, on);
      fontRenderInRect(FONT_DEFAULT, leftX + 24, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, "Favorite", col);
    } else if (k == RK_TITLE_FAKE && ed->titleTarget) {
      int on = (ed->titleTarget->flags & TitleFlag_FakeDEV9) ? 1 : 0;
      uiDrawSelectionIndicator(gs, leftX + 5, y + lineH / 2, 5, zOrder, col, on);
      fontRenderInRect(FONT_DEFAULT, leftX + 24, y, rightX, y + lineH, FONT_ALIGN_LEFT | FONT_ALIGN_VCENTER, zOrder + 1, "Fake DEV9 (ATA-net)", col);
    }
    y += lineH;
  }
}
