#include <debug.h>
#include <elf-loader.h>
#include <kernel.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "history.h"
#include "iso.h"
#include "module_init.h"
#include "options.h"

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
  if ((res = init()) != 0) {
    logString("Failed to initialize MC modules: %d\n", res);
    goto fail;
  }
  if ((res = initHDD(basePath)) != 0) {
    logString("Failed to initialize HDD modules: %d\n", res);
    goto fail;
  }
  logString("Modules loaded\n\n");

  logString("Searching for ISO on mass:/\n");
  struct TargetList *titles = findISO("mass:/");
  if (titles == NULL) {
    logString("No targets found\n");
    goto fail;
  }

  logString("Found %d target(s):\n", titles->total);
  
  init_scr();
  logString("Loading options from mass:\n");
  
  struct ArgumentList *globalOptions = malloc(sizeof(struct ArgumentList));
  getGlobalLaunchArguments(globalOptions, "mass:/");
  struct ArgumentList *titleOptions = malloc(sizeof(struct ArgumentList));
  getTitleLaunchArguments(titleOptions, titles->last);

  logString("Global args:\n");
  struct Argument *tArg = globalOptions->first;
  while (tArg != NULL) {
    logString("arg: %s, val: %s, disabled: %d\n", tArg->arg, tArg->value, tArg->isDisabled);
    tArg = tArg->next;
  }
  logString("Title args:\n");
  tArg = titleOptions->first;
  while (tArg != NULL) {
    logString("arg: %s, val: %s, disabled: %d\n", tArg->arg, tArg->value, tArg->isDisabled);
    tArg = tArg->next;
  }
  updateTitleLaunchArguments(titles->last, titleOptions);
  while (1) {
  }

  char *lastTitle = calloc(sizeof(char), PATH_MAX + 1);
  if (getLastLaunchedTitle("mass:", lastTitle)) {
    logString("Failed to read last played title\n");
  } else
    logString("Last played title is %s\n", lastTitle);

  struct Target *launchTarget = NULL;
  struct Target *current = titles->first;
  if (strlen(lastTitle) > 0) {
    while (launchTarget == NULL) {
      if (!strcmp(current->fullPath, lastTitle)) {
        launchTarget = current;
        break;
      } else if (current->next == NULL) {
        break;
      }
      current = current->next;
    }
  }
  if (launchTarget == NULL) {
    logString("Last played title not found, selecting the first title\n");
    launchTarget = titles->first;
  }

  if (updateLastLaunchedTitle(launchTarget->fullPath)) {
    logString("ERROR: Failed to update last played title\n");
  }
  updateHistoryFile(launchTarget->id);

  // Allocate memory for full path to neutrino ELF and assemble full path
  char neutrinoPath[PATH_MAX + 1];
  strcpy(neutrinoPath, basePath);
  strcat(neutrinoPath, neutrinoELF);

  logString("Will load %s (%s)\n", launchTarget->name, launchTarget->id);
  sleep(3);

  char *args[2];
  args[0] = "-bsd=ata";

  args[1] = malloc(5 + strlen(launchTarget->fullPath));
  strcpy(args[1], "-dvd=");
  strcat(args[1], launchTarget->fullPath);

  LoadELFFromFile(neutrinoPath, 2, args);

fail:
  sleep(3);
  return 1;
}
