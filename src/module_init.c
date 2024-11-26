#include "module_init.h"
#include "common.h"
#include <fcntl.h>
#include <iopcontrol.h>
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
  extern unsigned int size_##mod##_irx

#define IRX_LOAD(mod)                                                                                                                                \
  logString("\tloading " #mod "\n");                                                                                                                 \
  if ((ret = SifExecModuleBuffer(mod##_irx, size_##mod##_irx, 0, NULL, &iopret)) < 0)                                                                \
    return ret;                                                                                                                                      \
  if (iopret == 1) {                                                                                                                                 \
    return iopret;                                                                                                                                   \
  }

// Embedded IOP modules required for reading from memory card
IRX_DEFINE(iomanX);
IRX_DEFINE(fileXio);
IRX_DEFINE(sio2man);
IRX_DEFINE(mcman);
IRX_DEFINE(mcserv);
IRX_DEFINE(freepad);

// External module entry
typedef struct ExternalModule {
  char *name;         // Module name (without the path and IRX extension)
  unsigned char *irx; // Pointer to IRX module
  uint32_t size;      // IRX size
  uint32_t argLength; // Argument string length
  char *argStr;       // Argument string
  ModeType mode;      // Used to ignore modules not required for target mode

  struct ExternalModule *next; // Next module in the list
} ExternalModule;

// Loads external modules into memory
ExternalModule *buildExternalModuleList(char *basePath);

// Frees mod and returns pointer to the next module in chain
ExternalModule *freeExternalModule(ExternalModule *mod);

// Function used to initialize module arguments.
// Must set argLength and argStr and return 0 if successful.
// argStr pointer must be unique to ExternalModule
typedef int (*moduleArgFunc)(ExternalModule *);

typedef struct ExternalModuleEntry {
  char *name;                     // Module name
  char *path;                     // Relative path to module
  moduleArgFunc argumentFunction; // Function used to initialize module arguments
  ModeType mode;                  // Used to ignore modules not required for target mode
} ExternalModuleEntry;

// Initializes SMAP arguments
int initSMAPArguments(ExternalModule *mod);

#define MODULE_COUNT(a) sizeof(a) / sizeof(ExternalModuleEntry)
const ExternalModuleEntry external_modules[] = {
    // DEV9
    {"dev9", "modules/dev9_ns.irx", NULL, MODE_ALL},
    // BDM
    {"bdm", "modules/bdm.irx", NULL, MODE_ALL},
    // FAT/exFAT
    {"bdmfs_fatfs", "modules/bdmfs_fatfs.irx", NULL, MODE_ALL},
    // ATA
    {"ata_bd", "modules/ata_bd.irx", NULL, MODE_ATA},
    // USBD
    {"usbd_mini", "modules/usbd_mini.irx", NULL, MODE_USB},
    // USB Mass Storage
    {"usbmass_bd_mini", "modules/usbmass_bd_mini.irx", NULL, MODE_USB},
    // MX4SIO
    {"mx4sio_bd_mini", "modules/mx4sio_bd_mini.irx", NULL, MODE_MX4SIO},
    // SMAP driver. Actually includes small IP stack and UDPTTY
    {"smap_udpbd", "modules/smap_udpbd.irx", &initSMAPArguments, MODE_UDPBD},
    // iLink
    {"iLinkman", "modules/iLinkman.irx", NULL, MODE_ILINK},
    // iLink Mass Storage
    {"IEEE1394_bd_mini", "modules/IEEE1394_bd_mini.irx", NULL, MODE_ILINK},
};

// Initializes IOP modules
int initModules(char *basePath) {
  // Load optional modules into EE memory before resetting IOP
  ExternalModule *modules = buildExternalModuleList(basePath);
  if (modules == NULL) {
    logString("ERROR: Failed to prepare external modules\n");
    return -EIO;
  }

  // Initialize the RPC manager and reset IOP
  SifInitRpc(0);
  while (!SifIopReset("", 0)) {
  };
  while (!SifIopSync()) {
  };

  // Initialize the RPC manager
  SifInitRpc(0);

  int ret, iopret = 0;
  // Apply patches required to load modules from EE RAM
  if ((ret = sbv_patch_enable_lmb()))
    return ret;
  if ((ret = sbv_patch_disable_prefix_check()))
    return ret;

  // Load embedded modules
  IRX_LOAD(iomanX);
  IRX_LOAD(fileXio);
  IRX_LOAD(sio2man);
  IRX_LOAD(mcman);
  IRX_LOAD(mcserv);
  IRX_LOAD(freepad);

  // Load external modules from EE RAM
  while (modules != NULL) {
    if (modules->argStr != NULL)
      logString("\tloading %s with %s\n", modules->name, modules->argStr);
    else
      logString("\tloading %s\n", modules->name);

    ret = SifExecModuleBuffer(modules->irx, modules->size, modules->argLength, modules->argStr, &iopret);
    // Ignore error if module can fail
    if ((modules->mode == MODE_ALL) || (modules->mode == LAUNCHER_OPTIONS.mode)) {
      if (ret < 0)
        return ret;
      if (iopret == 1)
        return iopret;
    }
    modules = freeExternalModule(modules);
  }
  return 0;
}

// Frees mod and returns pointer to the next module in chain
ExternalModule *freeExternalModule(ExternalModule *mod) {
  ExternalModule *m = mod->next;
  free(mod->irx);
  free(mod->argStr);
  return m;
}

// Builds IP address argument for SMAP modules
int initSMAPArguments(ExternalModule *mod) {
  if (LAUNCHER_OPTIONS.udpbdIp[0] == '\0') {
    return -ENOENT;
  }

  char ipArg[19]; // 15 bytes for IP string + 3 bytes for 'ip='
  mod->argLength = 19;
  mod->argStr = calloc(sizeof(char), 19);
  snprintf(mod->argStr, sizeof(ipArg), "ip=%s", LAUNCHER_OPTIONS.udpbdIp);
  return 0;
}

// Loads external modules into memory
ExternalModule *buildExternalModuleList(char *basePath) {
  // Allocate memory for module paths
  int basePathLen = strlen(basePath);
  char pathBuf[PATH_MAX + 1];
  pathBuf[0] = '\0';
  strcpy(pathBuf, basePath);

  ExternalModule *curModule = NULL;
  ExternalModule *firstModule = NULL;
  int fd, fsize, res;
  for (int i = 0; i < MODULE_COUNT(external_modules); i++) {
    // Ignore module if:
    if (external_modules[i].mode != LAUNCHER_OPTIONS.mode && // Target mode doesn't match required mode
        external_modules[i].mode != MODE_ALL &&              // Module is not required
        LAUNCHER_OPTIONS.mode != MODE_ALL) {                 // Target mode is not set to ALL
      continue;
    }

    // End bufferred string at basePath for the next strcat in the loop
    pathBuf[basePathLen] = '\0';

    // Append module path to base path
    strcat(pathBuf, external_modules[i].path);

    // Open module
    if ((fd = open(pathBuf, O_RDONLY)) < 0) {
      logString("%s: Failed to open %s\n", external_modules[i].name, pathBuf);
      if ((external_modules[i].mode == MODE_ALL) || (external_modules[i].mode == LAUNCHER_OPTIONS.mode))
        goto fail;
      continue;
    }
    // Determine file size
    fsize = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    // Allocate memory for the module
    unsigned char *irxBuf = calloc(sizeof(char), fsize);
    if (irxBuf == NULL) {
      logString("\t%s: Failed to allocate memory\n", external_modules[i].name);
      close(fd);
      goto fail;
    }
    // Load module into buffer
    res = read(fd, irxBuf, fsize);
    if (res != fsize) {
      logString("\t%s: Failed to read module\n", external_modules[i].name);
      free(irxBuf);
      close(fd);
      goto fail;
    }
    close(fd);

    // Initialize ExternalModule
    ExternalModule *mod = calloc(sizeof(ExternalModule), 1);
    mod->name = external_modules[i].name;
    mod->irx = irxBuf;
    mod->size = fsize;
    mod->mode = external_modules[i].mode;

    // If module has an arugment function, execute it
    if (external_modules[i].argumentFunction != NULL) {
      res = external_modules[i].argumentFunction(mod);
      if (res) {
        free(irxBuf);
        free(mod);
        // Ignore errors if module can fail
        if ((external_modules[i].mode != MODE_ALL) && (external_modules[i].mode != LAUNCHER_OPTIONS.mode)) {
          logString("\t%s: Failed to initialize arguments, skipping module\n", external_modules[i].name);
          continue;
        } else {
          logString("\t%s: Failed to initialize arguments\n", external_modules[i].name);
          goto fail;
        }
      }
    }

    // Add module to chain
    if (firstModule == NULL)
      firstModule = mod;
    else
      curModule->next = mod;

    curModule = mod;
  }

  return firstModule;

fail:
  // Release memory and return NULL
  while (firstModule != NULL) {
    firstModule = freeExternalModule(firstModule);
  }
  return NULL;
}
