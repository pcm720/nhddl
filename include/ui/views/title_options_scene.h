#ifndef _UI_VIEWS_TITLE_OPTIONS_SCENE_H_
#define _UI_VIEWS_TITLE_OPTIONS_SCENE_H_

#include "backends/target.h"

typedef struct TitleOptionsSelectorData {
  Target *target;               // Selected title
} TitleOptionsSelectorData;

struct View *titleOptionsSelectorGetView(void);

#endif
