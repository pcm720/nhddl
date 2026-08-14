#ifndef _DEVICES_INIT_H_
#define _DEVICES_INIT_H_

#include "common.h"

// Initializes IOP modules
int initModules(ModeType modeType);

// Runs OPL's one-shot SPU2 reset module. A successful module intentionally
// returns MODULE_NO_RESIDENT_END and does not remain loaded.
int resetSpuOnce(void);

#endif
