#include "common.h"
#include "devices.h"
#include "gui.h"
#include "launcher.h"
#include "module_init.h"
#include "options.h"
#include "target.h"
#include "title_id.h"
#include <ctype.h>
#include <debug.h>
#include <fcntl.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Launcher options
LauncherOptions LAUNCHER_OPTIONS;
// Path to Neutrino ELF
char NEUTRINO_ELF_PATH[PATH_MAX + 1];
// Options file name relative to CWD
static const char optionsFile[] = "nhddl.yaml";
// nhddl.yaml fallback paths
static char *nhddlFallbackPaths[] = {
    "mcX:/APP_NHDDL/nhddl.yaml",
};
static char nhddlStorageFallbackPath[] = "/nhddl/nhddl.yaml";
// Neutrino ELF name relative to CWD
static const char neutrinoELF[] = "neutrino.elf";
// neutrino.elf fallback paths
static char *neutrinoMCFallbackPaths[] = {
    "mcX:/APPS/neutrino/neutrino.elf",
    "mcX:/NEUTRINO/NEUTRINO.ELF",
    "mcX:/NEUTRINO/neutrino.elf",
};
static char neutrinoStorageFallbackPath[] = "/neutrino/neutrino.elf";

// Supported options
#define OPTION_VMODE "video"
#define OPTION_MODE "mode"
#define OPTION_UDPBD_IP "udpbd_ip"
#define OPTION_IMAGE "dvd"
#define OPTION_NO_INIT "noinit"

#ifndef GIT_VERSION
#define GIT_VERSION "v-0.0.0-unknown"
#endif

// Does a quick init for options given in argv
int argInit();
// Initializes modules, NHDDL configuraton, Neutrino path and device map
int init(ModeType mode);
// Loads NHDDL options from optionsFile
int loadOptions(char *cwdPath, ModuleInitType initType);
// Attempts to parse argv into LAUNCHER_OPTIONS
void parseArgv(int argc, char *argv[]);
// Parses argv[0] for mode postfix
ModeType parseFilename(const char *path);
// Attempts to find neutrino.elf at current path or one of fallback paths
int findNeutrinoELF(char *cwdPath, ModuleInitType initType);
// Reads version.txt from NEUTRINO_ELF_PATH; returns empty string if the file could not be read
char *getNeutrinoVersion();
// Tries to load IPCONFIG.DAT from memory card
void parseIPConfig();
// Forwards the image to Neutrino without loading the UI
int forwardBoot();

int main(int argc, char *argv[]) {
  printf("*************\nNHDDL %s\nA Neutrino launcher by pcm720\n*************\n", GIT_VERSION);

  for (int i = 0; i < argc; i++)
    printf("argv[%d] = %s\n", i, argv[i]);

  // Parse arguments
  if ((argc > 0 && argv[0][0] == '-') || (argc > 1 && argv[1][0] == '-'))
    parseArgv(argc, argv);

  int res;
  if (LAUNCHER_OPTIONS.image && ((LAUNCHER_OPTIONS.mode != MODE_NONE) || LAUNCHER_OPTIONS.mode != MODE_HDL)) {
    res = forwardBoot();
    init_scr();
    logString("\n\nERROR: Failed to forward to Neutrino: %d\n", res);
    goto fail;
  }

  printf("Initializing UI\n");
  if ((res = uiInit())) {
    init_scr();
    logString("\n\nERROR: Failed to init UI: %d\n", res);
    goto fail;
  }

  // Start splash screen thread
  if ((res = startSplashScreen()) < 0) {
    init_scr();
    logString("\n\nERROR: Failed to start splash screen thread: %d\n", res);
    goto fail;
  }

  if ((argc > 0 && argv[0][0] == '-') || (argc > 1 && argv[1][0] == '-'))
    // If argv contains arguments, use them for init
    res = argInit();
  else if (argv && argv[0])
    res = init(parseFilename(argv[0]));
  else
    res = init(MODE_NONE);

  if (res)
    goto fail;

  uiSplashLogString(LEVEL_INFO_NODELAY, "Building target list...\n");

  TargetList *titles = malloc(sizeof(TargetList));
  titles->total = 0;
  titles->first = NULL;
  titles->last = NULL;

  // Scan every initialized device for entries
  for (int i = 0; i < MAX_DEVICES; i++) {
    if (deviceModeMap[i].mode == MODE_NONE || deviceModeMap[i].mountpoint == NULL)
      break;

    // Ignore devices without a scan function
    if (deviceModeMap[i].scan == NULL)
      continue;

    res = deviceModeMap[i].scan(titles, &deviceModeMap[i]);
    if (res != 0) {
      printf("WARN: failed to scan %s: %d\n", deviceModeMap[i].mountpoint, res);
    }
  }

  if (titles->total == 0) {
    freeTargetList(titles);
    uiSplashLogString(LEVEL_WARN, "No targets found\n");
    goto fail;
  }

  stopUISplashThread();
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

// Initializes device map while logging errors
int initDevices() {
  uiSplashLogString(LEVEL_INFO, "Waiting for storage devices...\n");
  int res = initDeviceMap();
  if ((res < 0)) {
    uiSplashLogString(LEVEL_ERROR, "Failed to initialize devices\n");
    return -EIO;
  }
  if (!res) {
    uiSplashLogString(LEVEL_ERROR, "No devices found\n");
    return -ENODEV;
  }
  return 0;
}

// Reads Neutrino version and displays Neutrino path and version on the splash screen
void showNeutrinoSplash() {
  // Get Neturino version
  char *neutrinoVersion = getNeutrinoVersion();
  uiSplashSetNeutrinoVersion(neutrinoVersion);
  uiSplashLogString(LEVEL_INFO, "Found Neutrino at\n%s\n", NEUTRINO_ELF_PATH);
  free(neutrinoVersion);
}

// Does a quick init for options given in argv
int argInit() {
  int res;
  char cwdPath[PATH_MAX + 1];

  if ((res = initModules(INIT_TYPE_FULL)) != 0)
    return res;

  // Initialize device map
  if (initDevices() < 0)
    return -EIO;

  // Search for neutrino.elf
  getcwd(cwdPath, PATH_MAX + 1);
  if (findNeutrinoELF(cwdPath, INIT_TYPE_FULL)) {
    uiSplashLogString(LEVEL_ERROR, "Couldn't find neutrino.elf\n");
    return -ENOENT;
  }

  showNeutrinoSplash();
  return 0;
}

// Initializes modules, NHDDL configuraton, Neutrino path and device map
int init(ModeType mode) {
  // Initialize launcher options
  LAUNCHER_OPTIONS.vmode = VMODE_NONE;
  LAUNCHER_OPTIONS.mode = mode;
  LAUNCHER_OPTIONS.udpbdIp[0] = '\0';

  int initType = INIT_TYPE_BASIC;
  int optionsFileNotRead = -1;
  int neutrinoNotFound = -1;
  int res;
  char cwdPath[PATH_MAX + 1];
  if (LAUNCHER_OPTIONS.mode != MODE_NONE) {
    // If specific mode is requested, skip CWD handling
    initType = INIT_TYPE_FULL;
    cwdPath[0] = '\0';
  } else {
    // Set initial init type to basic
    initType = INIT_TYPE_BASIC;
    LAUNCHER_OPTIONS.mode = MODE_ALL;
    int fd;

    // Get CWD and try to open it
    if (getcwd(cwdPath, PATH_MAX + 1)) {
      if (cwdPath[strlen(cwdPath) - 1] != '/') // Add path separator if cwd doesn't have one
        strcat(cwdPath, "/");

      if ((fd = open(cwdPath, O_RDONLY | O_DIRECTORY)) >= 0) {
        close(fd);

        // Try to load options from CWD
        if ((optionsFileNotRead = loadOptions(cwdPath, INIT_TYPE_FULL)) >= 0)
          initType = INIT_TYPE_FULL; // Set full level if options file was loaded
      }
    }
  }

  uiSplashLogString(LEVEL_INFO_NODELAY, "Loading modules...\n");

  while (initType <= INIT_TYPE_FULL) {
    if (LAUNCHER_OPTIONS.mode == MODE_ALL) // Exclude MX4SIO to avoid conflicts unless explicitly requested
      LAUNCHER_OPTIONS.mode = MODE_ALL & ~MODE_MX4SIO;

    // Load modules associated with target init type
    if ((res = initModules(initType)) != 0)
      return res;

    // Initialize device map after full init
    if ((initType == INIT_TYPE_FULL) && (initDevices() < 0))
      return -EIO;

    // Try to init options
    if ((optionsFileNotRead = loadOptions(cwdPath, initType)) < 0)
      cwdPath[0] = '\0'; // Drop CWD if there was no options file

    // Search for neutrino.elf
    if ((neutrinoNotFound < 0) && !(neutrinoNotFound = findNeutrinoELF(cwdPath, initType)))
      showNeutrinoSplash();

    // If options file was read, advance init level to full
    if ((optionsFileNotRead >= 0) && (initType != INIT_TYPE_FULL))
      initType = INIT_TYPE_FULL;
    else
      initType += 1;
  }

  if (neutrinoNotFound < 0) {
    uiSplashLogString(LEVEL_ERROR, "Couldn't find neutrino.elf\n");
    return -ENOENT;
  }

  return 0;
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
  if (!strcmp(modeStr, "mmce"))
    return MODE_MMCE;
  if (!strcmp(modeStr, "hdl"))
    return MODE_HDL;
  return MODE_ALL;
}

// Parses argv[0] for mode postfix
ModeType parseFilename(const char *path) {
  char *modeStr = strrchr(path, '-');
  if (!modeStr)
    return MODE_NONE;

  modeStr++;

  if (!strncmp(modeStr, "ata", 3))
    return MODE_ATA;
  if (!strncmp(modeStr, "m4s", 3))
    return MODE_MX4SIO;
  if (!strncmp(modeStr, "udpbd", 5))
    return MODE_UDPBD;
  if (!strncmp(modeStr, "usb", 3))
    return MODE_USB;
  if (!strncmp(modeStr, "ilink", 5))
    return MODE_ILINK;
  if (!strncmp(modeStr, "mmce", 4))
    return MODE_MMCE;
  if (!strncmp(modeStr, "hdl", 3))
    return MODE_HDL;

  return MODE_NONE;
}

// Parses video mode string into enum
VModeType parseVMode(const char *modeStr) {
  if (!strcmp(modeStr, "ntsc"))
    return VMODE_NTSC;
  if (!strcmp(modeStr, "pal"))
    return VMODE_PAL;
  if (!strcmp(modeStr, "480p"))
    return VMODE_480P;
  return VMODE_NONE;
}

// Attempts to parse argv into LAUNCHER_OPTIONS
void parseArgv(int argc, char *argv[]) {
  LAUNCHER_OPTIONS.mode = MODE_NONE;
  char *arg;
  for (int i = 0; i < argc; i++) {
    arg = argv[i];
    if ((arg == NULL) || (arg[0] != '-'))
      continue;

    // Find argument name
    char *val = strchr(arg, '=');
    if (val) {
      // Terminate argument and advance pointers to point to value and argument
      *val = '\0';
      val++;
    }
    arg++;

    if (val && !strcmp(OPTION_VMODE, arg)) {
      printf("Using VMode %s\n", val);
      LAUNCHER_OPTIONS.vmode = parseVMode(val);
    } else if (val && !strcmp(OPTION_MODE, arg)) {
      printf("Using mode %s\n", val);
      LAUNCHER_OPTIONS.mode |= parseMode(val);
    } else if (val && !strcmp(OPTION_UDPBD_IP, arg)) {
      printf("Using UDPBD IP %s\n", val);
      strlcpy(LAUNCHER_OPTIONS.udpbdIp, val, sizeof(LAUNCHER_OPTIONS.udpbdIp));
    } else if (val && !strcmp(OPTION_IMAGE, arg)) {
      printf("Using image %s\n", val);
      LAUNCHER_OPTIONS.image = strdup(val);
    } else if (!strcmp(OPTION_NO_INIT, arg)) {
      printf("Skipping IOP init\n");
      LAUNCHER_OPTIONS.noInit = 1;
    }
  }

  if (LAUNCHER_OPTIONS.mode == MODE_NONE)
    LAUNCHER_OPTIONS.mode = MODE_ALL;
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

// Loads NHDDL options from optionsFile
int loadOptions(char *cwdPath, ModuleInitType initType) {
  char lineBuffer[PATH_MAX + sizeof(optionsFile) + 1];
  if (cwdPath[0] != '\0') {
    // If path is valid, try it
    strcpy(lineBuffer, cwdPath);
    strcat(lineBuffer, optionsFile);
    if (!tryFile(lineBuffer))
      // Skip fallbacks
      goto fileExists;
  }

  if (initType == INIT_TYPE_FULL) {
    // If config file doesn't exist in CWD and all modules are loaded, try fallback paths
    struct DeviceMapEntry *device;
    for (int i = 0; i < MAX_DEVICES; i++) {
      lineBuffer[0] = '\0';
      if (deviceModeMap[i].mode == MODE_NONE)
        break;

      if (deviceModeMap[i].metadev)
        device = deviceModeMap[i].metadev;
      else
        device = &deviceModeMap[i];

      if (device->mountpoint != NULL) {
        strcpy(lineBuffer, device->mountpoint);
        strcat(lineBuffer, nhddlStorageFallbackPath);
        if (!tryFile(lineBuffer))
          goto fileExists;
      }
    }
  }

  if (initType > INIT_TYPE_BASIC) {
    // Try MMCE if init type is EXTENDED or FULL
    for (int i = 0; i < 2; i++) {
      sprintf(lineBuffer, "mmce%d:%s", i, nhddlStorageFallbackPath);
      if (!tryFile(lineBuffer))
        break;
    }
  }

  // Fallback to memory card paths
  lineBuffer[0] = '\0';
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < (sizeof(nhddlFallbackPaths) / sizeof(char *)); j++) {
      nhddlFallbackPaths[j][2] = i + '0';
      if (!tryFile(nhddlFallbackPaths[j])) {
        strcpy(lineBuffer, nhddlFallbackPaths[j]);
        break;
      }
    }
  }

  if (lineBuffer[0] == '\0') {
    printf("Can't load options file, will use defaults\n");
    return -ENOENT;
  }

fileExists:
  // Load NHDDL options file into ArgumentList
  ArgumentList *options = calloc(1, sizeof(ArgumentList));
  if (loadArgumentList(options, NULL, lineBuffer)) {
    // Else, fail
    printf("Can't load options file, will use defaults\n");
    freeArgumentList(options);
    return -ENOENT;
  }

  // Parse the list into Options
  Argument *arg = options->first;
  while (arg != NULL) {
    if (!arg->isDisabled) {
      if (strcmp(OPTION_VMODE, arg->arg) == 0) {
        LAUNCHER_OPTIONS.vmode = parseVMode(arg->value);
      } else if (strcmp(OPTION_MODE, arg->arg) == 0) {
        // Reset MODE_ALL to MODE_NONE if mode flag exists
        if (LAUNCHER_OPTIONS.mode == MODE_ALL)
          LAUNCHER_OPTIONS.mode = MODE_NONE;
        LAUNCHER_OPTIONS.mode |= parseMode(arg->value);
      } else if (strcmp(OPTION_UDPBD_IP, arg->arg) == 0) {
        strlcpy(LAUNCHER_OPTIONS.udpbdIp, arg->value, sizeof(LAUNCHER_OPTIONS.udpbdIp));
      }
    }
    arg = arg->next;
  }
  freeArgumentList(options);

  return 0;
}

// Attempts to find neutrino.elf at current path or one of fallback paths
int findNeutrinoELF(char *cwdPath, ModuleInitType initType) {
  if (cwdPath && cwdPath[0] != '\0') {
    // If path is valid, try it
    strcpy(NEUTRINO_ELF_PATH, cwdPath);
    strcat(NEUTRINO_ELF_PATH, neutrinoELF);
    if (!tryFile(NEUTRINO_ELF_PATH))
      return 0;
  }

  if (initType == INIT_TYPE_FULL) {
    // If neutrino.elf doesn't exist in CWD and all modules are loaded, try fallback paths on storage devices
    struct DeviceMapEntry *device;
    for (int i = 0; i < MAX_DEVICES; i++) {
      NEUTRINO_ELF_PATH[0] = '\0';
      if (deviceModeMap[i].mode == MODE_NONE)
        break;

      if (deviceModeMap[i].metadev)
        device = deviceModeMap[i].metadev;
      else
        device = &deviceModeMap[i];

      if (device->mountpoint != NULL) {
        strcpy(NEUTRINO_ELF_PATH, device->mountpoint);
        strcat(NEUTRINO_ELF_PATH, neutrinoStorageFallbackPath);
        if (!tryFile(NEUTRINO_ELF_PATH))
          return 0;
      }
    }
  }

  if (initType > INIT_TYPE_BASIC) {
    // Try MMCE if init type is EXTENDED or FULL
    for (int i = 0; i < 2; i++) {
      sprintf(NEUTRINO_ELF_PATH, "mmce%d:%s", i, neutrinoStorageFallbackPath);
      if (!tryFile(NEUTRINO_ELF_PATH))
        return 0;
    }
  }

  // Fallback to memory card paths
  NEUTRINO_ELF_PATH[0] = '\0';
  for (int i = 0; i < 2; i++) {
    for (int j = 0; j < (sizeof(neutrinoMCFallbackPaths) / sizeof(char *)); j++) {
      neutrinoMCFallbackPaths[j][2] = i + '0';
      if (!tryFile(neutrinoMCFallbackPaths[j])) {
        strcpy(NEUTRINO_ELF_PATH, neutrinoMCFallbackPaths[j]);
        return 0;
      }
    }
  }

  if (NEUTRINO_ELF_PATH[0] == '\0') {
    return -ENOENT;
  }
  return 0;
}

// Reads version.txt from NEUTRINO_ELF_PATH
// Returns empty string if the file could not be read
char *getNeutrinoVersion() {
  // Get full path to Neutrino directory
  const char *slashIdx = strrchr(NEUTRINO_ELF_PATH, '/');
  if (slashIdx == NULL)
    return strdup("");

  // Get the length of directory path
  int len = slashIdx - NEUTRINO_ELF_PATH;

  // Build path to version.txt
  char versionFilePath[PATH_MAX];
  strncpy(versionFilePath, NEUTRINO_ELF_PATH, len);
  versionFilePath[len] = '\0';
  strcat(versionFilePath, "/version.txt");

  // Open version.txt
  FILE *file = fopen(versionFilePath, "r");
  if (file == NULL)
    return strdup("");

  // Read the first line into versionFilePath, reusing it
  versionFilePath[0] = ' ';
  if (fgets(&versionFilePath[1], sizeof(versionFilePath) - 1, file) == NULL) {
    fclose(file);
    return strdup("");
  }

  fclose(file);

  // Trim newline
  len = strlen(versionFilePath);
  if (len > 0 && versionFilePath[len - 1] == '\n') {
    versionFilePath[len - 1] = '\0';
  }

  return strdup(versionFilePath);
}

// Quickly forwards the image to Neutrino without loading the UI
int forwardBoot() {
  int res;
  // Forward to Neutrino without loading the UI
  if (!LAUNCHER_OPTIONS.noInit && (res = initModules(INIT_TYPE_FULL)) != 0) {
    printf("Failed to init modules: %d\n", res);
    return res;
  }

  int deviceCount = initDeviceMap();
  if (deviceCount <= 0) {
    printf("Failed to init devices: %d\n", deviceCount);
    return -ENODEV;
  }

  if (tryFile(LAUNCHER_OPTIONS.image) < 0) {
    printf("Target image not found\n");
    return -ENOENT;
  }

  if (findNeutrinoELF(NULL, INIT_TYPE_FULL)) {
    printf("Failed to find Neutrino\n");
    return -ENOENT;
  }

  Target target = {
      .idx = 0,
      .id = getTitleID(LAUNCHER_OPTIONS.image),
      .fullPath = LAUNCHER_OPTIONS.image,
  };

  char *fileext = strrchr(LAUNCHER_OPTIONS.image, '.');
  if ((fileext != NULL) && (!strcmp(fileext, ".iso") || !strcmp(fileext, ".ISO"))) {
    // Get file name without the extension
    int nameLength = (int)(fileext - LAUNCHER_OPTIONS.image);
    target.name = calloc(sizeof(char), nameLength + 1);
    strncpy(target.name, LAUNCHER_OPTIONS.image, nameLength);
  }

  for (int i = 0; i < deviceCount; i++)
    if (strstr(LAUNCHER_OPTIONS.image, deviceModeMap[i].mountpoint)) {
      target.device = &deviceModeMap[i];
      break;
    }

  if (!target.device) {
    printf("Target device not found\n");
    return -ENODEV;
  }

  // Run the image
  launchTitle(&target, loadLaunchArgumentLists(&target));
  return -ENOENT;
}
