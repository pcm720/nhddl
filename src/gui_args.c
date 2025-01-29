// Implements support for known Neutrino arguments
#include "gui_args.h"
#include "gui_graphics.h"
#include "options.h"
#include <libpad.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Compatibility modes handlers
int gcDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight);
ActionType gcInput(NeutrinoArgument *arg, int input);
void gcMarshal(NeutrinoArgument *arg, ArgumentList *list);
void gcUnmarshal(NeutrinoArgument *arg, ArgumentList *list);

// GSM handlers
int gsmDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight);
ActionType gsmInput(NeutrinoArgument *arg, int input);
void gsmMarshal(NeutrinoArgument *arg, ArgumentList *list);
void gsmUnmarshal(NeutrinoArgument *arg, ArgumentList *list);

// PS2 Logo handlers
int ps2LogoDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight);
ActionType ps2LogoInput(NeutrinoArgument *arg, int input);
void ps2LogoMarshal(NeutrinoArgument *arg, ArgumentList *list);
void ps2LogoUnmarshal(NeutrinoArgument *arg, ArgumentList *list);

NeutrinoArgument uiArguments[] = {
    {.name = "Compatibility modes",
     .arg = "gc",
     .activeElementIdx = 0,
     .state = 0,
     .draw = gcDraw,
     .handleInput = gcInput,
     .unmarshal = gcUnmarshal,
     .marshal = gcMarshal},
    {.name = "Video mode",
     .arg = "gsm",
     .activeElementIdx = 0,
     .state = 0,
     .draw = gsmDraw,
     .handleInput = gsmInput,
     .unmarshal = gsmUnmarshal,
     .marshal = gsmMarshal},
    {.name = "Show PS2 logo",
     .arg = "logo",
     .activeElementIdx = 0,
     .state = 0,
     .draw = ps2LogoDraw,
     .handleInput = ps2LogoInput,
     .unmarshal = ps2LogoUnmarshal,
     .marshal = ps2LogoMarshal},
};
int uiArgumentsTotal = sizeof(uiArguments) / sizeof(NeutrinoArgument);

// Argument value map
typedef struct ArgValueMap {
  int mode;
  char value;
  const char *name;
} ArgValueMap;

#define ARG_GC_NUM_MODES (sizeof(gcValueMap) / sizeof(ArgValueMap))
static const ArgValueMap gcValueMap[] = {
    {(1 << 0), '0', "IOP: Fast reads"},
    {(1 << 2), '2', "IOP: Sync reads"},
    {(1 << 3), '3', "EE : Unhook syscalls"},
    {(1 << 5), '5', "IOP: Emulate DVD-DL"},
    {(1 << 7), '7', "IOP: Fix game buffer overrun"},
};

#define ARG_GSM_NUM_MODES (sizeof(gsmValueMap) / sizeof(ArgValueMap))
static const ArgValueMap gsmValueMap[] = {
    {(1 << 0), '1', "Force progressive"},
    {(1 << 1), '2', "Force progressive with line doubling"},
    {(1 << 2), 'F', "Enable field flipping"},
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
      larg->value[pos] = gcValueMap[i].value;
      pos++;
    }
  }
  larg->value[pos] = '\0';

  // Remove global flag only if value has changed and has value
  if (strcmp(prevValue, larg->value) && pos)
    larg->isGlobal = 0;

  free(prevValue);

  if (!pos)
    larg->isDisabled = 1;
  else
    larg->isDisabled = 0;
}

void gcUnmarshal(NeutrinoArgument *arg, ArgumentList *list) {
  arg->state = 0;
  arg->activeElementIdx = 0;
  Argument *larg = getArgument(list, arg->arg);
  if (!larg)
    return;

  if (larg->isDisabled)
    return;

  for (int i = 0; i < strlen(larg->value); i++) {
    for (int j = 0; j < ARG_GC_NUM_MODES; j++) {
      if (larg->value[i] == gcValueMap[j].value) {
        arg->state |= gcValueMap[j].mode;
        break;
      }
    }
  }
}

//
// GSM arguments
//
int gsmDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight) {
  // Draw title
  y = drawTextWindow(x, y, gsGlobal->Width - x, 0, 0, FontMainColor, ALIGN_HCENTER, arg->name);

  // Draw compatibility modes
  for (int idx = 0; idx < ARG_GSM_NUM_MODES; idx++) {
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
    case 0: // Force progressive
      arg->state ^= gsmValueMap[0].mode;
      arg->state &= ~gsmValueMap[1].mode; // Disable line doubling
      break;
    case 1:                               // Force progressive + line doubling
      arg->state &= ~gsmValueMap[0].mode; // Disable force progressive
      arg->state ^= gsmValueMap[1].mode;
      break;
    case 2: // Field flipping
      arg->state ^= gsmValueMap[2].mode;
      // Force enable force progressive if none is set
      if (arg->state == gsmValueMap[2].mode)
        arg->state |= gsmValueMap[0].mode;
      break;
    }
    // Reset state if only field flipping is enabled
    if (arg->state == gsmValueMap[2].mode)
      arg->state = 0;
  } else if (input & PAD_UP) {
    if (arg->activeElementIdx == 0)
      return ACTION_PREV_ARGUMENT;

    arg->activeElementIdx--;
  } else if (input & PAD_DOWN) {
    if (arg->activeElementIdx == ARG_GSM_NUM_MODES - 1)
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
  larg->value = calloc(sizeof(char), ARG_GSM_NUM_MODES + 1);

  int pos = 0;
  for (int i = 0; i < ARG_GSM_NUM_MODES; i++) {
    if (arg->state & gsmValueMap[i].mode) {
      larg->value[pos] = gsmValueMap[i].value;
      pos++;
    }
  }
  larg->value[pos] = '\0';

  // Remove global flag only if value has changed and has value
  if (strcmp(prevValue, larg->value) && pos)
    larg->isGlobal = 0;

  free(prevValue);

  if (!pos)
    larg->isDisabled = 1;
  else
    larg->isDisabled = 0;
}

void gsmUnmarshal(NeutrinoArgument *arg, ArgumentList *list) {
  arg->state = 0;
  arg->activeElementIdx = 0;
  Argument *larg = getArgument(list, arg->arg);
  if (!larg)
    return;

  if (larg->isDisabled)
    return;

  for (int i = 0; i < strlen(larg->value); i++) {
    for (int j = 0; j < ARG_GSM_NUM_MODES; j++) {
      if (larg->value[i] == gsmValueMap[j].value) {
        arg->state |= gsmValueMap[j].mode;
        break;
      }
    }
  }
}

//
// PS2 Logo
//
int ps2LogoDraw(NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight) {
  // Draw argument
  if (arg->state)
    drawIconWindow(x, y, 20, y + getFontLineHeight(), 0, FontMainColor, ALIGN_CENTER, ICON_ENABLED);
  return drawText(x + getIconWidth(ICON_ENABLED), y, 0, 0, 0, ((isActive) ? ColorSelected : FontMainColor), arg->name);
}

ActionType ps2LogoInput(NeutrinoArgument *arg, int input) {
  if (input & (PAD_CROSS | PAD_CIRCLE)) {
    arg->state ^= 1;
  } else if (input & PAD_UP) {
    return ACTION_PREV_ARGUMENT;
  } else if (input & PAD_DOWN) {
    return ACTION_NEXT_ARGUMENT;
  }
  return ACTION_NONE;
}

void ps2LogoMarshal(NeutrinoArgument *arg, ArgumentList *list) {
  Argument *larg = getArgument(list, arg->arg);
  if (!larg) {
    if (!arg->state)
      return;

    larg = insertArgument(list, arg->arg, "");
  }

  if (arg->state) {
    larg->isDisabled = 0;
    return;
  }

  larg->isDisabled = 1;
  return;
}

void ps2LogoUnmarshal(NeutrinoArgument *arg, ArgumentList *list) {
  arg->state = 0;
  Argument *larg = getArgument(list, arg->arg);
  if (!larg)
    return;

  if (!larg->isDisabled) {
    arg->state = 1;
    return;
  }
}
