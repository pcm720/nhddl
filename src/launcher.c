#include "common.h"
#include "history.h"
#include "iso.h"
#include "options.h"
#include <elf-loader.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char neutrinoELF[] = "neutrino.elf";
static char bsdArgument[] = "bsd";
static char bsdValue[] = "ata";
static char isoArgument[] = "dvd";

// Assembles argument lists into argv for Neutrino.
// Expects argv to be initialized with at least (arguments->total) elements.
int assembleArgv(struct ArgumentList *arguments, char **argv) {
  struct Argument *curArg = arguments->first;
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
      char *value = calloc(argSize, sizeof(char));

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
void launchTitle(struct Target *target, struct ArgumentList *arguments) {
  // Append arguments
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
    printf("ERROR: Failed to update last played title\n");
  }
  updateHistoryFile(target->id);

  char neutrinoPath[PATH_MAX + 1];
  strcpy(neutrinoPath, ELF_BASE_PATH);
  strcat(neutrinoPath, neutrinoELF);
  printf("ERROR: failed to load %s: %d\n", neutrinoELF, LoadELFFromFile(neutrinoPath, argCount, argv));
}
