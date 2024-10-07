#include "common.h"
#include "gui.h"
#include "iso.h"
#include "module_init.h"
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Relative path to neutrino ELF
const char STORAGE_BASE_PATH[] = "mass:";
char ELF_BASE_PATH[PATH_MAX + 1];

int main(int argc, char *argv[]) {
  // Initialize the screen (clear it)
  init_scr();

  printf("*************\n");
  logString("\n\nNHDDL — a Neutrino launcher by pcm720\n\n");
  printf("*************\n");

  // Get base path
  if (!getcwd(ELF_BASE_PATH, PATH_MAX + 1)) {
    logString("ERROR: Failed to get cwd\n");
    goto fail;
  }
  // Append '/' to current working directory
  strcat(ELF_BASE_PATH, "/");
  logString("Current working directory is %s\n", ELF_BASE_PATH);

  // Init HDD and MC modules
  logString("Loading modules...\n");
  int res;
  if ((res = init()) != 0) {
    logString("ERROR: Failed to initialize MC modules: %d\n", res);
    goto fail;
  }
  if ((res = initHDD(ELF_BASE_PATH)) != 0) {
    logString("Failed to initialize HDD modules: %d\n", res);
    goto fail;
  }
  logString("Modules loaded\n\n");

  logString("Searching for ISO on %s\n", STORAGE_BASE_PATH);
  struct TargetList *titles = findISO();
  if (titles == NULL) {
    logString("No targets found\n");
    goto fail;
  }
  
  init_scr();
  if ((res = uiLoop(titles))) {
    init_scr();
    logString("\n\nERROR: UI loop failed: %d\n", res);
    freeTargetList(titles);
    goto fail;
  };
  printf("UI loop done, exiting\n");
  freeTargetList(titles);
  return 0;

fail:
  sleep(3);
  return 1;
}
