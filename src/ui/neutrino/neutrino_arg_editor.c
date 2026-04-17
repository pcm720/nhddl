#include "ui/neutrino/neutrino_arg_editor.h"
#include "ui/neutrino/neutrino_arg_editor_priv.h"
#include <string.h>

void neutArgNotifyListMutated(NeutArgEditor *ed) {
  if (ed && ed->onListMutated)
    ed->onListMutated(ed->onListMutatedUserdata);
}

int neutArgEditorOnTitleHub(const NeutArgEditor *ed) {
  return ed && ed->layout == NeutLayout_TitleHub && ed->panel == 0;
}

int neutArgEditorPanel(const NeutArgEditor *ed) {
  return ed ? ed->panel : 0;
}

void neutArgEditorSetOnListMutated(NeutArgEditor *ed, void (*fn)(void *userdata), void *userdata) {
  if (!ed)
    return;
  ed->onListMutated = fn;
  ed->onListMutatedUserdata = userdata;
}

void neutArgEditorInit(NeutArgEditor *ed, ArgumentList *list, struct BackendDevice *pathRootDevice, Target *titleTarget, NeutLayout layout) {
  memset(ed, 0, sizeof(*ed));
  ed->list = list;
  ed->pathRootDevice = pathRootDevice;
  ed->titleTarget = titleTarget;
  ed->layout = layout;
  ed->panel = 0;
  neutArgEditorRebuildRows(ed);
}

void neutArgEditorSyncFromList(NeutArgEditor *ed) {
  neutArgParseGcFromList(ed->list, &ed->gcState);
  neutArgParseGsmFromList(ed->list, &ed->gsmState);
  neutArgParseFlag(NEUT_LOGO_ARG, ed->list, &ed->logoOn);
  neutArgParseFlag(NEUT_DBC_ARG, ed->list, &ed->dbcOn);
}

void neutArgEditorResumeAfterModal(NeutArgEditor *ed, ArgumentList *list, struct BackendDevice *pathRootDevice, Target *titleTarget) {
  if (!ed || !list)
    return;
  ed->list = list;
  ed->pathRootDevice = pathRootDevice;
  ed->titleTarget = titleTarget;
  neutArgEditorSyncFromList(ed);
  neutArgEditorRebuildRows(ed);
}

void neutArgEditorApplyToList(NeutArgEditor *ed) {
  neutArgMarshalGcToList(ed->gcState, ed->list);
  neutArgMarshalGsmToList(ed->gsmState, ed->list);
  neutArgMarshalFlag(NEUT_LOGO_ARG, ed->logoOn, ed->list);
  neutArgMarshalFlag(NEUT_DBC_ARG, ed->dbcOn, ed->list);
}
