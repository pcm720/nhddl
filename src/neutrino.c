#include "common.h"
#include "devices/devices.h"
#include "devices/init.h"
#include "dprintf.h"
#include "favorites.h"
#include "ftp.h"
#include "options.h"
#include <debug.h>
#include <elf-loader.h>
#include <errno.h>
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Path to Neutrino ELF
char NEUTRINO_ELF_PATH[PATH_MAX + 1];
// Neutrino ELF name relative to CWD
static const char neutrinoELF[] = "neutrino.elf";
// neutrino.elf fallback paths
static char *neutrinoMCFallbackPaths[] = {
    "mcX:/APPS/neutrino/neutrino.elf",
    "mcX:/NEUTRINO/NEUTRINO.ELF",
    "mcX:/NEUTRINO/neutrino.elf",
};
static char neutrinoStorageFallbackPath[] = "/neutrino/neutrino.elf";

// Arguments
static char isoArgument[] = "dvd";
static char bsdArgument[] = "bsd";
static char bsdfsArgument[] = "bsdfs";
static char igrPathArgument[] = "igr-path";
static char igrPowerArgument[] = "igr-power";

// Neutrino bsd values
#define BSD_ATA "ata"
#define BSD_MX4SIO "mx4sio"
#define BSD_UDPFS "udpfs"
#define BSD_USB "usb"
#define BSD_ILINK "ilink"
#define BSD_MMCE "mmce"

// Neutrino bsdfs values
#define BSDFS_HDL "hdl"

int launchELF(int argc, char *argv[]);

/*
 * elf-loader2 deliberately keeps its prepared-image API internal, although
 * the symbols are exported by the library. NHDDL uses the same ABI here to
 * keep one packed copy of itself staged in EE RAM. A front-panel reset can
 * then ExecPS2 that image without making another fileXio/UDPFS request --
 * crucial when the button is being used to recover from a stuck IOP startup.
 */
#define DASHBOARD_ELF_MAX_PROGRAM_HEADERS 32
#define DASHBOARD_ELF_MAX_LOAD_ITEMS (DASHBOARD_ELF_MAX_PROGRAM_HEADERS + 3)
typedef struct {
  void *dest_addr;
  void *src_addr;
  u32 size;
} DashboardELFLoadItem;
typedef struct {
  DashboardELFLoadItem items[DASHBOARD_ELF_MAX_LOAD_ITEMS];
} DashboardELFLoadInfo;
typedef struct {
  int argc;
  char *argv[16];
  char payload[256];
} DashboardELFArgInfo;
typedef struct {
  DashboardELFArgInfo arginfo;
  DashboardELFLoadInfo loaderinfo;
} DashboardELFExecInfo;

extern int elf_loader_exec_elf_prepare_loadinfo(DashboardELFExecInfo *execinfo, const void *buf, size_t buf_size);
extern int elf_loader_exec_elf_prepare_arginfo(DashboardELFExecInfo *execinfo, const char *filename, const char *partition, int argc,
                                               char *argv[]);
extern int elf_loader_exec_elf(DashboardELFExecInfo *execinfo);

static DashboardELFExecInfo dashboardRestartInfo;
static void *dashboardRestartImage = NULL;
static char dashboardRestartPath[PATH_MAX + 1] = {0};
static int dashboardRestartPrepared = 0;

int launchExternalELF(const char *path) {
  if ((path == NULL) || (path[0] == '\0'))
    return -1;

  // LoadELFFromFile gives the target argv[0] automatically. It first stages
  // the ELF, then resets the IOP and executes it through a low-memory loader,
  // so a normal 0x00100000-linked ELF cannot overwrite this launcher.
  return LoadELFFromFile(path, 0, NULL);
}

int prepareDashboardRestart(const char *path) {
  if ((path == NULL) || (path[0] == '\0'))
    return -EINVAL;
  if (dashboardRestartPrepared && !strcmp(path, dashboardRestartPath))
    return 0;

  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return -ENOENT;
  if ((fseek(file, 0, SEEK_END) != 0)) {
    fclose(file);
    return -EIO;
  }
  long imageSize = ftell(file);
  if ((imageSize <= 0) || (fseek(file, 0, SEEK_SET) != 0)) {
    fclose(file);
    return -EIO;
  }

  void *image = malloc((size_t)imageSize);
  if (image == NULL) {
    fclose(file);
    return -ENOMEM;
  }
  if (fread(image, 1, (size_t)imageSize, file) != (size_t)imageSize) {
    fclose(file);
    free(image);
    return -EIO;
  }
  fclose(file);

  DashboardELFExecInfo info;
  memset(&info, 0, sizeof(info));
  int ret = elf_loader_exec_elf_prepare_loadinfo(&info, image, (size_t)imageSize);
  if (ret == 0)
    ret = elf_loader_exec_elf_prepare_arginfo(&info, path, NULL, 0, NULL);
  if (ret < 0) {
    free(image);
    return ret;
  }

  if (dashboardRestartImage != NULL)
    free(dashboardRestartImage);
  dashboardRestartImage = image;
  dashboardRestartInfo = info;
  strlcpy(dashboardRestartPath, path, sizeof(dashboardRestartPath));
  dashboardRestartPrepared = 1;
  DPRINTF("Power reset: staged %ld-byte dashboard image from %s\n", imageSize, path);
  return 0;
}

int restartDashboardNow(void) {
  // Release the shared PS2IP/SMAP owner while its fileXio DEV9 control path
  // still exists. The staged loader resets the IOP next; powering DEV9 down
  // first prevents the next dashboard from inheriting a half-live adapter.
  if (LAUNCHER_OPTIONS.mode & MODE_UDPFS)
    ftpShutdownNetwork();

  if (dashboardRestartPrepared) {
    DPRINTF("Power reset: executing staged dashboard image\n");
    int ret = elf_loader_exec_elf(&dashboardRestartInfo);
    DPRINTF("Power reset: staged loader returned %d\n", ret);
  }

  // Early-startup fallback if staging was not possible. This path still
  // works while fileXio is healthy; the staged path above is IOP-independent.
  // If a caller deliberately replaced the staged image (for example with the
  // known-good startup fallback), keep using that path if the prepared-image
  // execution unexpectedly returns. Do not silently relaunch the failed
  // primary.
  const char *path = (dashboardRestartPrepared && dashboardRestartPath[0] != '\0')
                         ? dashboardRestartPath
                         : ((SELF_ELF_PATH[0] != '\0') ? SELF_ELF_PATH : NULL);
  return (path != NULL) ? launchExternalELF(path) : -ENOENT;
}

// Assembles argument lists into argv for loader.elf.
// Expects argv to be initialized with at least (arguments->total) elements.
int assembleArgv(ArgumentList *arguments, char **argv[]) {
  Argument *curArg = arguments->first;
  int argCount = 1; // argv[0] is always neutrino.elf
  int argSize = 0;

  *argv[0] = NEUTRINO_ELF_PATH;
  while (curArg != NULL) {
    if (!curArg->isDisabled) {
      argSize = strlen(curArg->arg) + (curArg->value ? strlen(curArg->value) : 0) + 3; // + \0, = and -
      char *value = calloc(sizeof(char), argSize);

      if (!curArg->value || !strlen(curArg->value))
        snprintf(value, argSize, "-%s", curArg->arg);
      else
        snprintf(value, argSize, "-%s=%s", curArg->arg, curArg->value);

      (*argv)[argCount] = value;
      argCount++;
    }
    curArg = curArg->next;
  }

  // Free unused memory
  if (argCount != arguments->total)
    *argv = realloc(*argv, argCount * sizeof(char *));

  return argCount;
}

// Launches target, passing arguments to Neutrino.
// Expects arguments to be initialized
void launchTitle(Target *target, ArgumentList *arguments) {
  // Append arguments
  char *bsdValue;
  // Map target device index to Neutrino bsd argument
  switch (target->device->mode) {
  case MODE_ATA:
    bsdValue = BSD_ATA;
    break;
  case MODE_MX4SIO:
    bsdValue = BSD_MX4SIO;
    break;
  case MODE_UDPFS:
    bsdValue = BSD_UDPFS;
    break;
  case MODE_USB:
    bsdValue = BSD_USB;
    break;
  case MODE_ILINK:
    bsdValue = BSD_ILINK;
    break;
  case MODE_MMCE:
    bsdValue = BSD_MMCE;
    break;
  case MODE_HDL:
    bsdValue = BSD_ATA;
    appendArgument(arguments, newArgument(bsdfsArgument, BSDFS_HDL));
    break;
  default:
    DPRINTF("ERROR: Unsupported mode\n");
    return;
  }

  DPRINTF("Updating last launched title\n");
  if (updateLastLaunchedTitle(target->device, target->fullPath)) {
    DPRINTF("ERROR: Failed to update last launched title\n");
  }
  // Record the launch in the recently-played list
  favoritesAddRecent(target->device, target->fullPath);

  // Sync storage device before loading Neutrino
  if (target->device->sync)
    target->device->sync();

  DPRINTF("Mounting VMC on MMCE devices\n");
  mmceMountVMC(target->id);

  // Append bsd and ISO path
  appendArgument(arguments, newArgument(bsdArgument, bsdValue));
  appendArgument(arguments, newArgument(isoArgument, target->fullPath));
  // The resident reset core must not assume a memory-card layout. Pass the
  // ELF path that actually launched this dashboard, plus the explicit choice
  // for front-panel interception. The controller combo stays independent.
  if (SELF_ELF_PATH[0] != '\0')
    appendArgument(arguments, newArgument(igrPathArgument, SELF_ELF_PATH));
  appendArgument(arguments, newArgument(igrPowerArgument,
                                        LAUNCHER_OPTIONS.powerButtonReset ? "1" : "0"));
  // QuickBoot reuses the launcher's current IOP load environment. That was
  // safe when NHDDL and Neutrino both used the same ministack-backed UDPFS
  // modules, but the dashboard now uses PS2IP so UDPFS and ps2ftpd can share
  // SMAP. Neutrino's in-game FHI still uses its own ministack stack, so a
  // UDPFS title must take the normal LE reboot path and start those matching
  // modules before it opens the ISO. Reusing the PS2IP file handle/DEV9 state
  // here black-screens titles such as Medal of Honor: European Assault.
  if ((target->device->mode != MODE_HDL) &&
      (target->device->mode != MODE_UDPFS))
    appendArgument(arguments, newArgument("qb", ""));

  // Assemble argv
  char **argv = malloc(((arguments->total) + 1) * sizeof(char *));
  int argCount = assembleArgv(arguments, &argv);

  DPRINTF("Launching %s (%s) with arguments:\n", target->name, target->id);
  for (int i = 0; i < argCount; i++) {
    DPRINTF("%d: %s\n", i + 1, argv[i]);
  }

  launchELF(argCount, argv);
}

// Attempts to find neutrino.elf at current path or one of fallback paths
int findNeutrinoELF(char *cwdPath) {
  if (cwdPath && cwdPath[0] != '\0') {
    // If path is valid, try it
    strcpy(NEUTRINO_ELF_PATH, cwdPath);
    strcat(NEUTRINO_ELF_PATH, neutrinoELF);
    if (!tryFile(NEUTRINO_ELF_PATH))
      return 0;
  }

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

  // Try MMCE if init type is EXTENDED or FULL
  for (int i = 0; i < 2; i++) {
    sprintf(NEUTRINO_ELF_PATH, "mmce%d:%s", i, neutrinoStorageFallbackPath);
    if (!tryFile(NEUTRINO_ELF_PATH))
      return 0;
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

__attribute__((section("._launch_args"))) // Place launchArgs in the _launch_args memory section
__attribute__((aligned(16)))              // Align the pointer
static void *launchArgs = NULL;           // Used to mark the start of argv copy used to start Neutrino

__attribute__((section("._launch_elf"))) // Place launchELF in the _launch_elf memory section
__attribute__((noreturn))                // Mark as noreturn
int launchELF(int argc, char *argv[]) {
  // Set the stack pointer location to point to the end of the unused kernel region to use as a stack
  asm volatile("move $sp, %0\n" : : "r"(0xffff0) : "memory");

  t_ExecData elfdata = {0};

  // Wipe Neutrino ELF memory regions
  for (int i = 0x1000000; i < GetMemorySize(); i += 64) {
    asm volatile("\tsq $0, 0(%0) \n"
                 "\tsq $0, 16(%0) \n"
                 "\tsq $0, 32(%0) \n"
                 "\tsq $0, 48(%0) \n" ::"r"(i));
  }

  // Writeback data cache before loading ELF.
  FlushCache(WRITEBACK_DCACHE);

  // Load Neutrino ELF into memory
  SifLoadFileInit();
  int ret = SifLoadElf(argv[0], &elfdata);
  SifLoadFileExit();
  if (!(ret == 0 && elfdata.epc != 0)) {
    init_scr();
    scr_clear();
    scr_printf(".\n\n\n\tFailed to load neutrino.elf: %d\n", ret);
    __builtin_trap();
  }

  // The shared PS2IP/ps2ftpd stack owns DEV9 until this point. Neutrino itself
  // is now resident and UDPFS launches deliberately do not use QuickBoot, so
  // it no longer needs the dashboard's UDPFS file handle. Shut DEV9 down while
  // its dev9x/fileXio RPC path is still alive; Neutrino will reboot into its
  // matching ministack load environment and initialize the adapter cleanly.
  // Either half of this handoff on its own is unsafe: DDIOC_OFF plus QuickBoot
  // destroys the inherited backend, while no DDIOC_OFF leaves the PS2IP SMAP
  // hardware state for the replacement driver.
  if (LAUNCHER_OPTIONS.mode & MODE_UDPFS) {
    int shutdownResult = ftpShutdownNetwork();
    if (shutdownResult < 0) {
      init_scr();
      scr_clear();
      scr_printf(".\n\n\n\tFailed to stop dashboard network: %d\n", shutdownResult);
      SleepThread();
    }
  }

  // Copy launch arguments from user memory into kernel memory
  char **largv = (char **)&launchArgs;
  char *argStart = (char *)&launchArgs + (argc * 0x4);
  for (int i = 0; i < argc; i++) {
    strcpy(argStart, argv[i]);
    largv[i] = argStart;
    argStart += strlen(largv[i]) + 1;
  }

  // The rest of the code doesn't use libc functions
  // Wipe NHDDL memory
  for (int i = 0x100000; i < 0x1000000; i += 64) {
    asm volatile("\tsq $0, 0(%0) \n"
                 "\tsq $0, 16(%0) \n"
                 "\tsq $0, 32(%0) \n"
                 "\tsq $0, 48(%0) \n" ::"r"(i));
  }

  FlushCache(WRITEBACK_DCACHE);
  FlushCache(INVALIDATE_ICACHE);
  TerminateLibrary();
  _ExecPS2((void *)elfdata.epc, (void *)elfdata.gp, argc, largv);
  KExit(-1);
  __builtin_trap();
}
