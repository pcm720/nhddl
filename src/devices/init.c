#include "devices/init.h"
#include "common.h"
#include "dprintf.h"
#include "ui/ui.h"
#include <ctype.h>
#include <debug.h>
#include <fcntl.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

// Macros for loading embedded IOP modules
#define IRX_DEFINE(mod)                                                                                                                              \
  extern unsigned char mod##_irx[] __attribute__((aligned(16)));                                                                                     \
  extern uint32_t size_##mod##_irx

// Defines moduleList entry for embedded module
#define INT_MODULE(mod, mode, argFunc) {#mod, mod##_irx, &size_##mod##_irx, 0, NULL, argFunc, mode}

// Embedded IOP modules
IRX_DEFINE(iomanX);
IRX_DEFINE(fileXio);
IRX_DEFINE(sio2man);
IRX_DEFINE(mcman);
IRX_DEFINE(mcserv);
IRX_DEFINE(freepad);
IRX_DEFINE(mmceman);
IRX_DEFINE(ps2dev9);
IRX_DEFINE(bdm);
IRX_DEFINE(bdmfs_fatfs);
IRX_DEFINE(ata_bd);
IRX_DEFINE(usbd_mini);
IRX_DEFINE(usbmass_bd_mini);
IRX_DEFINE(mx4sio_bd_mini);
IRX_DEFINE(iLinkman);
IRX_DEFINE(IEEE1394_bd_mini);
IRX_DEFINE(smap);
IRX_DEFINE(ministack);
IRX_DEFINE(udpfs_ioman);
IRX_DEFINE(ps2hdd_bdm);
IRX_DEFINE(ps2fs);

// Function used to initialize module arguments.
// Must set argLength and return non-null pointer to a argument string if successful.
// Returned pointer must point to dynamically allocated memory
typedef char *(*moduleArgFunc)(uint32_t *argLength);

typedef struct ModuleListEntry {
  char *name;                     // Module name
  unsigned char *irx;             // Pointer to IRX module
  uint32_t *size;                 // IRX size. Uses pointer to avoid compilation issues with internal modules
  uint32_t argLength;             // Total length of argument string
  char *argStr;                   // Module arguments
  moduleArgFunc argumentFunction; // Function used to initialize module arguments
  ModeType mode;                  // Used to ignore modules not required for target mode
} ModuleListEntry;

// Initializes ministack arguments
char *initMinistackArguments(uint32_t *argLength);
// Initializes PS2HDD arguments
char *initPS2HDDArguments(uint32_t *argLength);
// Initializes PS2FS arguments
char *initPS2FSArguments(uint32_t *argLength);

// List of modules to load
static ModuleListEntry moduleList[] = {
    //
    // Base modules
    //
    INT_MODULE(iomanX, MODE_ALL, NULL),
    INT_MODULE(fileXio, MODE_ALL, NULL),
    INT_MODULE(sio2man, MODE_ALL, NULL),
    INT_MODULE(mcman, MODE_ALL, NULL),
    INT_MODULE(mcserv, MODE_ALL, NULL),
    INT_MODULE(freepad, MODE_ALL, NULL),
    INT_MODULE(mmceman, MODE_ALL, NULL), // MMCE driver
    //
    // Backend modules
    //
    // DEV9
    INT_MODULE(ps2dev9, MODE_UDPFS | MODE_ATA | MODE_HDL, NULL),
    // BDM
    INT_MODULE(bdm, MODE_BDM, NULL),
    // FAT/exFAT
    INT_MODULE(bdmfs_fatfs, MODE_BDM, NULL),
    // UDPFS
    INT_MODULE(smap, MODE_UDPFS, NULL),
    INT_MODULE(ministack, MODE_UDPFS, &initMinistackArguments),
    INT_MODULE(udpfs_ioman, MODE_UDPFS, NULL),
    // ATA
    INT_MODULE(ata_bd, MODE_ATA | MODE_HDL, NULL),
    // USBD
    INT_MODULE(usbd_mini, MODE_USB, NULL),
    // USB Mass Storage
    INT_MODULE(usbmass_bd_mini, MODE_USB, NULL),
    // MX4SIO
    INT_MODULE(mx4sio_bd_mini, MODE_MX4SIO, NULL),
    // iLink
    INT_MODULE(iLinkman, MODE_ILINK, NULL),
    // iLink Mass Storage
    INT_MODULE(IEEE1394_bd_mini, MODE_ILINK, NULL),
    // PS2HDD driver
    INT_MODULE(ps2hdd_bdm, MODE_HDL, &initPS2HDDArguments),
    // PFS driver
    INT_MODULE(ps2fs, MODE_HDL, &initPS2FSArguments),
};
#define MODULE_COUNT sizeof(moduleList) / sizeof(ModuleListEntry)

// Loads module, executing argument function if it's present
int loadModule(ModuleListEntry *mod);

uint32_t loadedModules = 0;

// Initializes IOP modules
int initModules(ModeType modeType) {
  switch (modeType) {
  case MODE_NONE:
    // For MODE_NONE, only init RPC and fileXio
    sceSifInitRpc(0);
    fileXioInit();
    return 0;
  case MODE_ALL:
    // If MODE_ALL is received, clear state and force IOP reboot
    loadedModules = 0;
    LAUNCHER_OPTIONS.mode = 0;
    break;
  case MODE_MX4SIO:
    // Force IOP reboot if MMCE is initialized
    if (LAUNCHER_OPTIONS.mode & MODE_MMCE) {
      loadedModules = 0;
      LAUNCHER_OPTIONS.mode = 0;
    }
    break;
  case MODE_MMCE:
    // Force IOP reboot if MX4SIO is initialized
    if (LAUNCHER_OPTIONS.mode & MODE_MX4SIO) {
      loadedModules = 0;
      LAUNCHER_OPTIONS.mode = 0;
    }
    break;
  default:
    break;
  }
  int ret = 0;

  // Skip rebooting IOP if modules were loaded previously
  if (!loadedModules) {
    DPRINTF("Rebooting IOP\n");
    while (!SifIopReset("", 0)) {
    };
    while (!SifIopSync()) {
    };

    // Initialize the RPC manager
    sceSifInitRpc(0);

    // Apply patches required to load modules from EE RAM
    sbv_patch_enable_lmb();
    sbv_patch_disable_prefix_check();
    sbv_patch_fileio();
  }

  // Load modules
  for (int i = 0; i < MODULE_COUNT; i++) {
    if ((moduleList[i].mode != MODE_ALL) && !(modeType & moduleList[i].mode)) {
      continue;
    }

    if ((modeType & MODE_MX4SIO) && !strcmp(moduleList[i].name, "mmceman"))
      continue; // Do not load mmceman if MX4SIO mode is enabled to avoid conflicts

    if (loadedModules & (1 << i)) // Ignore already loaded modules
      continue;

    if ((moduleList[i].irx != NULL) && (moduleList[i].size != NULL)) {
      if ((ret = loadModule(&moduleList[i]))) {
        if ((modeType == MODE_ALL) && (moduleList[i].mode != MODE_ALL)) {
          // Ignore errors and disable the failed mode when loading all modes
          modeType &= ~(moduleList[i].mode);
          LAUNCHER_OPTIONS.mode &= ~(moduleList[i].mode);
          DPRINTF("Failed to initialize module %s: %d\n", moduleList[i].name, ret);
          continue;
        }

        uiSplashLogString(LEVEL_ERROR, "Failed to initialize module %s: %d\n", moduleList[i].name, ret);
        return ret;
      }
      loadedModules |= (1 << i);

      // Introduce delay to prevent ps2hdd module from hanging
      if ((modeType & MODE_HDL) && !strcmp(moduleList[i].name, "ata_bd"))
        sleep(1);

      // Explicitly init fileXio
      if (!strcmp(moduleList[i].name, "fileXio"))
        fileXioInit();
    }
    // Clean up arguments
    if (moduleList[i].argStr != NULL)
      free(moduleList[i].argStr);
  }

  LAUNCHER_OPTIONS.mode |= modeType;
  return 0;
}

// Loads module, executing argument function if it's present
int loadModule(ModuleListEntry *mod) {
  int ret, iopret = 0;

  uiSplashLogString(LEVEL_INFO_NODELAY, "Loading %s\n", mod->name);

  // If module has an arugment function, execute it
  if (mod->argumentFunction != NULL) {
    mod->argStr = mod->argumentFunction(&mod->argLength);
    if (mod->argStr == NULL)
      return -EINVAL;
  }

  ret = SifExecModuleBuffer(mod->irx, *mod->size, mod->argLength, mod->argStr, &iopret);
  if (ret >= 0)
    ret = 0;
  if (iopret == 1)
    ret = iopret;
  return ret;
}

// Tries to read SYS-CONF/IPCONFIG.DAT from memory card
int parseIPConfig() {
  // The 'X' in "mcX" will be replaced with memory card number
  static char ipconfigPath[] = "mcX:/SYS-CONF/IPCONFIG.DAT";

  int ipconfigFd, count;
  char ipAddr[16]; // IP address will not be longer than 15 characters
  for (char i = '0'; i < '2'; i++) {
    ipconfigPath[2] = i;
    // Attempt to open IPCONFIG.DAT
    ipconfigFd = open(ipconfigPath, O_RDONLY);
    if (ipconfigFd >= 0) {
      count = read(ipconfigFd, ipAddr, sizeof(ipAddr) - 1);
      close(ipconfigFd);
      break;
    }
  }

  if ((ipconfigFd < 0) || (count < sizeof(ipAddr) - 1)) {
    if (LAUNCHER_OPTIONS.mode & MODE_UDPFS) {
      uiSplashLogString(LEVEL_WARN, "Failed to get IP address from IPCONFIG.DAT\n");
    }
    return -ENOENT;
  }

  count = 0; // Reuse count as line index
  // In case IP address is shorter than 15 chars
  while (!isspace((unsigned char)ipAddr[count])) {
    // Advance index until we read a whitespace character
    count++;
  }

  strlcpy(LAUNCHER_OPTIONS.udpfsIp, ipAddr, count + 1);
  return strlen(LAUNCHER_OPTIONS.udpfsIp);
}

// Builds IP address argument for network modules
char *initMinistackArguments(uint32_t *argLength) {
  // If udpfs_ip was not set, try to get IP from IPCONFIG.DAT
  if ((LAUNCHER_OPTIONS.udpfsIp[0] == '\0') && (parseIPConfig() <= 0)) {
    return NULL;
  }

  char ipArg[19]; // 15 bytes for IP string + 3 bytes for 'ip='
  *argLength = 19;
  char *argStr = calloc(sizeof(char), 19);
  snprintf(argStr, sizeof(ipArg), "ip=%s", LAUNCHER_OPTIONS.udpfsIp);
  DPRINTF("with argument: %s\n", argStr);
  return argStr;
}

// up to 4 descriptors, 20 buffers
static char ps2hddArguments[] = "-o"
                                "\0"
                                "4"
                                "\0"
                                "-n"
                                "\0"
                                "20";
// Sets arguments for PS2HDD modules
char *initPS2HDDArguments(uint32_t *argLength) {
  *argLength = sizeof(ps2hddArguments);

  char *argStr = malloc(sizeof(ps2hddArguments));
  memcpy(argStr, ps2hddArguments, sizeof(ps2hddArguments));
  DPRINTF("with argument: %s\n", argStr);
  return argStr;
}

// up to 10 descriptors, 40 buffers
char ps2fsArguments[] = "-o"
                        "\0"
                        "10"
                        "\0"
                        "-n"
                        "\0"
                        "40";
// Sets arguments for PS2HDD modules
char *initPS2FSArguments(uint32_t *argLength) {
  *argLength = sizeof(ps2fsArguments);

  char *argStr = malloc(sizeof(ps2fsArguments));
  memcpy(argStr, ps2fsArguments, sizeof(ps2fsArguments));
  DPRINTF("with argument: %s\n", argStr);
  return argStr;
}
