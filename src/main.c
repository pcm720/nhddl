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
// The 'X' in "mcX" will be replaced with memory card number in parseIPConfig
static char ipconfigPath[] = "mcX:/SYS-CONF/IPCONFIG.DAT";
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
  if ((res = init_modules(ELF_BASE_PATH)) != 0) {
    logString("ERROR: Failed to initialize modules: %d\n", res);
    goto fail;
  }

  // If udpbd_ip was not set, try to get IP from IPCONFIG.DAT
  // Since MC might not be loaded before init_modules(),
  // parseIPConfig must be placed after all modules are loaded.
  if (!strlen(LAUNCHER_OPTIONS.udpbdIp)) {
    parseIPConfig(&LAUNCHER_OPTIONS);
  }

  logString("Initializing BDM devices...\n");
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
  logString("Found neutrino.elf at %s", NEUTRINO_ELF_PATH);

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
  if (!strcmp(modeStr, "ilink"))
    return MODE_ILINK;
  return MODE_ALL;
}

// Tries to read SYS-CONF/IPCONFIG.DAT from memory card
void parseIPConfig() {
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
    if (LAUNCHER_OPTIONS.mode == MODE_UDPBD)
      logString("WARN: Failed to get IP address from IPCONFIG.DAT\n");
    return;
  }

  count = 0; // Reuse count as line index
  // In case IP address is shorter than 15 chars
  while (!isspace((unsigned char)ipAddr[count])) {
    // Advance index until we read a whitespace character
    count++;
  }

  strlcpy(LAUNCHER_OPTIONS.udpbdIp, ipAddr, count + 1);
  return;
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
      if (tryFile(neutrinoMassFallbackPath)) {
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