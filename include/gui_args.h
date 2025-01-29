#ifndef _GUI_ARGS_H_
#define _GUI_ARGS_H_

#include "options.h"

typedef enum {
  ACTION_NONE,          // Will do nothing
  ACTION_CHANGED,       // Input has changed
  ACTION_NEXT_ARGUMENT, // Switch to the next argument in chain
  ACTION_PREV_ARGUMENT, // Switch to the previous argument in chain
} ActionType;

struct NeutrinoArgument;

// Must draw within specified limits and
// return the bottom Y coordinate of the last line.
typedef int (*drawFunc)(struct NeutrinoArgument *arg, uint8_t isActive, int x, int y, int z, int maxWidth, int maxHeight);

// Must process given input and return one of ActionType
typedef ActionType (*handleInputFunc)(struct NeutrinoArgument *arg, int input);

// Must find argument in the list (or insert a new one) and store new value
typedef void (*marshalFunc)(struct NeutrinoArgument *arg, ArgumentList *list);

// Must find argument in the list, validate and parse value
typedef void (*parseFunc)(struct NeutrinoArgument *arg, ArgumentList *list);

typedef struct NeutrinoArgument {
  const char *name;
  const char *arg;
  drawFunc draw;
  handleInputFunc handleInput;
  parseFunc parse;
  marshalFunc marshal;
  uint8_t state;            // Internal argument state
  uint8_t activeElementIdx; // Active element index
} NeutrinoArgument;

// Defined in gui_args.c
extern NeutrinoArgument uiArguments[];
extern int uiArgumentsTotal;

#endif
