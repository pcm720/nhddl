#include <debug.h>
#include <kernel.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf-loader.h>

#include "common.h"
#include "history.h"
#include "iso.h"
#include "module_init.h"

// Relative path to neutrino ELF
const char *neutrinoELF = "neutrino.elf";

int main(int argc, char *argv[]) {
  // Initialize the screen (clear it)
  init_scr();

  printf("*************\n");
  logString("Neutrino HDD launcher prototype by pcm720\n\n");
  printf("*************\n");

  // Get base path
  char basePath[PATH_MAX + 1];
  if (!getcwd(basePath, PATH_MAX + 1)) {
    logString("ERROR: Failed to get cwd\n");
    goto fail;
  }
  // Append '/' to current working directory
  strcat(basePath, "/");
  logString("Current working directory is %s\n", basePath);

  // Init HDD and MC modules
  logString("Loading modules...\n");
  int res;
  if ((res = initMC()) != 0) {
    logString("Failed to initialize MC modules: %d\n", res);
    goto fail;
  }
  if ((res = initHDD(basePath)) != 0) {
    logString("Failed to initialize HDD modules: %d\n", res);
    goto fail;
  }
  logString("Modules loaded\n\n");

  //   updateHistoryFile("SLUS_200.02");
  //   while (1) {
  //   }

  // TODO: wait for HDD properly
  sleep(3);

  logString("Searching for ISO on mass:/\n");
  struct targetList *titles = findISO("mass:/");
  if (titles == NULL) {
    logString("No targets found\n");
    goto fail;
  }

  logString("Found %d target(s):\n", titles->total);

  target *title = titles->first;
  while (title) {
    logString("%s (%s) @ %s\n", title->name, title->id, title->fullPath);
    title = title->next;
  }

  // TODO: check if ID is null
  logString("Adding history record\n");
  updateHistoryFile(titles->first->id);

  // Allocate memory for full path to neutrino ELF and assemble full path
  char neutrinoPath[PATH_MAX + 1];
  strcpy(neutrinoPath, basePath);
  strcat(neutrinoPath, neutrinoELF);

  logString("Will load the first target in the list: %s (%s)\n", titles->first->name, titles->first->id);
  sleep(3);

  char *args[2];
  args[0] = "-bsd=ata";

  struct target *target = titles->first;

  args[1] = malloc(5 + strlen(target->fullPath));
  strcpy(args[1], "-dvd=");
  strcat(args[1], target->fullPath);

  LoadELFFromFile(neutrinoPath, 2, args);

fail:
  sleep(3);
  return 1;
}
