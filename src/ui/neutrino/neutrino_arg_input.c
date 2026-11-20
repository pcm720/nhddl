#include "backends/backends.h"
#include "backends/target.h"
#include "config/arguments.h"
#include "devices/pad.h"
#include "devices/utils.h"
#include "ui/neutrino/neutrino_arg_editor_priv.h"
#include "ui/strings.h"
#include "ui/ui_shared.h"
#include "ui/views/file_selector_scene.h"
#include <stdio.h>
#include <string.h>

static void pathSetNotify(NeutrinoArgEditor *ed, const char *argName, const char *fullPath) {
  const char *store = fullPath;
  if (fullPath && fullPath[0]) {
    int relIdx = getRelativePathIdx((char *)fullPath);
    if (relIdx >= 0)
      store = fullPath + relIdx;
    else if (ed && ed->pathRootDevice && ed->pathRootDevice->mountpoint) {
      const char *mount = ed->pathRootDevice->mountpoint;
      size_t mlen = strlen(mount);
      if (mlen > 0 && !strncmp(fullPath, mount, mlen))
        store = fullPath + mlen;
    }
  }
  neutrinoArgPathSet(ed->list, argName, store);
  neutrinoArgNotifyListMutated(ed);
}

static NeutrinoArgEditor *s_pickEditor;
static int s_pickPathIdx = -1;

static void onPathFilePicked(const char *path, void *userdata) {
  if (!s_pickEditor || s_pickPathIdx < 0 || s_pickPathIdx >= NEUTRINO_NUM_PATH_ARGS)
    return;
  pathSetNotify(s_pickEditor, neutrinoPathArgName(s_pickPathIdx), path);
  if (s_pickEditor->panel == 3)
    neutrinoArgEditorRebuildRows(s_pickEditor);
  s_pickEditor = NULL;
  s_pickPathIdx = -1;
}

static void neutrinoPathPickDismiss(void *userdata) {
  s_pickEditor = NULL;
  s_pickPathIdx = -1;
}

ViewResult neutrinoArgEditorOnInput(NeutrinoArgEditor *ed, int padInput) {
  int visibleRows = NEUTRINO_ARG_VISIBLE_ROWS_MAX;

  if (ed->panel > 0 && (padInput & PAD_TRIANGLE)) {
    ed->panel = 0;
    neutrinoArgEditorRebuildRows(ed);
    return ViewResult_Continue;
  }

  if (padInput & PAD_UP) {
    ed->cursor--;
    if (ed->cursor < 0)
      ed->cursor = ed->rowCount - 1;
    neutrinoArgEditorClampScroll(ed, visibleRows);
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    ed->cursor++;
    if (ed->cursor >= ed->rowCount)
      ed->cursor = 0;
    neutrinoArgEditorClampScroll(ed, visibleRows);
    return ViewResult_Continue;
  }

  if (ed->rowCount <= 0)
    return ViewResult_Continue;
  unsigned char k = ed->rowKind[ed->cursor];
  unsigned char s = ed->rowSub[ed->cursor];

  if (k == RK_PATH && (padInput & PAD_SQUARE)) {
    pathSetNotify(ed, neutrinoPathArgName((int)s), NULL);
    if (ed->panel == 3 && (int)s == 2)
      neutrinoArgEditorRebuildRows(ed);
    return ViewResult_Continue;
  }

  if (k == RK_PATH && (padInput & PAD_SELECT_ACTION)) {
    if (!ed->pathRootDevice || !ed->pathRootDevice->mountpoint || !ed->pathRootDevice->mountpoint[0])
      return ViewResult_Continue;
    s_pickEditor = ed;
    s_pickPathIdx = (int)s;
    fileSelectorSetDismissNotifier(neutrinoPathPickDismiss, NULL);
    {
      char heading[48];
      snprintf(heading, sizeof(heading), uiStr(UI_STR_SELECT_FILE_FOR_FMT), neutrinoPathArgName((int)s));
      fileSelectorConfigureForMount(ed->pathRootDevice->mountpoint, onPathFilePicked, NULL, heading);
    }
    if (viewStack)
      viewStackPush(viewStack, fileSelectorGetView());
    return ViewResult_Continue;
  }

  if (padInput & PAD_SELECT_ACTION) {
    if (k == RK_MENU_GC) {
      ed->panel = 1;
      neutrinoArgEditorRebuildRows(ed);
    } else if (k == RK_MENU_GSM) {
      ed->panel = 2;
      neutrinoArgEditorRebuildRows(ed);
    } else if (k == RK_MENU_PATHS) {
      ed->panel = 3;
      neutrinoArgEditorRebuildRows(ed);
    } else if (k == RK_MENU_RAW) {
      ed->panel = 4;
      neutrinoArgEditorRebuildRows(ed);
    } else if (k == RK_TITLE_FAV && ed->titleTarget) {
      uint32_t f = ed->titleTarget->flags ^ TitleFlag_Favorite;
      updateTargetFlagsAndPersist(ed->titleTarget->device, ed->titleTarget, f);
    } else if (k == RK_TITLE_FAKE && ed->titleTarget) {
      uint32_t f = ed->titleTarget->flags ^ TitleFlag_FakeDEV9;
      updateTargetFlagsAndPersist(ed->titleTarget->device, ed->titleTarget, f);
    } else if (k == RK_GC) {
      ed->gcState ^= NEUTRINO_GC_OPTIONS[s].bit;
      neutrinoArgMarshalGcToList(ed->gcState, ed->list);
      neutrinoArgNotifyListMutated(ed);
    } else if (k == RK_GSM) {
      neutrinoArgGsmApplyToggle(&ed->gsmState, (int)s);
      neutrinoArgMarshalGsmToList(ed->gsmState, ed->list);
      neutrinoArgNotifyListMutated(ed);
    } else if (k == RK_FLAG_LOGO) {
      ed->logoOn = !ed->logoOn;
      neutrinoArgMarshalFlag(NEUTRINO_LOGO_ARG, ed->logoOn, ed->list);
      neutrinoArgNotifyListMutated(ed);
    } else if (k == RK_FLAG_DBC) {
      ed->dbcOn = !ed->dbcOn;
      neutrinoArgMarshalFlag(NEUTRINO_DBC_ARG, ed->dbcOn, ed->list);
      neutrinoArgNotifyListMutated(ed);
    } else if (k == RK_RAW_ARG && (int)s >= 0 && (int)s < ed->rawArgCount) {
      Argument *ra = ed->rawArgs[(int)s];
      if (ra) {
        ra->isDisabled = ra->isDisabled ? 0 : 1;
        neutrinoArgNotifyListMutated(ed);
      }
    }
  }
  return ViewResult_Continue;
}
