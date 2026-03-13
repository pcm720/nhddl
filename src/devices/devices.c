#include "devices/devices.h"
#include "config/config.h"
#include "devices/hdd.h"
#include "devices/pad.h"
#include "devices/utils.h"
#include "dprintf.h"
#include <ctype.h>
#include <debug.h>
#include <fcntl.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
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
#define INT_MODULE(mod, device, argFunc, conflicting) {#mod, mod##_irx, &size_##mod##_irx, 0, NULL, argFunc, device, conflicting}

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
IRX_DEFINE(ps2hdd_osd);
IRX_DEFINE(ps2fs);
#ifdef ENABLE_PRINTF
IRX_DEFINE(ppctty);
#endif

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
  DeviceType type;                // Device type
  DeviceType conflictingDevices;  // Device types that conflict with this module (e.g. Device_MMCE for mx4sio)
} ModuleListEntry;

// Used to keep track of loaded devices and modules
uint32_t loadedModules = -1;
uint32_t loadedDevices = 0;

// Initializes SMAP arguments
char *initSMAPArguments(uint32_t *argLength);
// Initializes PS2HDD arguments
char *initPS2HDDArguments(uint32_t *argLength);
// Initializes PS2FS arguments
char *initPS2FSArguments(uint32_t *argLength);

// List of modules to load
static ModuleListEntry moduleList[] = {
//
// Base modules
//
#ifdef ENABLE_PRINTF
    INT_MODULE(ppctty, Device_Basic, NULL, Device_None),
#endif
    INT_MODULE(iomanX, Device_Basic, NULL, Device_None),
    INT_MODULE(fileXio, Device_Basic, NULL, Device_None),
    INT_MODULE(sio2man, Device_Basic, NULL, Device_None),
    INT_MODULE(mcman, Device_Basic, NULL, Device_None),
    INT_MODULE(mcserv, Device_Basic, NULL, Device_None),
    INT_MODULE(freepad, Device_Basic, NULL, Device_None),
    INT_MODULE(mmceman, Device_MMCE, NULL, Device_MX4SIO), // MMCE driver
    //
    // Backend modules
    //
    // DEV9
    INT_MODULE(ps2dev9, Device_ATA | Device_HDD | Device_UDPFS | Device_iLink, NULL, Device_None),
    // BDM
    INT_MODULE(bdm, Device_ATA | Device_HDD | Device_USB | Device_MX4SIO | Device_iLink, NULL, Device_None),
    // FAT/exFAT
    INT_MODULE(bdmfs_fatfs, Device_ATA | Device_HDD | Device_USB | Device_MX4SIO | Device_iLink, NULL, Device_None),
    // UDPFS
    INT_MODULE(smap, Device_UDPFS, NULL, Device_None),
    INT_MODULE(ministack, Device_UDPFS, &initSMAPArguments, Device_None),
    INT_MODULE(udpfs_ioman, Device_UDPFS, NULL, Device_None),
    // exFAT on internal HDD
    INT_MODULE(ata_bd, Device_ATA | Device_HDD, NULL, Device_None),
    // USBD
    INT_MODULE(usbd_mini, Device_USB, NULL, Device_None),
    // USB Mass Storage
    INT_MODULE(usbmass_bd_mini, Device_USB, NULL, Device_None),
    // MX4SIO
    INT_MODULE(mx4sio_bd_mini, Device_MX4SIO, NULL, Device_MMCE),
    // iLink
    INT_MODULE(iLinkman, Device_iLink, NULL, Device_None),
    // iLink Mass Storage
    INT_MODULE(IEEE1394_bd_mini, Device_iLink, NULL, Device_None),
    // PS2HDD driver
    INT_MODULE(ps2hdd_osd, Device_HDD, &initPS2HDDArguments, Device_None),
    // PFS driver
    INT_MODULE(ps2fs, Device_HDD, &initPS2FSArguments, Device_None),
};
#define MODULE_COUNT sizeof(moduleList) / sizeof(ModuleListEntry)

DeviceType getConflictingDeviceTypes(DeviceType type) {
  DeviceType result = Device_None;
  for (int i = 0; i < MODULE_COUNT; i++) {
    if (moduleList[i].type & type)
      result |= moduleList[i].conflictingDevices;
  }
  return result;
}

int isDeviceLoaded(DeviceType type) { return (loadedDevices & type) != 0; }

// Loads module, executing argument function if it's present
int loadModule(ModuleListEntry *mod);

// Reboots IOP and initializes basic devices
int rebootIOP() {
  DPRINTF("devices: rebooting IOP\n");
  if (loadedModules != -1)
    cleanupRootMount();

  fileXioExit();
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

  loadedModules = 0;
  loadedDevices = 0;
  int res = loadDeviceModules(Device_Basic);
  if (res)
    return res;
  // Initialize pad library
  initPad();

  // Ensure root device is always available
  const char *root = getNHDDLRawRoot();
  if (root) {
    DeviceType rootType = guessDeviceType(root);
    if (rootType != Device_None && rootType != Device_Basic)
      loadDeviceModules(rootType);
  }
  return 0;
}

// Loads device modules
int loadDeviceModules(DeviceType dtype) {
  if (loadedModules == -1)
    rebootIOP();

  if (loadedDevices & dtype)
    return 0;

  uint32_t targetDevice = dtype;
  for (int i = 0; i < MODULE_COUNT; i++) {
    if (!(moduleList[i].type & dtype))
      continue;
    if (moduleList[i].conflictingDevices & loadedDevices) {
      // Requested type conflicts with already-loaded devices; reboot and reload non-conflicting
      targetDevice |= (loadedDevices & ~getConflictingDeviceTypes(dtype));
      rebootIOP();
      break;
    }
  }

  for (int i = 0; i < MODULE_COUNT; i++) {
    if (loadedModules & (1 << i))
      continue; // Ignore already loaded modules

    if ((moduleList[i].irx != NULL) && (moduleList[i].size != NULL) && (moduleList[i].type & targetDevice)) {
      int ret = loadModule(&moduleList[i]);
      if (ret) {
        if (!strcmp(moduleList[i].name, "ppctty")) // ppctty will fail on non-PPC consoles
          continue;
        DPRINTF("devices error: failed to load %s: %d\n", moduleList[i].name, ret);
        return ret;
      }
      loadedModules |= (1 << i);

      // Introduce delay to prevent ps2hdd module from hanging
      if (!strcmp(moduleList[i].name, "ata_bd"))
        sleep(1);

      // Explicitly init fileXio
      if (!strcmp(moduleList[i].name, "fileXio"))
        fileXioInit();
    }
    // Clean up arguments
    if (moduleList[i].argStr != NULL)
      free(moduleList[i].argStr);
  }
  loadedDevices |= targetDevice;
  return 0;
}

// Loads module, executing argument function if it's present
int loadModule(ModuleListEntry *mod) {
  int ret, iopret = 0;

  DPRINTF("devices: loading %s\n", mod->name);

  // If module has an arugment function, execute it
  if (mod->argumentFunction != NULL) {
    mod->argStr = mod->argumentFunction(&mod->argLength);
    if (mod->argStr == NULL) {
      // Ignore errors if module can fail
      ret = -EINVAL;
      return ret;
    }
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

  if ((ipconfigFd < 0) || (count < sizeof(ipAddr) - 1))
    return -ENOENT;

  count = 0; // Reuse count as line index
  // In case IP address is shorter than 15 chars
  while (!isspace((unsigned char)ipAddr[count]))
    // Advance index until we read a whitespace character
    count++;

  setIPAddress(ipAddr);
  return strlen(getIPAddress());
}

// Builds IP address argument for SMAP modules
char *initSMAPArguments(uint32_t *argLength) {
  // If ip_addr was not set, try to get IP from IPCONFIG.DAT
  if ((getIPAddress()[0] == '\0') && (parseIPConfig() <= 0))
    return NULL;

  char ipArg[19]; // 15 bytes for IP string + 3 bytes for 'ip='
  *argLength = 19;
  char *argStr = calloc(sizeof(char), 19);
  snprintf(argStr, sizeof(ipArg), "ip=%s", getIPAddress());
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

// up to 4 mountpoints, up to 10 descriptors, 40 buffers
char ps2fsArguments[] = "-m\0"
                        "4\0"
                        "-o"
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
