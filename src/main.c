#include "common.h"
#include "gui.h"
#include "iso.h"
#include "module_init.h"
#include <debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Path to ISO storage
const char STORAGE_BASE_PATH[] = "mass:";
// Path to ELF directory
char ELF_BASE_PATH[PATH_MAX + 1];
// Progressive mode file name
static const char progressiveFile[] = "480p";

int main(int argc, char *argv[]) {
  // Initialize the screen
  init_scr();

  printf("*************\n");
  logString("\n\nNHDDL - a Neutrino launcher by pcm720\n\n");
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
  if ((res = initBDM(ELF_BASE_PATH)) != 0) {
    logString("Failed to initialize BDM modules: %d\n", res);
    goto fail;
  }
  logString("Modules loaded\n\n");

  logString("Searching for ISO on %s\n", STORAGE_BASE_PATH);
  struct TargetList *titles = findISO();
  if (titles == NULL) {
    logString("No targets found\n");
    goto fail;
  }

  // If ELF file name ends with _p.elf or
  // progressiveFile exists in current working directory, enable 480p mode
  int enable480p = 0;
  char *suffix = strrchr(argv[0], '_');
  char *progFile = calloc(sizeof(char), PATH_MAX + 1);
  snprintf(progFile, PATH_MAX + 1, "%s/%s", ELF_BASE_PATH, progressiveFile);
  if (((suffix != NULL) && !strcmp(suffix, "_p.elf")) || (!access(progFile, R_OK))) {
    printf("Starting UI in progressive mode\n");
    enable480p = 1;
  }
  free(progFile);

  if ((res = uiInit(enable480p))) {
    printf("ERROR: Failed to init UI: %d\n", res);
    goto fail;
  }

  if ((res = uiLoop(titles))) {
    init_scr();
    logString("\n\nERROR: UI loop failed: %d\n", res);
    freeTargetList(titles);
    goto fail;
  }
  printf("UI loop done, exiting\n");
  freeTargetList(titles);
  return 0;

fail:
  sleep(3);
  return 1;
}
