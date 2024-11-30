#include "common.h"
#include "devices.h"
#include "gui.h"
#include "iso.h"
#include "module_init.h"
#include "options.h"
#include <ctype.h>
#include <debug.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Path to ELF directory
char ELF_BASE_PATH[PATH_MAX + 1];
// Path to Neutrino ELF
char NEUTRINO_ELF_PATH[PATH_MAX + 1];
// Launcher options
LauncherOptions LAUNCHER_OPTIONS;
// Options file name relative to ELF_BASE_PATH
static const char optionsFile[] = "nhddl.yaml";
// Neutrino ELF name
static const char neutrinoELF[] = "neutrino.elf";
// Fallback neutrino.elf paths
static char neutrinoMCFallbackPath[] = "mcX:/APPS/neutrino/neutrino.elf";
static char neutrinoMassFallbackPath[] = "massX:/neutrino/neutrino.elf";

// Supported options
#define OPTION_480P "480p"
#define OPTION_MODE "mode"
#define OPTION_UDPBD_IP "udpbd_ip"

#ifndef GIT_VERSION
#define GIT_VERSION "v-0.0.0-unknown"
#endif

void initOptions(char *basePath);
// Attempts to find neutrino.elf at current path or one of fallback paths
int findNeutrinoELF();
// Tries to load IPCONFIG.DAT from memory card
void parseIPConfig();

int main(int argc, char *argv[]) {
  // Initialize the screen
  init_scr();

  printf("*************\n");
  logString("\n\nNHDDL %s\nA Neutrino launcher by pcm720\n\n", GIT_VERSION);
  printf("*************\n");

  if (!getcwd(ELF_BASE_PATH, PATH_MAX + 1)) {
    logString("ERROR: Failed to get cwd\n");
    goto fail;
  }

  // Append '/' to current working directory
  if (ELF_BASE_PATH[strlen(ELF_BASE_PATH) - 1] != '/')
    strcat(ELF_BASE_PATH, "/");

  // Load options file from currently initalized filesystem before rebooting IOP
  initOptions(ELF_BASE_PATH);

  logString("Current working directory is %s\n", ELF_BASE_PATH);
  logString("Loading modules...\n");
  // Init modules
  int res;
  if ((res = initModules(ELF_BASE_PATH)) != 0) {
    logString("ERROR: Failed to initialize modules: %d\n", res);
    goto fail;
  }

  init_scr();
  logString("\n\nInitializing BDM devices...\n");
  res = initDeviceMap();
  if ((res < 0)) {
    logString("ERROR: failed to initialize device\n");
    goto fail;
  }
  if (!res) {
    logString("ERROR: No BDM devices found\n");
    goto fail;
  }

  // Make sure neutrino ELF exists
  if (findNeutrinoELF()) {
    goto fail;
  }
  logString("\nFound neutrino.elf at %s", NEUTRINO_ELF_PATH);

  logString("\n\nBuilding target list...\n");
  TargetList *titles = findISO();
  if (titles == NULL) {
    logString("No targets found\n");
    goto fail;
  }

  if ((res = uiInit())) {
    printf("ERROR: Failed to init UI: %d\n", res);
    goto fail;
  }

  if ((res = uiLoop(titles))) {
    init_scr();
    logString("\n\nERROR: UI loop failed: %d\n", res);
    goto fail;
  }
  printf("UI loop done, exiting\n");
  freeTargetList(titles);
  return 0;

fail:
  sleep(10);
  return 1;
}

// Parses mode string into enum
ModeType parseMode(const char *modeStr) {
  if (!strcmp(modeStr, "ata"))
    return MODE_ATA;
  if (!strcmp(modeStr, "mx4sio"))
    return MODE_MX4SIO;
  if (!strcmp(modeStr, "udpbd"))
    return MODE_UDPBD;
  if (!strcmp(modeStr, "usb"))
    return MODE_USB;
  if (!strcmp(modeStr, "ilink"))
    return MODE_ILINK;
  return MODE_ALL;
}

// Loads NHDDL options from optionsFile on memory card
void initOptions(char *basePath) {
  LAUNCHER_OPTIONS.is480pEnabled = 0;
  LAUNCHER_OPTIONS.mode = MODE_ALL;
  LAUNCHER_OPTIONS.udpbdIp[0] = '\0';

  char lineBuffer[PATH_MAX + sizeof(optionsFile) + 1];
  strcpy(lineBuffer, basePath);
  strcat(lineBuffer, optionsFile);

  // Load NHDDL options file into ArgumentList
  ArgumentList *options = calloc(1, sizeof(ArgumentList));
  if (loadArgumentList(options, lineBuffer)) {
    logString("Can't load options file, will use defaults\n");
    parseIPConfig(&LAUNCHER_OPTIONS); // Get IP from IPCONFIG.DAT
    freeArgumentList(options);
    return;
  }

  // Parse the list into Options
  Argument *arg = options->first;
  while (arg != NULL) {
    if (!arg->isDisabled) {
      if (strcmp(OPTION_480P, arg->arg) == 0) {
        LAUNCHER_OPTIONS.is480pEnabled = 1;
      } else if (strcmp(OPTION_MODE, arg->arg) == 0) {
        LAUNCHER_OPTIONS.mode = parseMode(arg->value);
      } else if (strcmp(OPTION_UDPBD_IP, arg->arg) == 0) {
        strlcpy(LAUNCHER_OPTIONS.udpbdIp, arg->value, sizeof(LAUNCHER_OPTIONS.udpbdIp));
      }
    }
    arg = arg->next;
  }
  freeArgumentList(options);
}

// Tests if file exists by opening it
int tryFile(char *filepath) {
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    return fd;
  }
  close(fd);
  return 0;
}

// Attempts to find neutrino.elf at current path or one of fallback paths
int findNeutrinoELF() {
  // Test if neturino.elf exists
  strcpy(NEUTRINO_ELF_PATH, ELF_BASE_PATH);
  strcat(NEUTRINO_ELF_PATH, neutrinoELF);

  if (tryFile(NEUTRINO_ELF_PATH)) {
    // If neutrino.elf doesn't exist in CWD, try fallback paths
    NEUTRINO_ELF_PATH[0] = '\0';
    for (int i = 0; i < MAX_MASS_DEVICES; i++) {
      if ((i > 1) && (deviceModeMap[i].mode == MODE_ALL)) {
        break;
      }

      neutrinoMCFallbackPath[2] = i + '0';
      neutrinoMassFallbackPath[4] = i + '0';
      if ((i < '2') && !tryFile(neutrinoMCFallbackPath)) {
        strcpy(NEUTRINO_ELF_PATH, neutrinoMCFallbackPath);
        goto out;
      }
      if (!tryFile(neutrinoMassFallbackPath)) {
        strcpy(NEUTRINO_ELF_PATH, neutrinoMassFallbackPath);
        goto out;
      }
    }
  }

  if (NEUTRINO_ELF_PATH[0] == '\0') {
    logString("ERROR: couldn't find neutrino.elf\n");
    return -ENOENT;
  }
out:
  return 0;
}