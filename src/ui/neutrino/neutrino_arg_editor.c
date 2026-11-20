#include "ui/neutrino/neutrino_arg_editor.h"
#include "ui/neutrino/neutrino_arg_editor_priv.h"
#include <string.h>

void neutrinoArgNotifyListMutated(NeutrinoArgEditor *ed) {
  if (ed && ed->onListMutated)
    ed->onListMutated(ed->onListMutatedUserdata);
}

int neutrinoArgEditorOnTitleHub(const NeutrinoArgEditor *ed) { return ed && ed->layout == NeutrinoLayout_TitleHub && ed->panel == 0; }

int neutrinoArgEditorPanel(const NeutrinoArgEditor *ed) { return ed ? ed->panel : 0; }

void neutrinoArgEditorSetOnListMutated(NeutrinoArgEditor *ed, void (*fn)(void *userdata), void *userdata) {
  if (!ed)
    return;
  ed->onListMutated = fn;
  ed->onListMutatedUserdata = userdata;
}

void neutrinoArgEditorInit(NeutrinoArgEditor *ed, ArgumentList *list, struct BackendDevice *pathRootDevice, Target *titleTarget,
                           NeutrinoLayout layout) {
  memset(ed, 0, sizeof(*ed));
  ed->list = list;
  ed->pathRootDevice = pathRootDevice;
  ed->titleTarget = titleTarget;
  ed->layout = layout;
  ed->panel = 0;
  neutrinoArgEditorRebuildRows(ed);
}

void neutrinoArgEditorSyncFromList(NeutrinoArgEditor *ed) {
  neutrinoArgParseGcFromList(ed->list, &ed->gcState);
  neutrinoArgParseGsmFromList(ed->list, &ed->gsmState);
  neutrinoArgParseFlag(NEUTRINO_LOGO_ARG, ed->list, &ed->logoOn);
  neutrinoArgParseFlag(NEUTRINO_DBC_ARG, ed->list, &ed->dbcOn);
}

void neutrinoArgEditorApplyToList(NeutrinoArgEditor *ed) {
  neutrinoArgMarshalGcToList(ed->gcState, ed->list);
  neutrinoArgMarshalGsmToList(ed->gsmState, ed->list);
  neutrinoArgMarshalFlag(NEUTRINO_LOGO_ARG, ed->logoOn, ed->list);
  neutrinoArgMarshalFlag(NEUTRINO_DBC_ARG, ed->dbcOn, ed->list);
}
