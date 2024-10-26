#include <errno.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <ps2sdkapi.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "module_init.h"

// Macros for loading embedded IOP modules
#define IRX_DEFINE(mod)                                                                                                                              \
  extern unsigned char mod##_irx[] __attribute__((aligned(16)));                                                                                     \
  extern unsigned int size_##mod##_irx

#define IRX_LOAD(mod)                                                                                                                                \
  logString("\tloading " #mod "\n");                                                                                                                 \
  if (SifExecModuleBuffer(mod##_irx, size_##mod##_irx, 0, NULL, &iopret) < 0)                                                                        \
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

// Initializes basic modules required for reading from memory card
int init() {
  int ret = 0;

// If DEBUG is defined, skip IOP reinitialization
#ifndef DEBUG
  // Reset IOP
  SifIopReset("", 0);
  // Initialize the RPC manager
  SifInitRpc(0);

  // Apply patches required to load modules from EE RAM
  if ((ret = sbv_patch_enable_lmb()))
    return ret;
  if ((ret = sbv_patch_disable_prefix_check()))
    return ret;
#endif

  // Load modules
  int iopret = 0;
  IRX_LOAD(iomanX);
  IRX_LOAD(fileXio);
  IRX_LOAD(sio2man);
  IRX_LOAD(mcman);
  IRX_LOAD(mcserv);
  IRX_LOAD(freepad);

  return 0;
}

// Loads modules from basePath
int initExtraModules(char *basePath, int numModules, const char *modules[]) {
  // Allocate memory for module paths
  int basePathLen = strlen(basePath);
  char pathBuf[PATH_MAX + 1];
  pathBuf[0] = '\0';
  strcpy(pathBuf, basePath);

  // Load modules
  int ret, iopret;
  for (int i = 0; i < numModules; i++) {
    strcat(pathBuf, modules[i]); // Append module path to base path
    logString("\tloading %s\n", pathBuf);
    ret = SifLoadStartModule(pathBuf, 0, NULL, &iopret);
    if (ret < 0) {
      return ret;
    }
    if (iopret == 1) {
      return iopret;
    }
    pathBuf[basePathLen] = '\0'; // End bufferred string at basePath for the next strcat in the loop
  }

  return 0;
}

#define MODULE_COUNT(a) sizeof(a) / sizeof(char *)
// Base BDM modules
const char *bdm_base_modules[] = {
    // BDM
    "modules/bdm.irx",
    // Required for getting title ID from ISO
    "modules/isofs.irx",
    // FAT/exFAT
    "modules/bdmfs_fatfs.irx",
};

// ATA modules
const char *ata_modules[] = {
    // DEV9
    "modules/dev9_ns.irx",
    // ATA
    "modules/ata_bd.irx",
};

// MX4SIO modules
const char *mx4sio_modules[] = {
    "modules/mx4sio_bd_mini.irx",
};

// UDPBD modules
const char *udpbd_modules[] = {
    // DEV9
    "modules/dev9_ns.irx",
    // SMAP driver.
    // Treated as a special case because of the IP address argument
    "modules/smap_udpbd.irx",
};

// USB modules
const char *usb_modules[] = {
    // USBD
    "modules/usbd_mini.irx",
    // USB Mass Storage
    "modules/usbmass_bd_mini.irx",
};

int initATA(char *basePath) {
  int res = 0;
  if ((res = initExtraModules(basePath, MODULE_COUNT(bdm_base_modules), bdm_base_modules))) {
    return res;
  }
  return initExtraModules(basePath, MODULE_COUNT(ata_modules), ata_modules);
}

int initMX4SIO(char *basePath) {
  int res = 0;
  if ((res = initExtraModules(basePath, MODULE_COUNT(bdm_base_modules), bdm_base_modules))) {
    return res;
  }
  return initExtraModules(basePath, MODULE_COUNT(mx4sio_modules), mx4sio_modules);
}

int initUDPBD(char *basePath, char *hostIPAddr) {
  if (strlen(hostIPAddr) == 0) {
    logString("ERROR: invalid IP address length\n");
    return -EINVAL;
  }

  int ret = 0;
  if ((ret = initExtraModules(basePath, MODULE_COUNT(bdm_base_modules), bdm_base_modules))) {
    return ret;
  }

  // Treating last module as a special case because it needs an argument to work
  if ((ret = initExtraModules(basePath, MODULE_COUNT(udpbd_modules) - 1, udpbd_modules))) {
    return ret;
  }

  // Allocate memory for module path and argument
  int iopret;
  char pathBuf[PATH_MAX + 1];
  pathBuf[0] = '\0';
  strcpy(pathBuf, basePath);
  char ipArg[19]; // 15 bytes for IP string + 3 bytes for 'ip='
  snprintf(ipArg, sizeof(ipArg), "ip=%s", hostIPAddr);

  // Append module path to base path
  strcat(pathBuf, udpbd_modules[MODULE_COUNT(udpbd_modules) - 1]);
  logString("\tloading %s\n\twith %s\n", pathBuf, ipArg);
  ret = SifLoadStartModule(pathBuf, sizeof(ipArg), ipArg, &iopret);
  if (ret < 0) {
    return ret;
  }
  if (iopret == 1) {
    return iopret;
  }
  return 0;
}

int initUSB(char *basePath) {
  int res = 0;
  if ((res = initExtraModules(basePath, MODULE_COUNT(bdm_base_modules), bdm_base_modules))) {
    return res;
  }
  return initExtraModules(basePath, MODULE_COUNT(usb_modules), usb_modules);
}

// Initializes BDM modules depending on launcher mode
int initBDM(char *basePath) {
  switch (LAUNCHER_OPTIONS.mode) {
  case MODE_MX4SIO:
    return initMX4SIO(ELF_BASE_PATH);
  case MODE_UDPBD:
    return initUDPBD(ELF_BASE_PATH, LAUNCHER_OPTIONS.udpbdIp);
  case MODE_USB:
    return initUSB(ELF_BASE_PATH);
  default:
    return initATA(ELF_BASE_PATH);
  }
}
