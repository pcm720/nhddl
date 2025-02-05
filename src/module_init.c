#include "module_init.h"
#include "common.h"
#include "gui.h"
#include <ctype.h>
#include <debug.h>
#include <fcntl.h>
#include <iopcontrol.h>
#include <libmc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Macros for loading embedded IOP modules
#define IRX_DEFINE(mod)                                                                                                                              \
  extern unsigned char mod##_irx[] __attribute__((aligned(16)));                                                                                     \
  extern uint32_t size_##mod##_irx

// Defines moduleList entry for embedded module
#define INT_MODULE(mod, mode, argFunc, initType) {#mod, mod##_irx, &size_##mod##_irx, 0, NULL, argFunc, mode, {NULL}, 0, initType}

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
IRX_DEFINE(smap_udpbd);
IRX_DEFINE(ps2hdd);
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
  char *path[2];                  // Relative path to module (in case module is external)
  uint8_t loaded;
  ModuleInitType initType;
} ModuleListEntry;

// Initializes SMAP arguments
char *initSMAPArguments(uint32_t *argLength);
// Initializes PS2HDD-BDM arguments
char *initPS2HDDArguments(uint32_t *argLength);
// Initializes PS2FS arguments
char *initPS2FSArguments(uint32_t *argLength);

// List of modules to load
static ModuleListEntry moduleList[] = {
    //
    // Base modules
    //
    INT_MODULE(iomanX, MODE_ALL, NULL, INIT_TYPE_PARTIAL),
    INT_MODULE(fileXio, MODE_ALL, NULL, INIT_TYPE_PARTIAL),
    INT_MODULE(sio2man, MODE_ALL, NULL, INIT_TYPE_PARTIAL),
    INT_MODULE(mcman, MODE_ALL, NULL, INIT_TYPE_PARTIAL),
    INT_MODULE(mcserv, MODE_ALL, NULL, INIT_TYPE_PARTIAL),
    INT_MODULE(freepad, MODE_ALL, NULL, INIT_TYPE_PARTIAL),
    INT_MODULE(mmceman, MODE_ALL, NULL, INIT_TYPE_PARTIAL), // MMCE driver
    //
    // Backend modules
    //
    // DEV9
    INT_MODULE(ps2dev9, MODE_UDPBD | MODE_ATA | MODE_HDL, NULL, INIT_TYPE_FULL),
    // BDM
    INT_MODULE(bdm, MODE_ALL, NULL, INIT_TYPE_FULL),
    // FAT/exFAT
    INT_MODULE(bdmfs_fatfs, MODE_ALL, NULL, INIT_TYPE_FULL),
    // SMAP driver. Actually includes small IP stack and UDPTTY
    INT_MODULE(smap_udpbd, MODE_UDPBD, &initSMAPArguments, INIT_TYPE_FULL),
    // ATA
    INT_MODULE(ata_bd, MODE_ATA | MODE_HDL, NULL, INIT_TYPE_FULL),
    // USBD
    INT_MODULE(usbd_mini, MODE_USB, NULL, INIT_TYPE_FULL),
    // USB Mass Storage
    INT_MODULE(usbmass_bd_mini, MODE_USB, NULL, INIT_TYPE_FULL),
    // MX4SIO
    INT_MODULE(mx4sio_bd_mini, MODE_MX4SIO, NULL, INIT_TYPE_FULL),
    // iLink
    INT_MODULE(iLinkman, MODE_ILINK, NULL, INIT_TYPE_FULL),
    // iLink Mass Storage
    INT_MODULE(IEEE1394_bd_mini, MODE_ILINK, NULL, INIT_TYPE_FULL),
    // PS2HDD driver
    INT_MODULE(ps2hdd, MODE_HDL, &initPS2HDDArguments, INIT_TYPE_FULL),
    // PFS driver
    INT_MODULE(ps2fs, MODE_HDL, &initPS2FSArguments, INIT_TYPE_FULL),
};
#define MODULE_COUNT sizeof(moduleList) / sizeof(ModuleListEntry)

// Returns 0 if memory card in mc1 is not a formatted PS2 memory card
// Used to avoid loading MX4SIO module and disabling mc1
int getMC1Type();

// Loads module, executing argument function if it's present
int loadModule(ModuleListEntry *mod);

// Initializes IOP modules
int initModules(ModuleInitType initType) {
  int ret = 0;

  // Skip rebooting IOP if modules were loaded previously
  if (!moduleList[0].loaded) {
    printf("Rebooting IOP\n");
    while (!SifIopReset("", 0)) {
    };
    while (!SifIopSync()) {
    };

    // Initialize the RPC manager
    SifInitRpc(0);

    // Apply patches required to load modules from EE RAM
    if ((ret = sbv_patch_enable_lmb()))
      return ret;
    if ((ret = sbv_patch_disable_prefix_check()))
      return ret;
  }

  // Load modules
  for (int i = 0; i < MODULE_COUNT; i++) {
    if ((initType == INIT_TYPE_PARTIAL) && (moduleList[i].initType == INIT_TYPE_FULL))
      return 0; // Return if partial init is requested and current module is listed only for full init

    if (moduleList[i].loaded) // Ignore already loaded modules
      continue;

    if ((moduleList[i].irx != NULL) && (moduleList[i].size != NULL) && (moduleList[i].mode & LAUNCHER_OPTIONS.mode)) {
      if ((ret = loadModule(&moduleList[i]))) {
        uiSplashLogString(LEVEL_ERROR, "Failed to initialize module %s: %d\n", moduleList[i].name, ret);
        return ret;
      }
      moduleList[i].loaded = 1;
    }
    // Clean up arguments
    if (moduleList[i].argStr != NULL)
      free(moduleList[i].argStr);
  }
  return 0;
}

// Loads module, executing argument function if it's present
int loadModule(ModuleListEntry *mod) {
  int ret, iopret = 0;
  if ((mod->mode == MODE_MX4SIO) && getMC1Type()) {
    // If mc1 is a valid memory card, skip MX4SIO modules
    uiSplashLogString(LEVEL_WARN, "Skipping %s\n(memory card is inserted in slot 2)\n", mod->name);
    return 0;
  }

  uiSplashLogString(LEVEL_INFO_NODELAY, "Loading %s\n", mod->name);

  // If module has an arugment function, execute it
  if (mod->argumentFunction != NULL) {
    mod->argStr = mod->argumentFunction(&mod->argLength);
    if (mod->argStr == NULL) {
      // Ignore errors if module can fail
      ret = -EINVAL;
      goto failCheck;
    }
  }

  ret = SifExecModuleBuffer(mod->irx, *mod->size, mod->argLength, mod->argStr, &iopret);
  if (ret >= 0)
    ret = 0;
  if (iopret == 1)
    ret = iopret;

failCheck:
  if ((ret != 0) &&                                                 // If module failed to initialize
      (mod->mode != MODE_ALL) &&                                    // Module is not required
      ((mod->mode & LAUNCHER_OPTIONS.mode) ^ LAUNCHER_OPTIONS.mode) // Module mode is not the only one enabled
  ) {
    // Exclude mode from target modes
    uiSplashLogString(LEVEL_WARN, "Failed to load module %s\nSome modes might not be available\n", mod->name);
    LAUNCHER_OPTIONS.mode ^= mod->mode;
    return 0;
  }

  return ret;
}

// Returns 0 if memory card in mc1 is not a formatted PS2 memory card
// Can be used to avoid loading MX4SIO module
int getMC1Type() {
  if (mcInit(MC_TYPE_XMC)) {
    printf("ERROR: Failed to initialize libmc\n");
    return -ENODEV;
  }

  int mc1Type = 0;
  // Get memory card type for mc1
  mcGetInfo(1, 0, &mc1Type, NULL, NULL);
  mcSync(0, NULL, NULL);
  mcReset();
  return mc1Type;
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
    if (LAUNCHER_OPTIONS.mode & MODE_UDPBD) {
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

  strlcpy(LAUNCHER_OPTIONS.udpbdIp, ipAddr, count + 1);
  return strlen(LAUNCHER_OPTIONS.udpbdIp);
}

// Builds IP address argument for SMAP modules
char *initSMAPArguments(uint32_t *argLength) {
  // If udpbd_ip was not set, try to get IP from IPCONFIG.DAT
  if ((LAUNCHER_OPTIONS.udpbdIp[0] == '\0') && (parseIPConfig() <= 0)) {
    return NULL;
  }

  char ipArg[19]; // 15 bytes for IP string + 3 bytes for 'ip='
  *argLength = 19;
  char *argStr = calloc(sizeof(char), 19);
  snprintf(argStr, sizeof(ipArg), "ip=%s", LAUNCHER_OPTIONS.udpbdIp);
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
  return argStr;
}
