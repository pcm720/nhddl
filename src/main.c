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

// Launcher options
LauncherOptions LAUNCHER_OPTIONS;
// Path to Neutrino ELF
char NEUTRINO_ELF_PATH[PATH_MAX + 1];
// Options file name relative to CWD
static const char optionsFile[] = "nhddl.yaml";
// Options file paths for SAS
static char nhddlSASPath[] = "mcX:/NHDDL/nhddl.yaml";
static char nhddlSASConfigPath[] = "mcX:/NHDDL-CONF/nhddl.yaml";
static char nhddlMassFallbackPath[] = "massX:/nhddl/nhddl.yaml";
// Neutrino ELF name relative to CWD
static const char neutrinoELF[] = "neutrino.elf";
// Fallback neutrino.elf paths
static char neutrinoMCFallbackPath[] = "mcX:/NEUTRINO/neutrino.elf";
static char neutrinoMassFallbackPath[] = "massX:/neutrino/neutrino.elf";

// Supported options
#define OPTION_480P "480p"
#define OPTION_MODE "mode"
#define OPTION_UDPBD_IP "udpbd_ip"

#ifndef GIT_VERSION
#define GIT_VERSION "v-0.0.0-unknown"
#endif

// Initializes modules, NHDDL configuraton, Neutrino path and device map
int init();
// Loads NHDDL options from option file
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
  printf("\n*************\n");

  int res;
  if ((res = init()))
    goto fail;

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

// Initialized BDM device map while logging errors
int initBDM() {
  init_scr();
  logString("\n\nInitializing BDM devices...\n");
  int res = initDeviceMap();
  if ((res < 0)) {
    logString("ERROR: failed to initialize devices\n");
    return -EIO;
  }
  if (!res) {
    logString("ERROR: No BDM devices found\n");
    return -ENODEV;
  }
  return 0;
}

// Split init into two versions
// since they differ enough to make the code too complicated otherwise
#ifdef STANDALONE
// Standalone init
int init() {
  int fd, res;
  char cwdPath[PATH_MAX + 1];
  // Get CWD and try to open it
  if (getcwd(cwdPath, PATH_MAX + 1) && ((fd = open(cwdPath, O_RDONLY | O_DIRECTORY)) >= 0)) {
    close(fd);
    strcat(cwdPath, "/");
    // Try to load options from CWD
    initOptions(cwdPath);
  } else {
    cwdPath[0] = '\0';                // CWD is not valid
    LAUNCHER_OPTIONS.mode = MODE_ALL; // Force mode to ALL to load all modules
  }

  logString("Loading embedded modules...\n");
  // Init modules
  if ((res = initModules()) != 0) {
    logString("ERROR: Failed to initialize modules: %d\n", res);
    return res;
  }
  // Init device map
  if (initBDM() < 0) {
    logString("ERROR: failed to initialize devices\n");
    return -EIO;
  }

  // Reload options
  initOptions(cwdPath);

  // Make sure neutrino ELF exists
  if (findNeutrinoELF(cwdPath)) {
    logString("ERROR: couldn't find neutrino.elf\n");
    return -ENOENT;
  }
  logString("\nFound neutrino.elf at %s\n", NEUTRINO_ELF_PATH);

  return 0;
}
#else
// Non-standalone init
int init() {
  int fd, res;
  char cwdPath[PATH_MAX + 1];
  // Get CWD and try to open it
  if (getcwd(cwdPath, PATH_MAX + 1) && ((fd = open(cwdPath, O_RDONLY | O_DIRECTORY)) >= 0)) {
    // Skip loading embedded modules if CWD is available
    close(fd);
    strcat(cwdPath, "/");
    logString("Current working directory is %s\n", cwdPath);
  } else {
    cwdPath[0] = '\0';                // CWD is not valid
    LAUNCHER_OPTIONS.mode = MODE_ALL; // Force mode to ALL
    // Load embedded modules first to make sure memory card is available
    logString("Loading embedded modules...\n");
    // Init modules
    if ((res = initModules()) != 0) {
      logString("ERROR: Failed to initialize modules: %d\n", res);
      return res;
    }
  }
  // Try to load options file from currently initalized filesystems
  initOptions(cwdPath);

  // Make sure neutrino ELF exists
  if (findNeutrinoELF(cwdPath)) {
    logString("ERROR: couldn't find neutrino.elf\n");
    return -ENOENT;
  }
  logString("\nFound neutrino.elf at %s\n", NEUTRINO_ELF_PATH);

  // Get Neutrino directory by trimming ELF file name from the path
  char *neutrinoELFDir = calloc(sizeof(char), strlen(NEUTRINO_ELF_PATH) - sizeof(neutrinoELF) + 3);
  strlcpy(neutrinoELFDir, NEUTRINO_ELF_PATH, strlen(NEUTRINO_ELF_PATH) - sizeof(neutrinoELF) + 2);
  logString("Loading external modules...\n");
  // Load external modules from Neutrino path into EE memory
  res = loadExternalModules(neutrinoELFDir);
  free(neutrinoELFDir);
  if (res) {
    logString("ERROR: Failed to prepare external modules\n");
    return -EIO;
  }

  // Init modules
  if ((res = initModules()) != 0) {
    logString("ERROR: Failed to initialize modules: %d\n", res);
    return res;
  }
  // Init device map
  if (initBDM() < 0) {
    logString("ERROR: failed to initialize devices\n");
    return -EIO;
  }

  // Reload options
  initOptions(cwdPath);

  return 0;
}
#endif

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

// Tests if file exists by opening it
int tryFile(char *filepath) {
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    return fd;
  }
  close(fd);
  return 0;
}

// Loads NHDDL options from optionsFile on memory card
void initOptions(char *cwdPath) {
  LAUNCHER_OPTIONS.is480pEnabled = 0;
  LAUNCHER_OPTIONS.mode = MODE_ALL;
  LAUNCHER_OPTIONS.udpbdIp[0] = '\0';

  char lineBuffer[PATH_MAX + sizeof(optionsFile) + 1];
  if (cwdPath[0] != '\0') {
    // If path is valid, try it
    strcpy(lineBuffer, cwdPath);
    strcat(lineBuffer, optionsFile);
    if (!tryFile(lineBuffer))
      // Skip fallbacks
      goto fileExists;
  }

  // If config file doesn't exist in CWD, try fallback paths
  lineBuffer[0] = '\0';
  for (int i = 0; i < MAX_MASS_DEVICES; i++) {
    if ((i > 1) && (deviceModeMap[i].mode == MODE_NONE)) {
      break;
    }

    nhddlMassFallbackPath[4] = i + '0';
    if (i < '2') {
      nhddlSASPath[2] = i + '0';
      if (!tryFile(nhddlSASPath)) {
        strcpy(lineBuffer, nhddlSASPath);
        goto fileExists;
      }
      nhddlSASConfigPath[2] = i + '0';
      if (!tryFile(nhddlSASConfigPath)) {
        strcpy(lineBuffer, nhddlSASConfigPath);
        goto fileExists;
      }
    }
    if (!tryFile(nhddlMassFallbackPath)) {
      strcpy(lineBuffer, nhddlMassFallbackPath);
      goto fileExists;
    }
  }

  if (lineBuffer[0] == '\0') {
    logString("Can't find options file, will use defaults\n");
    return;
  }

fileExists:
  // Load NHDDL options file into ArgumentList
  ArgumentList *options = calloc(1, sizeof(ArgumentList));
  if (loadArgumentList(options, lineBuffer)) {
    // Else, fail
    logString("Can't load options file, will use defaults\n");
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

// Attempts to find neutrino.elf at current path or one of fallback paths
int findNeutrinoELF(char *cwdPath) {
  if (cwdPath[0] != '\0') {
    // If path is valid, try it
    strcpy(NEUTRINO_ELF_PATH, cwdPath);
    strcat(NEUTRINO_ELF_PATH, neutrinoELF);
    if (!tryFile(NEUTRINO_ELF_PATH))
      return 0;
  }

  // If neutrino.elf doesn't exist in CWD, try fallback paths
  NEUTRINO_ELF_PATH[0] = '\0';
  for (int i = 0; i < MAX_MASS_DEVICES; i++) {
    if ((i > 1) && (deviceModeMap[i].mode == MODE_NONE)) {
      break;
    }

    neutrinoMCFallbackPath[2] = i + '0';
    neutrinoMassFallbackPath[4] = i + '0';
    if ((i < '2') && !tryFile(neutrinoMCFallbackPath)) {
      strcpy(NEUTRINO_ELF_PATH, neutrinoMCFallbackPath);
      return 0;
    }
    if (!tryFile(neutrinoMassFallbackPath)) {
      strcpy(NEUTRINO_ELF_PATH, neutrinoMassFallbackPath);
      return 0;
    }
  }

  if (NEUTRINO_ELF_PATH[0] == '\0') {
    return -ENOENT;
  }
  return 0;
}