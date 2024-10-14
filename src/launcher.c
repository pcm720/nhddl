#include "common.h"
#include "history.h"
#include "iso.h"
#include "options.h"
#include <elf-loader.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char isoArgument[] = "dvd";
static char bsdArgument[] = "bsd";

// Neutrino bsd values
#define BSD_ATA "ata"
#define BSD_MX4SIO "mx4sio"
#define BSD_UDPBD "udpbd"
#define BSD_USB "usb"

// Assembles argument lists into argv for Neutrino.
// Expects argv to be initialized with at least (arguments->total) elements.
int assembleArgv(ArgumentList *arguments, char **argv) {
  Argument *curArg = arguments->first;
  int argCount = 0;
  int argSize = 0;
  while (curArg != NULL) {
    if (!curArg->isDisabled) {
      if (!strlen(curArg->value) && !strcmp(COMPAT_MODES_ARG, curArg->arg)) {
        // Skip empty compatibility mode argument
        curArg = curArg->next;
        continue;
      }

      argSize = strlen(curArg->arg) + strlen(curArg->value) + 3; // + \0, = and -
      char *value = calloc(sizeof(char), argSize);

      if (!strlen(curArg->value))
        snprintf(value, argSize, "-%s", curArg->arg);
      else
        snprintf(value, argSize, "-%s=%s", curArg->arg, curArg->value);

      argv[argCount] = value;
      argCount++;
    }
    curArg = curArg->next;
  }

  // Free unused memory
  if (argCount != arguments->total)
    argv = realloc(argv, argCount * sizeof(char *));

  return argCount;
}

// Launches target, passing arguments to Neutrino.
// Expects arguments to be initialized
void launchTitle(Target *target, ArgumentList *arguments) {
  // Append arguments
  char *bsdValue;
  switch (LAUNCHER_OPTIONS.mode) {
  case MODE_ATA:
    bsdValue = BSD_ATA;
    break;
  case MODE_MX4SIO:
    bsdValue = BSD_MX4SIO;
    break;
  case MODE_UDPBD:
    bsdValue = BSD_UDPBD;
    break;
  case MODE_USB:
    bsdValue = BSD_USB;
    break;
  default:
    printf("ERROR: Unsupported mode\n");
    return;
  }
  appendArgument(arguments, newArgument(bsdArgument, bsdValue));
  appendArgument(arguments, newArgument(isoArgument, target->fullPath));

  // Assemble argv
  char **argv = malloc((arguments->total) * sizeof(char *));
  int argCount = assembleArgv(arguments, argv);

  printf("Launching %s (%s) with arguments:\n", target->name, target->id);
  for (int i = 0; i < argCount; i++) {
    printf("%d: %s\n", i + 1, argv[i]);
  }

  if (updateLastLaunchedTitle(target->fullPath)) {
    printf("ERROR: Failed to update last launched title\n");
  }
  updateHistoryFile(target->id);

  printf("ERROR: failed to load %s: %d\n", NEUTRINO_ELF_PATH, LoadELFFromFile(NEUTRINO_ELF_PATH, argCount, argv));
}
