#ifndef _GUI_H_
#define _GUI_H_

#include "iso.h"

int uiInit();
int uiLoop(struct TargetList *titles);
void uiCleanup();

#endif