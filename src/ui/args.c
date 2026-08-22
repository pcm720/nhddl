// Implements support for known Neutrino arguments
#include "ui/args.h"
#include "options.h"
#include "ui/graphics.h"
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
    {.name = "Modos de compatibilidad",
     .arg = "gc",
     .activeElementIdx = 0,
     .state = 0,
     .draw = gcDraw,
     .handleInput = gcInput,
     .parse = gcParse,
     .marshal = gcMarshal},
    {.name = "Modo de video",
     .arg = "gsm",
     .activeElementIdx = 0,
     .state = 0,
     .draw = gsmDraw,
     .handleInput = gsmInput,
     .parse = gsmParse,
     .marshal = gsmMarshal},
    {.name = "Mostrar logo PS2",
     .arg = "logo",
     .activeElementIdx = 0,
     .state = 0,
     .draw = toggleDraw,
     .handleInput = toggleInput,
     .parse = toggleParse,
     .marshal = toggleMarshal},
    {.name = "Habilitar colores de depuración",
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
    {(1 << 0), "0", "IOP: Lecturas rápidas"},
    {(1 << 2), "2", "IOP: Lecturas sincronizadas"},
    {(1 << 3), "3", "EE : Desenganche de syscalls"},
    {(1 << 5), "5", "IOP: Emular DVD-DL"},
    {(1 << 7), "7", "IOP: Reparar desbordamiento de buffer"},
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
static const ArgValueMap gsmValueMap[] = {
    {(1 << 0), "fp1", "Forzar progresivo (240p/288p)"}, {(1 << 1), "fp2", "Forzar progresivo (480p/576p)"},
    {(1 << 2), "1080ix1", "Forzar 1080i con escala x1"}, {(1 << 3), "1080ix2", "Forzar 1080i con escala x2"},
    {(1 << 4), "1080ix3", "Forzar 1080i con escala x3"}, {(1 << 5), ":1", "Inversión de campo tipo 1 (GSM/OPL)"},
    {(1 << 6), ":2", "Inversión de campo tipo 2"},          {(1 << 7), ":3", "Inversión de campo tipo 3"},
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
    case 0: // 240/288p
      arg->state &= ~gsmValueMap[4].mode;
      arg->state &= ~gsmValueMap[3].mode;
      arg->state &= ~gsmValueMap[2].mode;
      arg->state &= ~gsmValueMap[1].mode;
      arg->state ^= gsmValueMap[0].mode;
      break;
    case 1: // 480p/576p
      arg->state &= ~gsmValueMap[4].mode;
      arg->state &= ~gsmValueMap[3].mode;
      arg->state &= ~gsmValueMap[2].mode;
      arg->state &= ~gsmValueMap[0].mode;
      arg->state ^= gsmValueMap[1].mode;
      break;
    case 2: // 1080i x1
      arg->state &= ~gsmValueMap[4].mode;
      arg->state &= ~gsmValueMap[3].mode;
      arg->state &= ~gsmValueMap[1].mode;
      arg->state &= ~gsmValueMap[0].mode;
      arg->state ^= gsmValueMap[2].mode;
      break;
    case 3: // 1080i x2
      arg->state &= ~gsmValueMap[4].mode;
      arg->state &= ~gsmValueMap[2].mode;
      arg->state &= ~gsmValueMap[1].mode;
      arg->state &= ~gsmValueMap[0].mode;
      arg->state ^= gsmValueMap[3].mode;
      break;
    case 4: // 1080i x3
      arg->state &= ~gsmValueMap[3].mode;
      arg->state &= ~gsmValueMap[2].mode;
      arg->state &= ~gsmValueMap[1].mode;
      arg->state &= ~gsmValueMap[0].mode;
      arg->state ^= gsmValueMap[4].mode;
      break;
    case 5: // Field flipping type 1
      arg->state ^= gsmValueMap[5].mode;
      // Disable other field flipping modes
      arg->state &= ~gsmValueMap[6].mode;
      arg->state &= ~gsmValueMap[7].mode;
      // Force enable force progressive if none is set
      if (arg->state == gsmValueMap[5].mode)
        arg->state |= gsmValueMap[1].mode;
      break;
    case 6: // Field flipping type 2
      arg->state ^= gsmValueMap[6].mode;
      // Disable other field flipping modes
      arg->state &= ~gsmValueMap[5].mode;
      arg->state &= ~gsmValueMap[7].mode;
      // Force enable force progressive if none is set
      if (arg->state == gsmValueMap[6].mode)
        arg->state |= gsmValueMap[1].mode;
      break;
    case 7: // Field flipping type  3
      arg->state ^= gsmValueMap[7].mode;
      // Disable other field flipping modes
      arg->state &= ~gsmValueMap[5].mode;
      arg->state &= ~gsmValueMap[6].mode;
      // Force enable force progressive if none is set
      if (arg->state == gsmValueMap[7].mode)
        arg->state |= gsmValueMap[1].mode;
      break;
    }
    // Reset state if only field flipping is enabled
    if ((arg->state == gsmValueMap[5].mode) || (arg->state == gsmValueMap[6].mode) || (arg->state == gsmValueMap[7].mode))
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
    const char *vmArg = "";
    const char *cmArg = "";

    // Video mode
    if (arg->state & gsmValueMap[0].mode) {
      vmArg = gsmValueMap[0].value;
    } else if (arg->state & gsmValueMap[1].mode) {
      vmArg = gsmValueMap[1].value;
    } else if (arg->state & gsmValueMap[2].mode) {
      vmArg = gsmValueMap[2].value;
    } else if (arg->state & gsmValueMap[3].mode) {
      vmArg = gsmValueMap[3].value;
    } else if (arg->state & gsmValueMap[4].mode) {
      vmArg = gsmValueMap[4].value;
    }
    // Field flipping
    if (arg->state & gsmValueMap[5].mode) {
      cmArg = gsmValueMap[5].value;
    } else if (arg->state & gsmValueMap[6].mode) {
      cmArg = gsmValueMap[6].value;
    } else if (arg->state & gsmValueMap[7].mode) {
      cmArg = gsmValueMap[7].value;
    }
    snprintf(larg->value, 10, "%s%s", vmArg, cmArg);
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

  // Video modes
  if (!strncmp(argptr, "fp", 2)) {
    switch (argptr[2]) {
    case '1':
      arg->state |= gsmValueMap[0].mode;
      break;
    case '2':
      arg->state |= gsmValueMap[1].mode;
      break;
    default:
      goto fail;
    }
    argptr += 3;
  } else if (!strncmp(argptr, "1080ix", 6)) {
    switch (argptr[6]) {
    case '1':
      arg->state |= gsmValueMap[2].mode;
      break;
    case '2':
      arg->state |= gsmValueMap[3].mode;
      break;
    case '3':
      arg->state |= gsmValueMap[4].mode;
      break;
    default:
      goto fail;
    }
    argptr += 7;
  } else
    goto fail;

  // Compatibility modes
  if (argptr[0] == ':') {
    argptr++;
    switch (argptr[0]) {
    case '1': // Mode 1
      arg->state |= gsmValueMap[5].mode;
      break;
    case '2': // Mode 2
      arg->state |= gsmValueMap[6].mode;
      break;
    case '3': // Mode 3
      arg->state |= gsmValueMap[7].mode;
      break;
    }
  }

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
