#include "gui_args.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Inserts a new compat mode arg into the argument list
void insertCompatModeArg(ArgumentList *target, uint8_t modes) {
  Argument *newArg = calloc(sizeof(Argument), 1);
  newArg->arg = strdup(COMPAT_MODES_ARG);

  newArg->value = calloc(sizeof(char), CM_NUM_MODES + 1);
  storeCompatModes(newArg, modes);

  target->total++;

  // Put at the start of the list
  newArg->next = target->first;
  target->first = newArg;
  if (target->last == NULL)
    target->last = newArg;
}

// Parses compatibility mode argument value into a bitmask
uint8_t parseCompatModes(char *stringValue) {
  uint8_t result = 0;
  for (int i = 0; i < strlen(stringValue); i++) {
    for (int j = 0; j < CM_NUM_MODES; j++) {
      if (stringValue[i] == COMPAT_MODE_MAP[j].value) {
        result |= COMPAT_MODE_MAP[j].mode;
        break;
      }
    }
  }
  return result;
}

// Stores compatibility mode from bitmask into argument value and sets isDisabled flag accordingly.
// Target must be at least 6 bytes long, including null terminator
void storeCompatModes(Argument *target, uint8_t modes) {
  int pos = 0;

  for (int i = 0; i < CM_NUM_MODES; i++) {
    if (modes & COMPAT_MODE_MAP[i].mode) {
      target->value[pos] = COMPAT_MODE_MAP[i].value;
      pos++;
    }
  }

  target->value[pos] = '\0';

  if (!pos)
    target->isDisabled = 1;
  else
    target->isDisabled = 0;
}
