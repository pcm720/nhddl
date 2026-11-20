#ifndef _UI_NEUTRINO_NEUTRINO_ARG_EDITOR_H_
#define _UI_NEUTRINO_NEUTRINO_ARG_EDITOR_H_

#include "config/arguments.h"
#include "ui/view.h"
#include <stdint.h>

struct BackendDevice;
typedef struct Target Target;

typedef enum {
  // Hub: plain toggles + sub-panels for gc/gsm/paths/raw. titleTarget NULL = global.cnf (no Favorite/Fake DEV9 rows).
  NeutrinoLayout_TitleHub = 0,
} NeutrinoLayout;

typedef struct NeutrinoArgEditor {
  ArgumentList *list;
  struct BackendDevice *pathRootDevice;
  Target *titleTarget;
  NeutrinoLayout layout;
  int panel;
  uint32_t gcState;
  uint32_t gsmState;
  int logoOn;
  int dbcOn;
  int rowCount;
#define NEUTRINO_EDITOR_MAX_ROWS 120
  unsigned char rowKind[NEUTRINO_EDITOR_MAX_ROWS];
  unsigned char rowSub[NEUTRINO_EDITOR_MAX_ROWS];
  int scroll;
  int cursor;
#define NEUTRINO_EDITOR_MAX_RAW 96
  Argument *rawArgs[NEUTRINO_EDITOR_MAX_RAW];
  int rawArgCount;
  void (*onListMutated)(void *userdata);
  void *onListMutatedUserdata;
} NeutrinoArgEditor;

// pathRootDevice: mount for path picker (use metadata device when set).
// titleTarget: NULL for global Neutrino (main menu); else Favorite/Fake DEV9 on hub when applicable.
void neutrinoArgEditorInit(NeutrinoArgEditor *ed, ArgumentList *list, struct BackendDevice *pathRootDevice, Target *titleTarget, NeutrinoLayout layout);

// Optional: invoked when Neutrino argument list content changes (paths, -gc/-gsm, flags, raw enable/disable).
void neutrinoArgEditorSetOnListMutated(NeutrinoArgEditor *ed, void (*fn)(void *userdata), void *userdata);

int neutrinoArgEditorOnTitleHub(const NeutrinoArgEditor *ed);
int neutrinoArgEditorPanel(const NeutrinoArgEditor *ed);
// Panel card header for TitleHub sub-panels; NULL on hub.
const char *neutrinoArgEditorPanelTitle(const NeutrinoArgEditor *ed);

void neutrinoArgEditorSyncFromList(NeutrinoArgEditor *ed);
void neutrinoArgEditorApplyToList(NeutrinoArgEditor *ed);

void neutrinoArgEditorDraw(NeutrinoArgEditor *ed, GSGLOBAL *gs, int zOrder, int vx1, int vy1, int vx2, int vy2);

// Pushes file selector on viewStack (ui_shared) when picking a path.
ViewResult neutrinoArgEditorOnInput(NeutrinoArgEditor *ed, int padInput);

#endif
