#include "backends/backends.h"
#include "backends/target.h"
#include "config/arguments.h"
#include "devices/devices.h"
#include "ui/neutrino/neutrino_arg_editor_priv.h"
#include <string.h>

static int isKnownTableArg(const char *name) {
  if (!name)
    return 1;
  static const char *known[] = {
      NEUTRINO_GC_ARG,   NEUTRINO_GSM_ARG,   NEUTRINO_LOGO_ARG,    NEUTRINO_DBC_ARG,   NEUTRINO_PATH_MC0,
      NEUTRINO_PATH_MC1, NEUTRINO_PATH_ATA0, NEUTRINO_PATH_ATA0ID, NEUTRINO_PATH_ATA1, NULL,
  };
  for (int i = 0; known[i]; i++) {
    if (!strcmp(name, known[i]))
      return 1;
  }
  return 0;
}

static int neutrinoAta0PathActive(ArgumentList *list) {
  Argument *a = getArgument(list, NEUTRINO_PATH_ATA0);
  return a && !a->isDisabled && a->value && a->value[0];
}

static void collectRawArgs(NeutrinoArgEditor *ed) {
  ed->rawArgCount = 0;
  for (Argument *a = ed->list->first; a && ed->rawArgCount < NEUTRINO_EDITOR_MAX_RAW; a = a->next) {
    if (a->arg && !isKnownTableArg(a->arg))
      ed->rawArgs[ed->rawArgCount++] = a;
  }
}

static void appendPathRows(NeutrinoArgEditor *ed, int *rInOut) {
  int r = *rInOut;
  for (int i = 0; i < NEUTRINO_NUM_PATH_ARGS && r < NEUTRINO_EDITOR_MAX_ROWS; i++) {
    if (i == 3 && !neutrinoAta0PathActive(ed->list))
      continue;
    ed->rowKind[r] = RK_PATH;
    ed->rowSub[r++] = (unsigned char)i;
  }
  *rInOut = r;
}

void neutrinoArgEditorRebuildRows(NeutrinoArgEditor *ed) {
  int r = 0;
  if (ed->layout == NeutrinoLayout_TitleHub && ed->panel == 0) {
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
    if (r < NEUTRINO_EDITOR_MAX_ROWS) {
      ed->rowKind[r] = RK_MENU_RAW;
      ed->rowSub[r++] = 0;
    }
    if (r < NEUTRINO_EDITOR_MAX_ROWS) {
      ed->rowKind[r] = RK_FLAG_LOGO;
      ed->rowSub[r++] = 0;
    }
    if (r < NEUTRINO_EDITOR_MAX_ROWS) {
      ed->rowKind[r] = RK_FLAG_DBC;
      ed->rowSub[r++] = 0;
    }
  } else if (ed->panel == 1) {
    // Section title is drawn as the content-panel header for TitleHub.
    for (int i = 0; i < NEUTRINO_GC_OPTION_COUNT && r < NEUTRINO_EDITOR_MAX_ROWS; i++) {
      ed->rowKind[r] = RK_GC;
      ed->rowSub[r++] = (unsigned char)i;
    }
  } else if (ed->panel == 2) {
    for (int i = 0; i < NEUTRINO_GSM_OPTION_COUNT && r < NEUTRINO_EDITOR_MAX_ROWS; i++) {
      ed->rowKind[r] = RK_GSM;
      ed->rowSub[r++] = (unsigned char)i;
    }
  } else if (ed->panel == 3) {
    appendPathRows(ed, &r);
  } else if (ed->panel == 4) {
    collectRawArgs(ed);
    if (ed->rawArgCount == 0 && r < NEUTRINO_EDITOR_MAX_ROWS) {
      ed->rowKind[r] = RK_RAW_NONE;
      ed->rowSub[r++] = 0;
    } else {
      for (int j = 0; j < ed->rawArgCount && r < NEUTRINO_EDITOR_MAX_ROWS; j++) {
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
