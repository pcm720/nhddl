#include <errno.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <ps2sdkapi.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <string.h>

#include "common.h"
#include "module_init.h"

// Embedded IOP modules required for reading from memory card
IRX_DEFINE(iomanX);
IRX_DEFINE(fileXio);
IRX_DEFINE(sio2man);
IRX_DEFINE(mcman);
IRX_DEFINE(mcserv);
IRX_DEFINE(freepad);

// Initializes basic modules required for reading from memory card
int init() {
  // Reset IOP
  SifIopReset("", 0);
  // Initialize the RPC manager
  SifInitRpc(0);

  int ret;
  // Apply patches required to load modules from EE RAM
  if ((ret = sbv_patch_enable_lmb()))
    return ret;
  if ((ret = sbv_patch_disable_prefix_check()))
    return ret;

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

// List of HDD modules to load from storage, in order
// Paths are relative to ELF current working directory
#ifndef MX4SIO
const int HDD_MODULE_COUNT = 5;
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
    "modules/ata_bd.irx"
};
#else
const int HDD_MODULE_COUNT = 4;
const char *hddmodules[] = {
    // BDM
    "modules/bdm.irx",
    // Required for getting title ID from ISO
    "modules/isofs.irx",
    // exFAT
    "modules/bdmfs_fatfs.irx",
    // MX4SIO
    "modules/mx4sio_bd_mini.irx",
};
#endif

// Loads HDD modules from basePath
int initBDM(char *basePath) {
  // Allocate memory for module paths
  char pathBuf[PATH_MAX + 1];
  strcpy(pathBuf, basePath);

  // Load the needed modules
  int ret;
  for (int i = 0; i < HDD_MODULE_COUNT; i++) {
    strcat(pathBuf, hddmodules[i]); // append module path to base path
    logString("\tloading %s\n", pathBuf);
    if ((ret = SifLoadModule(pathBuf, 0, NULL)) < 0) {
      logString("ERROR: Failed to load the module %s\n", pathBuf);
      return ret;
    }
    pathBuf[strlen(basePath)] = '\0'; // end bufferred string at basePath for the next strcat in the loop
  }

  return 0;
}
