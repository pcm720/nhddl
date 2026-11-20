#ifndef _NEUTRINO_NEUTRINO_ARG_MARSHAL_H_
#define _NEUTRINO_NEUTRINO_ARG_MARSHAL_H_

#include "config/arguments.h"
#include <stdint.h>

// Sync structured Neutrino options with an ArgumentList
void neutrinoArgMarshalGcToList(uint32_t state, ArgumentList *list);
void neutrinoArgParseGcFromList(ArgumentList *list, uint32_t *stateOut);
void neutrinoArgGsmApplyToggle(uint32_t *state, int idx);
void neutrinoArgMarshalGsmToList(uint32_t state, ArgumentList *list);
void neutrinoArgParseGsmFromList(ArgumentList *list, uint32_t *stateOut);
void neutrinoArgMarshalFlag(const char *argName, int on, ArgumentList *list);
void neutrinoArgParseFlag(const char *argName, ArgumentList *list, int *onOut);
void neutrinoArgPathSet(ArgumentList *list, const char *argName, const char *fullPath);

#endif
