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

void launchTitle(struct Target *target, struct ArgumentList *arguments) {
  // TODO:
  // Assemble arguments
  if (updateLastLaunchedTitle(target->fullPath)) {
    printf("ERROR: Failed to update last played title\n");
  }
  updateHistoryFile(target->id);

  // Allocate memory for full path to neutrino ELF and assemble full path
  char neutrinoPath[PATH_MAX + 1];
  strcpy(neutrinoPath, ELF_BASE_PATH);
  strcat(neutrinoPath, neutrinoELF);

  printf("Launching %s (%s)\n", target->name, target->id);

  char *args[2];
  args[0] = "-bsd=ata";

  args[1] = malloc(5 + strlen(target->fullPath));
  strcpy(args[1], "-dvd=");
  strcat(args[1], target->fullPath);

  printf("ERROR: failed to load %s: %d\n", neutrinoELF, LoadELFFromFile(neutrinoPath, 2, args));
}
