#ifndef _UI_NEUTRINO_NEUTRINO_ARG_EDITOR_PRIV_H_
#define _UI_NEUTRINO_NEUTRINO_ARG_EDITOR_PRIV_H_

#include "config/arguments.h"
#include "neutrino/neutrino_arg_defs.h"
#include "neutrino/neutrino_arg_marshal.h"
#include "ui/neutrino/neutrino_arg_editor.h"
#include <stdint.h>

#define RK_SECTION 1
#define RK_GC 2
#define RK_GSM 3
#define RK_PATH 4
#define RK_FLAG_LOGO 5
#define RK_FLAG_DBC 6
#define RK_TITLE_FAV 7
#define RK_TITLE_FAKE 8
#define RK_MENU_GC 12
#define RK_MENU_GSM 13
#define RK_MENU_PATHS 14
#define RK_MENU_RAW 15
#define RK_RAW_ARG 17
#define RK_RAW_NONE 18

#define NEUT_ARG_VISIBLE_ROWS_MAX 14

void neutArgNotifyListMutated(NeutArgEditor *ed);
void neutArgEditorRebuildRows(NeutArgEditor *ed);
void neutArgEditorClampScroll(NeutArgEditor *ed, int visibleRows);

#endif
