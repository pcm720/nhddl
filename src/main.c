#include "common.h"
#include "backends/backends.h"
#include "devices/devices.h"
#include "backends/title_id.h"
#include "dprintf.h"
#include "forwarder.h"
#include "neutrino.h"
#include "options.h"
#include "target.h"
#include "ui/ui.h"
#include <ctype.h>
#include <debug.h>
#include <fcntl.h>
#include <kernel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Launcher options
LauncherOptions LAUNCHER_OPTIONS;
// Options file name relative to CWD
static const char optionsFile[] = "nhddl.yaml";
// nhddl.yaml fallback paths
static char *nhddlFallbackPaths[] = {
    "mcX:/APP_NHDDL/nhddl.yaml",
};
static char nhddlStorageFallbackPath[] = "/nhddl/nhddl.yaml";

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
int init(DeviceType mode);
// Loads NHDDL options from optionsFile
int loadOptions(char *cwdPath, ModuleInitType initType);
// Attempts to parse argv into LAUNCHER_OPTIONS
void parseArgv(int argc, char *argv[]);
// Parses argv[0] for mode postfix
DeviceType parseFilename(const char *path);
// Tries to load IPCONFIG.DAT from memory card
void parseIPConfig();

int main(int argc, char *argv[]) {
  DPRINTF("*************\nNHDDL %s\nA Neutrino launcher by pcm720\n*************\n", GIT_VERSION);

  for (int i = 0; i < argc; i++)
    DPRINTF("argv[%d] = %s\n", i, argv[i]);

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

  DPRINTF("Initializing UI\n");
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
      DPRINTF("WARN: failed to scan %s: %d\n", deviceModeMap[i].mountpoint, res);
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
  DPRINTF("UI loop done, exiting\n");
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
int init(DeviceType mode) {
  // Initialize launcher options
  LAUNCHER_OPTIONS.vmode = VMode_NONE;
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
DeviceType parseMode(const char *modeStr) {
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
DeviceType parseFilename(const char *path) {
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
    return VMode_NTSC;
  if (!strcmp(modeStr, "pal"))
    return VMode_PAL;
  if (!strcmp(modeStr, "480p"))
    return VMode_480p;
  return VMode_NONE;
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
      DPRINTF("Using VMode %s\n", val);
      LAUNCHER_OPTIONS.vmode = parseVMode(val);
    } else if (val && !strcmp(OPTION_MODE, arg)) {
      DPRINTF("Using mode %s\n", val);
      LAUNCHER_OPTIONS.mode |= parseMode(val);
    } else if (val && !strcmp(OPTION_UDPBD_IP, arg)) {
      DPRINTF("Using UDPBD IP %s\n", val);
      strlcpy(LAUNCHER_OPTIONS.udpbdIp, val, sizeof(LAUNCHER_OPTIONS.udpbdIp));
    } else if (val && !strcmp(OPTION_IMAGE, arg)) {
      DPRINTF("Using image %s\n", val);
      LAUNCHER_OPTIONS.image = strdup(val);
    } else if (!strcmp(OPTION_NO_INIT, arg)) {
      DPRINTF("Skipping IOP init\n");
      LAUNCHER_OPTIONS.noInit = 1;
    }
  }

  if (LAUNCHER_OPTIONS.mode == MODE_NONE)
    LAUNCHER_OPTIONS.mode = MODE_ALL;
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
    DPRINTF("Can't load options file, will use defaults\n");
    return -ENOENT;
  }

fileExists:
  // Load NHDDL options file into ArgumentList
  ArgumentList *options = calloc(1, sizeof(ArgumentList));
  if (loadArgumentList(options, NULL, lineBuffer)) {
    // Else, fail
    DPRINTF("Can't load options file, will use defaults\n");
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

// Attempts to detect root device and load device drivers required for accessing CWD
// Returns root path to device ELF
char *resolveRootDevice(char *argv0) {
  char *result = argv0;
  // Load device drivers for boot path
  printf("argv[0] is %s, guessing device type\n", argv0);
  uint32_t device = devices_guess_device_type(argv0);
  if (device & Device_MMCE) {
    printf("main: loading MMCE drivers\n");
    device_init_load_modules("mmce");
    return result;
  } else if (device & Device_HDD) {
    printf("main: loading HDD drivers\n");
    device_init_load_modules("hdd");
    devices_probe(argv0, 10);
    return result;
  } else if (device & Device_BDM) {
    printf("main: loading BDM drivers\n");
    device_init_load_modules("usb");
    // probe cwd
    device_init_load_modules("hdd");
    // probe cwd
    device_init_load_modules("mx4sio");
    // probe cwd
    // We do not support loading NHDDL from UDPBD and iLink
  } else
    return result;

  // Find current working directory for BDM
  if (device & Device_BDM) {
    printf("main: probing root path for BDM device\n");
    if (argv0[4] == ':' || argv0[6] != '/') {
      // argv[0] is "mass:" or doesn't have a trailing slash, fix it to "mass?:/"
      int arglen = strlen(argv0) + 3;
      result = (char *)malloc(arglen);
      int startPos = ((argv0[5] == '/') || (argv0[5] == ':')) ? 6 : 5;
      snprintf(result, arglen, "mass?:/%s", &argv0[startPos]);
    }

    int fd = 0;
    int attempts = 0;
    for (int i = 0; i < 8; i++) {
      result[4] = '0' + i;
      printf("main: probing %s\n", result);
      if (devices_probe(result, 2)) {
        printf("main: failed to probe\n");
        return argv0; // No BDM devices were found
      }
      fd = open(result, O_RDONLY);
      if (fd >= 0) {
        printf("main: found root path\n");
        close(fd);
        return result;
      }
    }
  }
  return argv0;
}
