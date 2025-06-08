// Implements support for known Neutrino arguments
#include "gui_args.h"
#include "gui_graphics.h"
#include "options.h"
#include <libpad.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Compatibility modes handlers
int gcDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight);
ActionType gcInput(NeutrinoArgument *arg, int input);
void gcMarshal(NeutrinoArgument *arg, ArgumentList *list);
void gcParse(NeutrinoArgument *arg, ArgumentList *list);

// GSM handlers
int gsmDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight);
ActionType gsmInput(NeutrinoArgument *arg, int input);
void gsmMarshal(NeutrinoArgument *arg, ArgumentList *list);
void gsmParse(NeutrinoArgument *arg, ArgumentList *list);

// Generic handlers
//
// A simple one-value toggle
int toggleDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight);
ActionType toggleInput(NeutrinoArgument *arg, int input);
void toggleMarshal(NeutrinoArgument *arg, ArgumentList *list);
void toggleParse(NeutrinoArgument *arg, ArgumentList *list);

NeutrinoArgument uiArguments[] = {
    {.name = "Compatibility modes",
     .arg = "gc",
     .activeElementIdx = 0,
     .state = 0,
     .draw = gcDraw,
     .handleInput = gcInput,
     .parse = gcParse,
     .marshal = gcMarshal},
    {.name = "Video mode",
     .arg = "gsm",
     .activeElementIdx = 0,
     .state = 0,
     .draw = gsmDraw,
     .handleInput = gsmInput,
     .parse = gsmParse,
     .marshal = gsmMarshal},
    {.name = "Show PS2 logo",
     .arg = "logo",
     .activeElementIdx = 0,
     .state = 0,
     .draw = toggleDraw,
     .handleInput = toggleInput,
     .parse = toggleParse,
     .marshal = toggleMarshal},
    {.name = "Enable debug colors",
     .arg = "dbc",
     .activeElementIdx = 0,
     .state = 0,
     .draw = toggleDraw,
     .handleInput = toggleInput,
     .parse = toggleParse,
     .marshal = toggleMarshal},
};
int uiArgumentsTotal = sizeof(uiArguments) / sizeof(NeutrinoArgument);

// Argument value map
typedef struct ArgValueMap {
  int mode;
  const char *value;
  const char *name;
} ArgValueMap;

#define ARG_GC_NUM_MODES (sizeof(gcValueMap) / sizeof(ArgValueMap))
static const ArgValueMap gcValueMap[] = {
    {(1 << 0), "0", "IOP: Fast reads"},
    {(1 << 2), "2", "IOP: Sync reads"},
    {(1 << 3), "3", "EE : Unhook syscalls"},
    {(1 << 5), "5", "IOP: Emulate DVD-DL"},
    {(1 << 7), "7", "IOP: Fix game buffer overrun"},
};

//
// Compatibility arguments
//
int gcDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight) {
  // Draw title
  y = drawTextWindow(x, y, gsGlobal->Width - x, 0, 0, FontMainColor, ALIGN_HCENTER, arg->name);

  // Draw compatibility modes
  for (int idx = 0; idx < ARG_GC_NUM_MODES; idx++) {
    if (arg->state & gcValueMap[idx].mode) {
      drawIconWindow(x, y, 20, y + getFontLineHeight(), 0, FontMainColor, ALIGN_CENTER, ICON_ENABLED);
    }
    y = drawText(x + getIconWidth(ICON_ENABLED), y, 0, 0, 0, (((arg->activeElementIdx == idx) && isActive) ? ColorSelected : FontMainColor),
                 gcValueMap[idx].name);
  }

  return y;
}

ActionType gcInput(NeutrinoArgument *arg, int input) {
  if (input & (PAD_CROSS | PAD_CIRCLE)) {
    arg->state ^= gcValueMap[arg->activeElementIdx].mode;
    return ACTION_CHANGED;
  } else if (input & PAD_UP) {
    if (arg->activeElementIdx == 0)
      return ACTION_PREV_ARGUMENT;

    arg->activeElementIdx--;
  } else if (input & PAD_DOWN) {
    if (arg->activeElementIdx == ARG_GC_NUM_MODES - 1)
      return ACTION_NEXT_ARGUMENT;

    arg->activeElementIdx++;
  }
  return ACTION_NONE;
}

void gcMarshal(NeutrinoArgument *arg, ArgumentList *list) {
  Argument *larg = getArgument(list, arg->arg);
  if (!larg) {
    if (!arg->state)
      return;

    larg = insertArgument(list, arg->arg, "");
  }

  if (larg->isDisabled && !arg->state) // Ignore disabled value
    return;

  // Recreate value to enforce the string size
  char *prevValue = larg->value;
  larg->value = calloc(sizeof(char), ARG_GC_NUM_MODES + 1);

  int pos = 0;
  for (int i = 0; i < ARG_GC_NUM_MODES; i++) {
    if (arg->state & gcValueMap[i].mode) {
      larg->value[pos] = (char)gcValueMap[i].value[0];
      pos++;
    }
  }
  larg->value[pos] = '\0';

  // Remove global flag only if value has changed and has value
  if (prevValue) {
    if (strcmp(prevValue, larg->value) && pos)
      larg->isGlobal = 0;

    free(prevValue);
  }

  if (!pos)
    larg->isDisabled = 1;
  else
    larg->isDisabled = 0;
}

void gcParse(NeutrinoArgument *arg, ArgumentList *list) {
  arg->state = 0;
  arg->activeElementIdx = 0;
  Argument *larg = getArgument(list, arg->arg);
  if (!larg)
    return;

  if (larg->isDisabled) {
    // Force empty value
    if (larg->value)
      free(larg->value);

    larg->value = strdup("");
    return;
  }

  for (int i = 0; i < strlen(larg->value); i++) {
    for (int j = 0; j < ARG_GC_NUM_MODES; j++) {
      if (larg->value[i] == (char)gcValueMap[j].value[0]) {
        arg->state |= gcValueMap[j].mode;
        break;
      }
    }
  }
  if (!arg->state)
    larg->isDisabled = 1;
}

//
// GSM arguments
//
#define GSM_FULL_HEIGHT_MODES_HEADER "Full-height"
#define GSM_HALF_HEIGHT_MODES_HEADER "Half-height"
#define GSM_COMPAT_MODES_HEADER "Compatibility modes"
static const ArgValueMap gsmValueMap[] = {
    {(1 << 0), "fp", "Full-frame: force progressive (480p/576p)"},
    {(1 << 1), ":fp1", "Half-frame: force progressive (240p/288p)"},
    {(1 << 2), ":fp2", "Half-frame: force progressive with line doubling (480p/576p)"},
    {(1 << 3), ":1", "Field flipping type 1 (GSM/OPL)"},
    {(1 << 4), ":2", "Field flipping type 2"},
    {(1 << 5), ":3", "Field flipping type 3"},
};

int gsmDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight) {
  // Draw title
  y = drawTextWindow(x, y, gsGlobal->Width - x, 0, 0, FontMainColor, ALIGN_HCENTER, arg->name);
  for (int idx = 0; idx < sizeof(gsmValueMap) / sizeof(ArgValueMap); idx++) {
    if (arg->state & gsmValueMap[idx].mode) {
      drawIconWindow(x, y, 20, y + getFontLineHeight(), 0, FontMainColor, ALIGN_CENTER, ICON_ENABLED);
    }
    y = drawText(x + getIconWidth(ICON_ENABLED), y, 0, 0, 0, (((arg->activeElementIdx == idx) && isActive) ? ColorSelected : FontMainColor),
                 gsmValueMap[idx].name);
  }

  return y;
}

ActionType gsmInput(NeutrinoArgument *arg, int input) {
  if (input & (PAD_CROSS | PAD_CIRCLE)) {
    switch (arg->activeElementIdx) {
    case 0: // Full-frame: force progressive
      arg->state ^= gsmValueMap[0].mode;
      break;
    case 1:                               // Half-frame: force progressive
      arg->state &= ~gsmValueMap[2].mode; // Disable line doubling
      arg->state ^= gsmValueMap[1].mode;
      break;
    case 2:                               // Half-frame: force progressive with line doubling
      arg->state &= ~gsmValueMap[1].mode; // Disable force progressive
      arg->state ^= gsmValueMap[2].mode;
      break;
    case 3: // Field flipping type 1
      arg->state ^= gsmValueMap[3].mode;
      // Disable other field flipping modes
      arg->state &= ~gsmValueMap[4].mode;
      arg->state &= ~gsmValueMap[5].mode;
      // Force enable force progressive if none is set
      if (arg->state == gsmValueMap[3].mode)
        arg->state |= gsmValueMap[0].mode;
      break;
    case 4: // Field flipping type 2
      arg->state ^= gsmValueMap[4].mode;
      // Disable other field flipping modes
      arg->state &= ~gsmValueMap[3].mode;
      arg->state &= ~gsmValueMap[5].mode;
      // Force enable force progressive if none is set
      if (arg->state == gsmValueMap[4].mode)
        arg->state |= gsmValueMap[0].mode;
      break;
    case 5: // Field flipping type  3
      arg->state ^= gsmValueMap[5].mode;
      // Disable other field flipping modes
      arg->state &= ~gsmValueMap[3].mode;
      arg->state &= ~gsmValueMap[4].mode;
      // Force enable force progressive if none is set
      if (arg->state == gsmValueMap[5].mode)
        arg->state |= gsmValueMap[0].mode;
      break;
    }
    // Reset state if only field flipping is enabled
    if ((arg->state == gsmValueMap[3].mode) || (arg->state == gsmValueMap[4].mode) || (arg->state == gsmValueMap[5].mode))
      arg->state = 0;

    return ACTION_CHANGED;
  } else if (input & PAD_UP) {
    if (arg->activeElementIdx == 0)
      return ACTION_PREV_ARGUMENT;

    arg->activeElementIdx--;
  } else if (input & PAD_DOWN) {
    if (arg->activeElementIdx == sizeof(gsmValueMap) / sizeof(ArgValueMap) - 1)
      return ACTION_NEXT_ARGUMENT;

    arg->activeElementIdx++;
  }
  return ACTION_NONE;
}

void gsmMarshal(NeutrinoArgument *arg, ArgumentList *list) {
  Argument *larg = getArgument(list, arg->arg);
  if (!larg) {
    if (!arg->state)
      return;

    larg = insertArgument(list, arg->arg, "");
  }

  if (larg->isDisabled && !arg->state) // Ignore disabled value
    return;

  // Recreate value to enforce the string size
  char *prevValue = larg->value;
  larg->value = calloc(sizeof(char), 10);

  if (arg->state) {
    const char *ffArg = "";
    const char *hfArg = "";
    const char *cmArg = "";

    // Full-height
    if (arg->state & gsmValueMap[0].mode) {
      ffArg = gsmValueMap[0].value;
    }
    // Half-height
    if (arg->state & gsmValueMap[1].mode) {
      hfArg = gsmValueMap[1].value;
    } else if (arg->state & gsmValueMap[2].mode) {
      hfArg = gsmValueMap[2].value;
    }
    // Field flipping
    if (arg->state & gsmValueMap[3].mode) {
      cmArg = gsmValueMap[3].value;
    } else if (arg->state & gsmValueMap[4].mode) {
      cmArg = gsmValueMap[4].value;
    } else if (arg->state & gsmValueMap[5].mode) {
      cmArg = gsmValueMap[5].value;
    }

    // Insert placeholder if the compatibility argument
    // is not empty, but the half-frame argument is
    if (hfArg[0] == '\0' && cmArg[0] != '\0')
      hfArg = ":";

    snprintf(larg->value, 10, "%s%s%s", ffArg, hfArg, cmArg);
  }

  // Remove global flag only if value has changed and has value
  if (prevValue) {
    if (strcmp(prevValue, larg->value))
      larg->isGlobal = 0;

    free(prevValue);
  }

  if (!arg->state)
    larg->isDisabled = 1;
  else
    larg->isDisabled = 0;
}

void gsmParse(NeutrinoArgument *arg, ArgumentList *list) {
  arg->state = 0;
  arg->activeElementIdx = 0;
  Argument *larg = getArgument(list, arg->arg);
  if (!larg)
    return;

  if (larg->isDisabled) {
    // Force empty value
    if (larg->value)
      free(larg->value);

    larg->value = strdup("");
    return;
  }

  if (larg->value[0] == '\0')
    goto fail; // Empty argument

  char *argptr = larg->value;

  // Full-frame
  if (argptr[0] != ':') {
    if (!strncmp(argptr, "fp", 2))
      arg->state |= gsmValueMap[0].mode;
    else
      goto fail;

    argptr += 3;
  } else
    argptr++;

  // Half-frame
  if (argptr[0] == '\0')
    return;
  if (argptr[0] != ':') {
    if (!strncmp(argptr, "fp1", 3))
      arg->state |= gsmValueMap[1].mode;
    else if (!strncmp(argptr, "fp2", 3))
      arg->state |= gsmValueMap[2].mode;
    else
      goto fail;

    argptr += 4;
  } else
    argptr++;

  // Compatibility modes
  if (argptr[0] == '\0')
    return;
  if (argptr[0] != ':') {
    if (!strncmp(argptr, "1", 1))
      arg->state |= gsmValueMap[3].mode;
    else if (!strncmp(argptr, "2", 1))
      arg->state |= gsmValueMap[4].mode;
    else if (!strncmp(argptr, "3", 1))
      arg->state |= gsmValueMap[5].mode;
    else
      goto fail;

    argptr += 2;
  } else
    argptr++;

  return;
fail:
  larg->isDisabled = 1;
  return;
}

//
// Generic toggle
//
int toggleDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight) {
  // Draw argument
  if (arg->state)
    drawIconWindow(x, y, 20, y + getFontLineHeight(), 0, FontMainColor, ALIGN_CENTER, ICON_ENABLED);
  return drawText(x + getIconWidth(ICON_ENABLED), y, 0, 0, 0, ((isActive) ? ColorSelected : FontMainColor), arg->name);
}

ActionType toggleInput(NeutrinoArgument *arg, int input) {
  if (input & (PAD_CROSS | PAD_CIRCLE)) {
    arg->state ^= 1;
    return ACTION_CHANGED;
  } else if (input & PAD_UP) {
    return ACTION_PREV_ARGUMENT;
  } else if (input & PAD_DOWN) {
    return ACTION_NEXT_ARGUMENT;
  }
  return ACTION_NONE;
}

void toggleMarshal(NeutrinoArgument *arg, ArgumentList *list) {
  Argument *larg = getArgument(list, arg->arg);
  if (!larg) {
    if (!arg->state)
      return;

    larg = insertArgument(list, arg->arg, "");
  }

  larg->isGlobal = 0;
  if (arg->state) {
    larg->isDisabled = 0;
    return;
  }

  larg->isDisabled = 1;
}

void toggleParse(NeutrinoArgument *arg, ArgumentList *list) {
  arg->state = 0;
  Argument *larg = getArgument(list, arg->arg);
  if (!larg)
    return;

  if (!larg->isDisabled) {
    arg->state = 1;
    return;
  }
}
