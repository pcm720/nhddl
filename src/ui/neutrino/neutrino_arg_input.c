#include "ui/neutrino/neutrino_arg_editor_priv.h"
#include "backends/backends.h"
#include "backends/target.h"
#include "config/arguments.h"
#include "ui/font.h"
#include "ui/layout.h"
#include "ui/ui_shared.h"
#include "ui/views/file_selector_scene.h"
#include <libpad.h>

static void pathSetNotify(NeutArgEditor *ed, const char *argName, const char *fullPath) {
  neutArgPathSet(ed->list, argName, fullPath);
  neutArgNotifyListMutated(ed);
}

static NeutArgEditor *s_pickEditor;
static int s_pickPathIdx = -1;

static void onPathFilePicked(const char *path, void *userdata) {
  (void)userdata;
  if (!s_pickEditor || s_pickPathIdx < 0 || s_pickPathIdx >= NEUT_NUM_PATH_ARGS)
    return;
  pathSetNotify(s_pickEditor, neutPathArgName(s_pickPathIdx), path);
  if (s_pickEditor->panel == 3)
    neutArgEditorRebuildRows(s_pickEditor);
  s_pickEditor = NULL;
  s_pickPathIdx = -1;
}

static void neutPathPickDismiss(void *userdata) {
  (void)userdata;
  s_pickEditor = NULL;
  s_pickPathIdx = -1;
}

ViewResult neutArgEditorOnInput(NeutArgEditor *ed, int padInput) {
  int lineH = fontGetLineHeight(FONT_DEFAULT);
  if (lineH <= 0)
    lineH = SCENE_ROW_SIZE;
  int visibleRows = NEUT_ARG_VISIBLE_ROWS_MAX;

  if (ed->panel > 0 && (padInput & PAD_CIRCLE)) {
    ed->panel = 0;
    neutArgEditorRebuildRows(ed);
    return ViewResult_Continue;
  }

  if (padInput & PAD_UP) {
    ed->cursor--;
    if (ed->cursor < 0)
      ed->cursor = ed->rowCount - 1;
    neutArgEditorClampScroll(ed, visibleRows);
    return ViewResult_Continue;
  }
  if (padInput & PAD_DOWN) {
    ed->cursor++;
    if (ed->cursor >= ed->rowCount)
      ed->cursor = 0;
    neutArgEditorClampScroll(ed, visibleRows);
    return ViewResult_Continue;
  }

  if (ed->rowCount <= 0)
    return ViewResult_Continue;
  unsigned char k = ed->rowKind[ed->cursor];
  unsigned char s = ed->rowSub[ed->cursor];

  if (k == RK_PATH && padInput & PAD_TRIANGLE) {
    pathSetNotify(ed, neutPathArgName((int)s), NULL);
    if (ed->panel == 3 && (int)s == 2)
      neutArgEditorRebuildRows(ed);
    return ViewResult_Continue;
  }

  if (k == RK_PATH && padInput & PAD_CROSS) {
    if (!ed->pathRootDevice || !ed->pathRootDevice->mountpoint || !ed->pathRootDevice->mountpoint[0])
      return ViewResult_Continue;
    s_pickEditor = ed;
    s_pickPathIdx = (int)s;
    fileSelectorSetDismissNotifier(neutPathPickDismiss, NULL);
    fileSelectorConfigureForMount(ed->pathRootDevice->mountpoint, onPathFilePicked, NULL);
    if (viewStack)
      viewStackPush(viewStack, fileSelectorGetView());
    return ViewResult_Continue;
  }

  if (padInput & PAD_CROSS) {
    if (k == RK_MENU_GC) {
      ed->panel = 1;
      neutArgEditorRebuildRows(ed);
    } else if (k == RK_MENU_GSM) {
      ed->panel = 2;
      neutArgEditorRebuildRows(ed);
    } else if (k == RK_MENU_PATHS) {
      ed->panel = 3;
      neutArgEditorRebuildRows(ed);
    } else if (k == RK_MENU_RAW) {
      ed->panel = 4;
      neutArgEditorRebuildRows(ed);
    } else if (k == RK_TITLE_FAV && ed->titleTarget) {
      uint32_t f = ed->titleTarget->flags ^ TitleFlag_Favorite;
      updateTargetFlagsAndPersist(ed->titleTarget->device, ed->titleTarget, f);
    } else if (k == RK_TITLE_FAKE && ed->titleTarget) {
      uint32_t f = ed->titleTarget->flags ^ TitleFlag_FakeDEV9;
      updateTargetFlagsAndPersist(ed->titleTarget->device, ed->titleTarget, f);
    } else if (k == RK_GC) {
      ed->gcState ^= NEUT_GC_OPTIONS[s].bit;
      neutArgMarshalGcToList(ed->gcState, ed->list);
      neutArgNotifyListMutated(ed);
    } else if (k == RK_GSM) {
      neutArgGsmApplyToggle(&ed->gsmState, (int)s);
      neutArgMarshalGsmToList(ed->gsmState, ed->list);
      neutArgNotifyListMutated(ed);
    } else if (k == RK_FLAG_LOGO) {
      ed->logoOn = !ed->logoOn;
      neutArgMarshalFlag(NEUT_LOGO_ARG, ed->logoOn, ed->list);
      neutArgNotifyListMutated(ed);
    } else if (k == RK_FLAG_DBC) {
      ed->dbcOn = !ed->dbcOn;
      neutArgMarshalFlag(NEUT_DBC_ARG, ed->dbcOn, ed->list);
      neutArgNotifyListMutated(ed);
    } else if (k == RK_RAW_ARG && (int)s >= 0 && (int)s < ed->rawArgCount) {
      Argument *ra = ed->rawArgs[(int)s];
      if (ra) {
        ra->isDisabled = ra->isDisabled ? 0 : 1;
        neutArgNotifyListMutated(ed);
      }
    }
  }
  return ViewResult_Continue;
}
