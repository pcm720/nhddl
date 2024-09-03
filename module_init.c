#include <errno.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <ps2sdkapi.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <string.h>

#include "common.h"

// Embedded IOP module externs
extern unsigned char sio2man_irx[] __attribute__((aligned(16)));
extern unsigned int size_sio2man_irx;

extern unsigned char mcman_irx[] __attribute__((aligned(16)));
extern unsigned int size_mcman_irx;

extern unsigned char mcserv_irx[] __attribute__((aligned(16)));
extern unsigned int size_mcserv_irx;

extern unsigned char fileXio_irx[] __attribute__((aligned(16)));
extern unsigned int size_fileXio_irx;

extern unsigned char iomanX_irx[] __attribute__((aligned(16)));
extern unsigned int size_iomanX_irx;

// List of HDD modules to load, in order
// Paths are relative to ELF current working directory
const int MODULE_COUNT = 5;
const char *hddmodules[] = {
    // BDM
    "modules/bdm.irx",
    // Required for getting title ID from ISO
    "modules/isofs.irx",
    // exFAT
    "modules/bdmfs_fatfs.irx",
    // DEV9
    "modules/dev9_ns.irx",
    // ATA
    "modules/ata_bd.irx"};

int initMC() {
  SifIopReset("", 0);
  // Initialize the RPC manager
  SifInitRpc(0);

  int ret;
  // Apply patches required to load modules from EE RAM
  if ((ret = sbv_patch_enable_lmb())) {
    return ret;
  }
  if ((ret = sbv_patch_disable_prefix_check())) {
    return ret;
  }

  // Load embedded modules
  logString("\tLoading iomanX.irx\n");
  if ((ret = SifExecModuleBuffer(&iomanX_irx, size_iomanX_irx, 0, NULL, &ret) < 0) || (ret)) {
    return ret;
  }
  logString("\tLoading fileXio.irx\n");
  if ((ret = SifExecModuleBuffer(&fileXio_irx, size_fileXio_irx, 0, NULL, &ret) < 0) || (ret)) {
    return ret;
  }
  logString("\tLoading sio2man.irx\n");
  if ((ret = SifExecModuleBuffer(&sio2man_irx, size_sio2man_irx, 0, NULL, &ret) < 0) || (ret)) {
    return ret;
  }
  logString("\tLoading mcman.irx\n");
  if ((ret = SifExecModuleBuffer(&mcman_irx, size_mcman_irx, 0, NULL, &ret) < 0) || (ret)) {
    return ret;
  }
  logString("\tLoading mcserv.irx\n");
  if ((ret = SifExecModuleBuffer(&mcserv_irx, size_mcserv_irx, 0, NULL, &ret) < 0) || (ret)) {
    return ret;
  }
  return 0;
}

int initHDD(char *basePath) {
  // Allocate memory for module paths
  char pathBuf[PATH_MAX + 1];
  strcpy(pathBuf, basePath);

  // Load the needed modules
  int ret;
  for (int i = 0; i < MODULE_COUNT; i++) {
    strcat(pathBuf, hddmodules[i]); // append module path to base path
    logString("\tLoading %s\n", pathBuf);
    if ((ret = SifLoadModule(pathBuf, 0, NULL)) < 0) {
      logString("ERROR: Failed to load the module %s\n", pathBuf);
      return ret;
    }
    pathBuf[strlen(basePath)] = '\0'; // end bufferred string at basePath for the next strcat in the loop
  }

  return 0;
}
