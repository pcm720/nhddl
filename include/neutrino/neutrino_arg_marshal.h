#ifndef _NEUTRINO_NEUTRINO_ARG_MARSHAL_H_
#define _NEUTRINO_NEUTRINO_ARG_MARSHAL_H_

#include "config/arguments.h"
#include <stdint.h>

// Sync structured Neutrino options with an ArgumentList (CNF-style entries).

void neutArgMarshalGcToList(uint32_t state, ArgumentList *list);
void neutArgParseGcFromList(ArgumentList *list, uint32_t *stateOut);
void neutArgGsmApplyToggle(uint32_t *state, int idx);
void neutArgMarshalGsmToList(uint32_t state, ArgumentList *list);
void neutArgParseGsmFromList(ArgumentList *list, uint32_t *stateOut);
void neutArgMarshalFlag(const char *argName, int on, ArgumentList *list);
void neutArgParseFlag(const char *argName, ArgumentList *list, int *onOut);
void neutArgPathSet(ArgumentList *list, const char *argName, const char *fullPath);

#endif
