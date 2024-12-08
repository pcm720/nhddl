#include "module_init.h"
#include "common.h"
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
#define INT_MODULE(mod, mode, argFunc) {#mod, mod##_irx, &size_##mod##_irx, 0, NULL, argFunc, mode, {NULL}, 0}

// Embedded IOP modules
IRX_DEFINE(iomanX);
IRX_DEFINE(fileXio);
IRX_DEFINE(sio2man);
IRX_DEFINE(mcman);
IRX_DEFINE(mcserv);
IRX_DEFINE(freepad);

#ifdef STANDALONE
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
#endif

// Function used to initialize module arguments.
// Must set argLength and return non-null pointer to a argument string if successful.
typedef char *(*moduleArgFunc)(uint32_t *argLength);

typedef struct ModuleListEntry {
  char *name;                     // Module name
  unsigned char *irx;             // Pointer to IRX module
  uint32_t *size;                 // IRX size. Uses pointer to avoid compilation issues with internal modules
  uint32_t argLength;             // Argument string length
  char *argStr;                   // Argument string
  moduleArgFunc argumentFunction; // Function used to initialize module arguments
  ModeType mode;                  // Used to ignore modules not required for target mode
  char *path[2];                  // Relative path to module (in case module is external)
  uint8_t loaded;
} ModuleListEntry;

// Initializes SMAP arguments
char *initSMAPArguments(uint32_t *argLength);

// List of modules to load
static ModuleListEntry moduleList[] = {
    // Embedded modules
    INT_MODULE(iomanX, MODE_ALL, NULL),
    INT_MODULE(fileXio, MODE_ALL, NULL),
    INT_MODULE(sio2man, MODE_ALL, NULL),
    INT_MODULE(mcman, MODE_ALL, NULL),
    INT_MODULE(mcserv, MODE_ALL, NULL),
    INT_MODULE(freepad, MODE_ALL, NULL),
#ifndef STANDALONE
    // DEV9
    {"dev9", NULL, NULL, 0, NULL, NULL, MODE_UDPBD | MODE_ATA, {"modules/dev9_ns.irx", "dev9_ns.irx"}, 0},
    // BDM
    {"bdm", NULL, NULL, 0, NULL, NULL, MODE_ALL, {"modules/bdm.irx", "bdm.irx"}, 0},
    // FAT/exFAT
    {"bdmfs_fatfs", NULL, NULL, 0, NULL, NULL, MODE_ALL, {"modules/bdmfs_fatfs.irx", "bdmfs_fatfs.irx"}, 0},
    // SMAP driver. Actually includes small IP stack and UDPTTY
    {"smap_udpbd", NULL, NULL, 0, NULL, &initSMAPArguments, MODE_UDPBD, {"modules/smap_udpbd.irx", "smap_udpbd.irx"}, 0},
    // ATA
    {"ata_bd", NULL, NULL, 0, NULL, NULL, MODE_ATA, {"modules/ata_bd.irx", "ata_bd.irx"}, 0},
    // USBD
    {"usbd_mini", NULL, NULL, 0, NULL, NULL, MODE_USB, {"modules/usbd_mini.irx", "usbd_mini.irx"}, 0},
    // USB Mass Storage
    {"usbmass_bd_mini", NULL, NULL, 0, NULL, NULL, MODE_USB, {"modules/usbmass_bd_mini.irx", "usbmass_bd_mini.irx"}, 0},
    // MX4SIO
    {"mx4sio_bd_mini", NULL, NULL, 0, NULL, NULL, MODE_MX4SIO, {"modules/mx4sio_bd_mini.irx", "mx4sio_bd_mini.irx"}, 0},
    // iLink
    {"iLinkman", NULL, NULL, 0, NULL, NULL, MODE_ILINK, {"modules/iLinkman.irx", "iLinkman.irx"}, 0},
    // iLink Mass Storage
    {"IEEE1394_bd_mini", NULL, NULL, 0, NULL, NULL, MODE_ILINK, {"modules/IEEE1394_bd_mini.irx", "IEEE1394_bd_mini.irx"}, 0},
#else
    // DEV9
    INT_MODULE(ps2dev9, MODE_UDPBD | MODE_ATA, NULL),
    // BDM
    INT_MODULE(bdm, MODE_ALL, NULL),
    // FAT/exFAT
    INT_MODULE(bdmfs_fatfs, MODE_ALL, NULL),
    // SMAP driver. Actually includes small IP stack and UDPTTY
    INT_MODULE(smap_udpbd, MODE_UDPBD, &initSMAPArguments),
    // ATA
    INT_MODULE(ata_bd, MODE_ATA, NULL),
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
#endif
};
#define MODULE_COUNT sizeof(moduleList) / sizeof(ModuleListEntry)

// Returns 0 if memory card in slot 1 is not a formatted memory card
// Used to avoid loading MX4SIO module and disabling mc1
int getMC1Type();

// Loads module, executing argument function if it's present
int loadModule(ModuleListEntry *mod);

// Frees dynamically allocated memory for ModuleListEntry
void freeModule();

// Initializes IOP modules
int initModules() {
  int ret = 0;

  // Skip rebooting IOP if modules were loaded previously
  if (!moduleList[0].loaded) {
    logString("Rebooting IOP\n");
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
  init_scr();
  logString("\n\nLoading modules:\n");
  for (int i = 0; i < MODULE_COUNT; i++) {
    if (moduleList[i].loaded) // Ignore already loaded modules
      continue;

    if ((moduleList[i].irx != NULL) && (moduleList[i].size != NULL) && (moduleList[i].mode & LAUNCHER_OPTIONS.mode)) {
      if ((ret = loadModule(&moduleList[i]))) {
        return ret;
      }
      moduleList[i].loaded = 1;
    }
    // Free loaded external module
    if ((moduleList[i].irx != NULL) && (moduleList[i].path[0] != NULL))
      freeModule(&moduleList[i]);
  }
  return 0;
}

// Loads module, executing argument function if it's present
int loadModule(ModuleListEntry *mod) {
  int ret, iopret = 0;
  if ((mod->mode == MODE_MX4SIO) && getMC1Type()) {
    // If mc1 is a valid memory card, skip MX4SIO modules
    logString("\tskipping %s (memory card inserted)\n", mod->name);
    return 0;
  }

  logString("\tloading %s\n", mod->name);

  // If module has an arugment function, execute it
  if (mod->argumentFunction != NULL) {
    mod->argStr = mod->argumentFunction(&mod->argLength);
    if (mod->argStr == NULL) {
      // Ignore errors if module can fail
      ret = -EINVAL;
      goto failCheck;
    }
  }

  if (mod->argStr != NULL)
    logString("\t\twith %s\n", mod->argStr);

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
    logString("\t\tFailed to load module (%d), some modes might not be available\n", ret);
    LAUNCHER_OPTIONS.mode ^= mod->mode;
    return 0;
  }

  return ret;
}

// Frees dynamically allocated memory for ModuleListEntry
// Must not be called on embedded modules
void freeModule(ModuleListEntry *mod) {
  free(mod->irx);
  free(mod->size);
  if (mod->argStr != NULL)
    free(mod->argStr);
}

// Loads external modules into memory
int loadExternalModules(char *basePath) {
  // Allocate memory for module paths
  int basePathLen = strlen(basePath);
  char pathBuf[PATH_MAX + 1];
  pathBuf[0] = '\0';
  strcpy(pathBuf, basePath);

  int i, res;
  int fd = 0;
  for (i = 0; i < MODULE_COUNT; i++) {
    // Ignore module if:
    if (((moduleList[i].irx != NULL) || (moduleList[i].path[0] == NULL)) // Module IRX is already in memory or doesn't have a path
        ||                                                               // or
        (!(moduleList[i].mode & LAUNCHER_OPTIONS.mode) &&                // Target mode doesn't match required mode
         moduleList[i].mode != MODE_ALL &&                               // Module is not required
         LAUNCHER_OPTIONS.mode != MODE_ALL)                              // Target mode is not set to ALL
    ) {
      continue;
    }

    // Append module path to base path
    for (int p = 0; p < (sizeof(moduleList[i].path) / sizeof(char *)); p++) {
      // End bufferred string at basePath for the next strcat in the loop
      pathBuf[basePathLen] = '\0';
      strcat(pathBuf, moduleList[i].path[p]);

      // Try to open module
      if ((fd = open(pathBuf, O_RDONLY)) < 0) {
        continue;
      }
      break;
    }
    if (fd < 0) {
      // Exclude mode from target modes
      LAUNCHER_OPTIONS.mode ^= moduleList[i].mode;
      logString("%s: Failed to open %s\n", moduleList[i].name, pathBuf);
      if ((moduleList[i].mode == MODE_ALL) || !((moduleList[i].mode & LAUNCHER_OPTIONS.mode) ^ LAUNCHER_OPTIONS.mode))
        goto fail; // Fail if module is required

      continue;
    }

    // Determine file size
    uint32_t fsize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    // Allocate memory for the module
    unsigned char *irxBuf = calloc(sizeof(char), fsize);
    if (irxBuf == NULL) {
      logString("\t%s: Failed to allocate memory\n", moduleList[i].name);
      close(fd);
      goto fail;
    }
    // Load module into buffer
    res = read(fd, irxBuf, fsize);
    if (res != fsize) {
      logString("\t%s: Failed to read module\n", moduleList[i].name);
      free(irxBuf);
      close(fd);
      goto fail;
    }
    close(fd);

    moduleList[i].irx = irxBuf;
    moduleList[i].size = malloc(sizeof(uint32_t));
    memcpy(moduleList[i].size, &fsize, sizeof(uint32_t));
  }

  return 0;

fail:
  return -1;
}

// Returns 0 if memory card in mc1 is not a formatted memory card
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
    if (LAUNCHER_OPTIONS.mode & MODE_UDPBD)
      logString("\t\tFailed to get IP address from IPCONFIG.DAT\n");
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
