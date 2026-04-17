#include "neutrino/neutrino_arg_marshal.h"
#include "neutrino/neutrino_arg_defs.h"
#include "config/arguments.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void neutArgMarshalGcToList(uint32_t state, ArgumentList *list) {
  Argument *larg = getArgument(list, NEUT_GC_ARG);
  if (!state) {
    if (larg) {
      if (larg->value) {
        free(larg->value);
        larg->value = strdup("");
      }
      larg->isDisabled = 1;
    }
    return;
  }
  if (!larg)
    larg = insertArgument(list, NEUT_GC_ARG, "");
  else if (!larg->value)
    larg->value = strdup("");

  if (larg->value) {
    free(larg->value);
    larg->value = malloc((size_t)NEUT_GC_OPTION_COUNT + 1u);
  }
  if (!larg->value)
    return;
  int pos = 0;
  for (int i = 0; i < NEUT_GC_OPTION_COUNT; i++) {
    if (state & NEUT_GC_OPTIONS[i].bit)
      larg->value[pos++] = NEUT_GC_OPTIONS[i].digit[0];
  }
  larg->value[pos] = '\0';
  larg->isDisabled = 0;
}

void neutArgParseGcFromList(ArgumentList *list, uint32_t *stateOut) {
  *stateOut = 0;
  Argument *larg = getArgument(list, NEUT_GC_ARG);
  if (!larg || larg->isDisabled || !larg->value)
    return;
  for (size_t i = 0; i < strlen(larg->value); i++) {
    for (int j = 0; j < NEUT_GC_OPTION_COUNT; j++) {
      if (larg->value[i] == NEUT_GC_OPTIONS[j].digit[0]) {
        *stateOut |= NEUT_GC_OPTIONS[j].bit;
        break;
      }
    }
  }
}

void neutArgGsmApplyToggle(uint32_t *state, int idx) {
  if (idx < 0 || idx >= NEUT_GSM_OPTION_COUNT)
    return;
  switch (idx) {
  case 0:
    *state &= ~NEUT_GSM_OPTIONS[4].bit;
    *state &= ~NEUT_GSM_OPTIONS[3].bit;
    *state &= ~NEUT_GSM_OPTIONS[2].bit;
    *state &= ~NEUT_GSM_OPTIONS[1].bit;
    *state ^= NEUT_GSM_OPTIONS[0].bit;
    break;
  case 1:
    *state &= ~NEUT_GSM_OPTIONS[4].bit;
    *state &= ~NEUT_GSM_OPTIONS[3].bit;
    *state &= ~NEUT_GSM_OPTIONS[2].bit;
    *state &= ~NEUT_GSM_OPTIONS[0].bit;
    *state ^= NEUT_GSM_OPTIONS[1].bit;
    break;
  case 2:
    *state &= ~NEUT_GSM_OPTIONS[4].bit;
    *state &= ~NEUT_GSM_OPTIONS[3].bit;
    *state &= ~NEUT_GSM_OPTIONS[1].bit;
    *state &= ~NEUT_GSM_OPTIONS[0].bit;
    *state ^= NEUT_GSM_OPTIONS[2].bit;
    break;
  case 3:
    *state &= ~NEUT_GSM_OPTIONS[4].bit;
    *state &= ~NEUT_GSM_OPTIONS[2].bit;
    *state &= ~NEUT_GSM_OPTIONS[1].bit;
    *state &= ~NEUT_GSM_OPTIONS[0].bit;
    *state ^= NEUT_GSM_OPTIONS[3].bit;
    break;
  case 4:
    *state &= ~NEUT_GSM_OPTIONS[3].bit;
    *state &= ~NEUT_GSM_OPTIONS[2].bit;
    *state &= ~NEUT_GSM_OPTIONS[1].bit;
    *state &= ~NEUT_GSM_OPTIONS[0].bit;
    *state ^= NEUT_GSM_OPTIONS[4].bit;
    break;
  case 5:
    *state ^= NEUT_GSM_OPTIONS[5].bit;
    *state &= ~NEUT_GSM_OPTIONS[6].bit;
    *state &= ~NEUT_GSM_OPTIONS[7].bit;
    if (*state == NEUT_GSM_OPTIONS[5].bit)
      *state |= NEUT_GSM_OPTIONS[1].bit;
    break;
  case 6:
    *state ^= NEUT_GSM_OPTIONS[6].bit;
    *state &= ~NEUT_GSM_OPTIONS[5].bit;
    *state &= ~NEUT_GSM_OPTIONS[7].bit;
    if (*state == NEUT_GSM_OPTIONS[6].bit)
      *state |= NEUT_GSM_OPTIONS[1].bit;
    break;
  case 7:
    *state ^= NEUT_GSM_OPTIONS[7].bit;
    *state &= ~NEUT_GSM_OPTIONS[5].bit;
    *state &= ~NEUT_GSM_OPTIONS[6].bit;
    if (*state == NEUT_GSM_OPTIONS[7].bit)
      *state |= NEUT_GSM_OPTIONS[1].bit;
    break;
  }
  if (*state == NEUT_GSM_OPTIONS[5].bit || *state == NEUT_GSM_OPTIONS[6].bit || *state == NEUT_GSM_OPTIONS[7].bit)
    *state = 0;
}

void neutArgMarshalGsmToList(uint32_t state, ArgumentList *list) {
  Argument *larg = getArgument(list, NEUT_GSM_ARG);
  if (!state) {
    if (larg) {
      if (larg->value) {
        free(larg->value);
        larg->value = strdup("");
      }
      larg->isDisabled = 1;
    }
    return;
  }
  if (!larg)
    larg = insertArgument(list, NEUT_GSM_ARG, "");
  else if (!larg->value)
    larg->value = strdup("");

  const char *vmArg = "";
  const char *cmArg = "";
  if (state & NEUT_GSM_OPTIONS[0].bit)
    vmArg = NEUT_GSM_OPTIONS[0].value;
  else if (state & NEUT_GSM_OPTIONS[1].bit)
    vmArg = NEUT_GSM_OPTIONS[1].value;
  else if (state & NEUT_GSM_OPTIONS[2].bit)
    vmArg = NEUT_GSM_OPTIONS[2].value;
  else if (state & NEUT_GSM_OPTIONS[3].bit)
    vmArg = NEUT_GSM_OPTIONS[3].value;
  else if (state & NEUT_GSM_OPTIONS[4].bit)
    vmArg = NEUT_GSM_OPTIONS[4].value;
  if (state & NEUT_GSM_OPTIONS[5].bit)
    cmArg = NEUT_GSM_OPTIONS[5].value;
  else if (state & NEUT_GSM_OPTIONS[6].bit)
    cmArg = NEUT_GSM_OPTIONS[6].value;
  else if (state & NEUT_GSM_OPTIONS[7].bit)
    cmArg = NEUT_GSM_OPTIONS[7].value;

  if (larg->value)
    free(larg->value);
  larg->value = malloc(16);
  if (!larg->value)
    return;
  snprintf(larg->value, 16, "%s%s", vmArg, cmArg);
  larg->isDisabled = 0;
}

void neutArgParseGsmFromList(ArgumentList *list, uint32_t *stateOut) {
  *stateOut = 0;
  Argument *larg = getArgument(list, NEUT_GSM_ARG);
  if (!larg || larg->isDisabled || !larg->value || larg->value[0] == '\0') {
    if (larg && larg->isDisabled && larg->value) {
      free(larg->value);
      larg->value = strdup("");
    }
    return;
  }
  char *argptr = larg->value;
  if (!strncmp(argptr, "fp", 2)) {
    if (argptr[2] == '1')
      *stateOut |= NEUT_GSM_OPTIONS[0].bit;
    else if (argptr[2] == '2')
      *stateOut |= NEUT_GSM_OPTIONS[1].bit;
    else
      goto fail;
    argptr += 3;
  } else if (!strncmp(argptr, "1080ix", 6)) {
    if (argptr[6] == '1')
      *stateOut |= NEUT_GSM_OPTIONS[2].bit;
    else if (argptr[6] == '2')
      *stateOut |= NEUT_GSM_OPTIONS[3].bit;
    else if (argptr[6] == '3')
      *stateOut |= NEUT_GSM_OPTIONS[4].bit;
    else
      goto fail;
    argptr += 7;
  } else
    goto fail;
  if (argptr[0] == ':') {
    argptr++;
    if (argptr[0] == '1')
      *stateOut |= NEUT_GSM_OPTIONS[5].bit;
    else if (argptr[0] == '2')
      *stateOut |= NEUT_GSM_OPTIONS[6].bit;
    else if (argptr[0] == '3')
      *stateOut |= NEUT_GSM_OPTIONS[7].bit;
  }
  return;
fail:
  *stateOut = 0;
  larg->isDisabled = 1;
}

void neutArgMarshalFlag(const char *argName, int on, ArgumentList *list) {
  Argument *larg = getArgument(list, argName);
  if (!on) {
    if (!larg)
      return;
    larg->isDisabled = 1;
    return;
  }
  if (!larg)
    larg = insertArgument(list, argName, NULL);
  larg->isDisabled = 0;
}

void neutArgParseFlag(const char *argName, ArgumentList *list, int *onOut) {
  *onOut = 0;
  Argument *larg = getArgument(list, argName);
  if (larg && !larg->isDisabled)
    *onOut = 1;
}

void neutArgPathSet(ArgumentList *list, const char *argName, const char *fullPath) {
  /* Empty path must stay in the list so per-title delta save can write `-name` and
   * override global.cnf; removing the row would omit it from the delta file and global
   * would win again after reload. */
  if (!fullPath || !fullPath[0]) {
    Argument *larg = getArgument(list, argName);
    if (!larg)
      larg = insertArgument(list, argName, NULL);
    else if (larg->value) {
      free(larg->value);
      larg->value = NULL;
    }
    larg->isDisabled = 0;
    return;
  }
  Argument *larg = getArgument(list, argName);
  if (!larg) {
    larg = insertArgument(list, argName, (char *)fullPath);
    larg->isDisabled = 0;
    return;
  }
  if (larg->value)
    free(larg->value);
  larg->value = strdup(fullPath);
  larg->isDisabled = 0;
}
