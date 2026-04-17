#include "ui/neutrino/neutrino_arg_editor_priv.h"
#include "backends/backends.h"
#include "backends/target.h"
#include "config/arguments.h"
#include "devices/devices.h"
#include <string.h>

static int isKnownTableArg(const char *name) {
  if (!name)
    return 1;
  static const char *known[] = {
      NEUT_GC_ARG, NEUT_GSM_ARG, NEUT_LOGO_ARG, NEUT_DBC_ARG, NEUT_PATH_MC0, NEUT_PATH_MC1, NEUT_PATH_ATA0, NEUT_PATH_ATA0ID, NEUT_PATH_ATA1, NULL,
  };
  for (int i = 0; known[i]; i++) {
    if (!strcmp(name, known[i]))
      return 1;
  }
  return 0;
}

static int neutAta0PathActive(ArgumentList *list) {
  Argument *a = getArgument(list, NEUT_PATH_ATA0);
  return a && !a->isDisabled && a->value && a->value[0];
}

static void collectRawArgs(NeutArgEditor *ed) {
  ed->rawArgCount = 0;
  for (Argument *a = ed->list->first; a && ed->rawArgCount < NEUT_EDITOR_MAX_RAW; a = a->next) {
    if (a->arg && !isKnownTableArg(a->arg))
      ed->rawArgs[ed->rawArgCount++] = a;
  }
}

static void appendPathRows(NeutArgEditor *ed, int *rInOut) {
  int r = *rInOut;
  for (int i = 0; i < NEUT_NUM_PATH_ARGS && r < NEUT_EDITOR_MAX_ROWS; i++) {
    if (i == 3 && !neutAta0PathActive(ed->list))
      continue;
    ed->rowKind[r] = RK_PATH;
    ed->rowSub[r++] = (unsigned char)i;
  }
  *rInOut = r;
}

void neutArgEditorRebuildRows(NeutArgEditor *ed) {
  int r = 0;
  if (ed->layout == NeutLayout_TitleHub && ed->panel == 0) {
    Target *titleTarget = ed->titleTarget;
    if (titleTarget) {
      ed->rowKind[r] = RK_TITLE_FAV;
      ed->rowSub[r++] = 0;
      if (titleTarget->device && (titleTarget->device->type == Device_ATA || titleTarget->device->type == Device_HDD)) {
        ed->rowKind[r] = RK_TITLE_FAKE;
        ed->rowSub[r++] = 0;
      }
    }
    ed->rowKind[r] = RK_MENU_GC;
    ed->rowSub[r++] = 0;
    ed->rowKind[r] = RK_MENU_GSM;
    ed->rowSub[r++] = 0;
    ed->rowKind[r] = RK_MENU_PATHS;
    ed->rowSub[r++] = 0;
    if (r < NEUT_EDITOR_MAX_ROWS) {
      ed->rowKind[r] = RK_MENU_RAW;
      ed->rowSub[r++] = 0;
    }
    if (r < NEUT_EDITOR_MAX_ROWS) {
      ed->rowKind[r] = RK_FLAG_LOGO;
      ed->rowSub[r++] = 0;
    }
    if (r < NEUT_EDITOR_MAX_ROWS) {
      ed->rowKind[r] = RK_FLAG_DBC;
      ed->rowSub[r++] = 0;
    }
  } else if (ed->panel == 1) {
    ed->rowKind[r] = RK_SECTION;
    ed->rowSub[r++] = 0;
    for (int i = 0; i < NEUT_GC_OPTION_COUNT && r < NEUT_EDITOR_MAX_ROWS; i++) {
      ed->rowKind[r] = RK_GC;
      ed->rowSub[r++] = (unsigned char)i;
    }
  } else if (ed->panel == 2) {
    ed->rowKind[r] = RK_SECTION;
    ed->rowSub[r++] = 1;
    for (int i = 0; i < NEUT_GSM_OPTION_COUNT && r < NEUT_EDITOR_MAX_ROWS; i++) {
      ed->rowKind[r] = RK_GSM;
      ed->rowSub[r++] = (unsigned char)i;
    }
  } else if (ed->panel == 3) {
    ed->rowKind[r] = RK_SECTION;
    ed->rowSub[r++] = 2;
    appendPathRows(ed, &r);
  } else if (ed->panel == 4) {
    collectRawArgs(ed);
    if (r < NEUT_EDITOR_MAX_ROWS) {
      ed->rowKind[r] = RK_SECTION;
      ed->rowSub[r++] = 5;
    }
    if (ed->rawArgCount == 0 && r < NEUT_EDITOR_MAX_ROWS) {
      ed->rowKind[r] = RK_RAW_NONE;
      ed->rowSub[r++] = 0;
    } else {
      for (int j = 0; j < ed->rawArgCount && r < NEUT_EDITOR_MAX_ROWS; j++) {
        ed->rowKind[r] = RK_RAW_ARG;
        ed->rowSub[r++] = (unsigned char)j;
      }
    }
  }
  ed->rowCount = r;
  if (ed->cursor >= ed->rowCount)
    ed->cursor = ed->rowCount > 0 ? ed->rowCount - 1 : 0;
  if (ed->cursor < 0)
    ed->cursor = 0;
  ed->scroll = 0;
}
