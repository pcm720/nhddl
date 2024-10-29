#include "common.h"
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

// Path to ISO storage
const char STORAGE_BASE_PATH[] = "mass:";
const size_t STORAGE_BASE_PATH_LEN = sizeof(STORAGE_BASE_PATH) / sizeof(char);
// Path to ELF directory
char ELF_BASE_PATH[PATH_MAX + 1];
// Path to Neutrino ELF
char NEUTRINO_ELF_PATH[PATH_MAX + 1];
// Launcher options
LauncherOptions LAUNCHER_OPTIONS;
// Options file name relative to ELF_BASE_PATH
static const char optionsFile[] = "nhddl.yaml";
// The 'X' in "mcX" will be replaced with memory card number in parseIPConfig
static char ipconfigPath[] = "mcX:/SYS-CONF/IPCONFIG.DAT";
// Neutrino ELF name
static const char neutrinoELF[] = "neutrino.elf";

// Supported options
#define OPTION_480P "480p"
#define OPTION_MODE "mode"
#define OPTION_UDPBD_IP "udpbd_ip"

#ifndef GIT_VERSION
#define GIT_VERSION "v-0.0.0-unknown"
#endif
// Used as ELF_BASE_PATH if DEBUG is defined
#define DEBUG_PATH "mc1:/APPS/neutrino"

void initOptions(char *basePath);

int main(int argc, char *argv[]) {
  // Initialize the screen
  init_scr();

  printf("*************\n");
  logString("\n\nNHDDL %s\nA Neutrino launcher by pcm720\n\n", GIT_VERSION);
  printf("*************\n");

// If DEBUG is not defined
#ifndef DEBUG
  // Get base path from current working directory
  if (!getcwd(ELF_BASE_PATH, PATH_MAX + 1)) {
    logString("ERROR: Failed to get cwd\n");
    goto fail;
  }
#else
  // Get base path from hardcoded DEBUG_PATH
  strcpy(ELF_BASE_PATH, DEBUG_PATH);
#endif

  if (strncmp("mc", ELF_BASE_PATH, 2) && strncmp("host", ELF_BASE_PATH, 4)) {
    logString("ERROR: NHDDL can only be run from the memory card");
    goto fail;
  }

  // Append '/' to current working directory
  strcat(ELF_BASE_PATH, "/");
  logString("Current working directory is %s\n", ELF_BASE_PATH);

  logString("Loading basic modules...\n");
  // Init MC and pad modules
  int res;
  if ((res = init()) != 0) {
    logString("ERROR: Failed to initialize modules: %d\n", res);
    goto fail;
  }

  strcpy(NEUTRINO_ELF_PATH, ELF_BASE_PATH);
  strcat(NEUTRINO_ELF_PATH, neutrinoELF);
  int neutrinoFd = open(NEUTRINO_ELF_PATH, O_RDONLY);
  if (neutrinoFd < 0) {
    logString("ERROR: %s doesn't exist\n", NEUTRINO_ELF_PATH);
    goto fail;
  }
  close(neutrinoFd);

  initOptions(ELF_BASE_PATH);

  // Init BDM modules
  logString("Loading BDM modules...\n");
  if ((res = initBDM(ELF_BASE_PATH)) != 0) {
    logString("ERROR: Failed to initialize modules: %d\n", res);
    goto fail;
  }

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
  return MODE_ATA;
}

// Tries to read SYS-CONF/IPCONFIG.DAT from basePath
void parseIPConfig(LauncherOptions *opts) {
  int ipconfigFd, count;
  char ipAddr[16]; // IP address will not be longer than 15 characters
  for (char i = '0'; i < '2'; i++) {
    ipconfigPath[2] = i;
    // Attempt to open history file
    ipconfigFd = open(ipconfigPath, O_RDONLY);
    if (ipconfigFd >= 0) {
      count = read(ipconfigFd, ipAddr, sizeof(ipAddr) - 1);
      close(ipconfigFd);
      break;
    }
  }

  if ((ipconfigFd < 0) || (count < sizeof(ipAddr) - 1)) {
    logString("Failed to get IP address from IPCONFIG.DAT\n");
    return;
  }

  count = 0; // Reuse count as line index
  // In case IP address is shorter than 15 chars
  while (!isspace((unsigned char)ipAddr[count])) {
    // Advance index until we read a whitespace character
    count++;
  }

  strlcpy(opts->udpbdIp, ipAddr, count + 1);
  return;
}

// Loads NHDDL options from optionsFile on memory card
void initOptions(char *basePath) {
  LAUNCHER_OPTIONS.is480pEnabled = 0;
  LAUNCHER_OPTIONS.mode = MODE_ATA;
  LAUNCHER_OPTIONS.udpbdIp[0] = '\0';

  char lineBuffer[PATH_MAX + sizeof(optionsFile) + 1];
  strcpy(lineBuffer, basePath);
  strcat(lineBuffer, optionsFile);

  // Load NHDDL options file into ArgumentList
  ArgumentList *options = calloc(1, sizeof(ArgumentList));
  if (loadArgumentList(options, lineBuffer)) {
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

  // If mode is set to UDPBD, but udpbd_ip was not set,
  // try to get IP from IPCONFIG.DAT
  if ((LAUNCHER_OPTIONS.mode == MODE_UDPBD) && !strlen(LAUNCHER_OPTIONS.udpbdIp)) {
    parseIPConfig(&LAUNCHER_OPTIONS);
  }
}
