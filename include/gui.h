#ifndef _GUI_H_
#define _GUI_H_

#include "iso.h"

int uiInit(int enable480p);
int uiLoop(struct TargetList *titles);
void uiCleanup();

#endif