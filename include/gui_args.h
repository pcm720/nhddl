#ifndef _GUI_ARGS_H_
#define _GUI_ARGS_H_

#include "options.h"

// Parses compatibility mode argument value into a bitmask
uint8_t parseCompatModes(char *stringValue);

// Stores compatibility mode from bitmask into argument value and sets isDisabled flag accordingly.
// Target must be at least 6 bytes long, including null terminator
void storeCompatModes(Argument *target, uint8_t modes);

// Inserts a new compat mode arg into the argument list
void insertCompatModeArg(ArgumentList *target, uint8_t modes);

#endif
