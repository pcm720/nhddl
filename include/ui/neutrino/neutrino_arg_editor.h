#ifndef _UI_NEUTRINO_NEUTRINO_ARG_EDITOR_H_
#define _UI_NEUTRINO_NEUTRINO_ARG_EDITOR_H_

#include "config/arguments.h"
#include "ui/view.h"
#include <stdint.h>

struct BackendDevice;
typedef struct Target Target;

typedef enum {
  // Hub: plain toggles + sub-panels for gc/gsm/paths/raw. titleTarget NULL = global.cnf (no Favorite / Fake DEV9 rows).
  NeutLayout_TitleHub = 0,
} NeutLayout;

typedef struct NeutArgEditor {
  ArgumentList *list;
  struct BackendDevice *pathRootDevice;
  Target *titleTarget;
  NeutLayout layout;
  int panel;
  uint32_t gcState;
  uint32_t gsmState;
  int logoOn;
  int dbcOn;
  int rowCount;
#define NEUT_EDITOR_MAX_ROWS 120
  unsigned char rowKind[NEUT_EDITOR_MAX_ROWS];
  unsigned char rowSub[NEUT_EDITOR_MAX_ROWS];
  int scroll;
  int cursor;
#define NEUT_EDITOR_MAX_RAW 96
  Argument *rawArgs[NEUT_EDITOR_MAX_RAW];
  int rawArgCount;
  void (*onListMutated)(void *userdata);
  void *onListMutatedUserdata;
} NeutArgEditor;

// pathRootDevice: mount for path picker (use metadata device when set).
// titleTarget: NULL for global Neutrino (main menu); else Favorite / Fake DEV9 on hub when applicable.
void neutArgEditorInit(NeutArgEditor *ed, ArgumentList *list, struct BackendDevice *pathRootDevice, Target *titleTarget, NeutLayout layout);

// Optional: invoked when Neutrino argument list content changes (paths, -gc/-gsm, flags, raw enable/disable).
void neutArgEditorSetOnListMutated(NeutArgEditor *ed, void (*fn)(void *userdata), void *userdata);

// After a modal (e.g. file picker) closes: sync toggles from list, rebuild row model, keep current panel/submenu.
void neutArgEditorResumeAfterModal(NeutArgEditor *ed, ArgumentList *list, struct BackendDevice *pathRootDevice, Target *titleTarget);

int neutArgEditorOnTitleHub(const NeutArgEditor *ed);
int neutArgEditorPanel(const NeutArgEditor *ed);

void neutArgEditorSyncFromList(NeutArgEditor *ed);
void neutArgEditorApplyToList(NeutArgEditor *ed);

void neutArgEditorDraw(NeutArgEditor *ed, GSGLOBAL *gs, int zOrder, int vx1, int vy1, int vx2, int vy2);

// Pushes file selector on viewStack (ui_shared) when picking a path.
ViewResult neutArgEditorOnInput(NeutArgEditor *ed, int padInput);

#endif
