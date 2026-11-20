#include "backends/backends.h"
#include "backends/cache.h"
#include "config/arguments.h"
#include "config/common.h"
#include "config/config.h"
#include "devices/hdd.h"
#include "devices/utils.h"
#include "dprintf.h"
#include "neutrino/neutrino_arg_defs.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Arguments
static char isoArgument[] = "dvd";
static char bsdArgument[] = "bsd";
static char bsdfsArgument[] = "bsdfs";

// Neutrino bsd values
#define BSD_ATA "ata"
#define BSD_MX4SIO "mx4sio"
#define BSD_UDPFS "udpfs"
#define BSD_USB "usb"
#define BSD_ILINK "ilink"
#define BSD_MMCE "mmce"
#define BSD_ATA_NET "ata-net"

// Neutrino bsdfs values
#define BSDFS_HDL "hdl"

int findNeutrinoELF();
int launchELF(int argc, char *argv[]);

// Assembles argument lists into argv for loader.elf.
// Expects argv to be initialized with at least (arguments->total) elements.
// Known path args with empty values are skipped
int assembleArgv(ArgumentList *arguments, char **argv[]) {
  Argument *curArg = arguments->first;
  int argCount = 1; // argv[0] is always neutrino.elf
  int argSize = 0;

  *argv[0] = (char *)getNeutrinoPath();
  while (curArg != NULL) {
    if (!curArg->isDisabled) {
      int emptyVal = !curArg->value || !curArg->value[0];
      if (neutrinoIsPathArg(curArg->arg) && emptyVal) {
        curArg = curArg->next;
        continue;
      }
      argSize = strlen(curArg->arg) + (curArg->value ? strlen(curArg->value) : 0) + 3; // + \0, = and -
      char *value = calloc(sizeof(char), argSize);

      if (emptyVal)
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

// Join device mount onto known path args with relative values
static void resolvePathArgsForLaunch(ArgumentList *arguments, struct BackendDevice *dev) {
  if (!arguments || !dev)
    return;
  struct BackendDevice *pathDev = (dev->metadev) ? dev->metadev : dev;
  const char *mount = pathDev && pathDev->mountpoint ? pathDev->mountpoint : NULL;
  if (!mount || !mount[0])
    return;

  for (Argument *a = arguments->first; a; a = a->next) {
    if (!neutrinoIsPathArg(a->arg) || !a->value || !a->value[0] || strchr(a->value, ':'))
      continue;
    char full[PATH_MAX];
    joinDevicePath(full, sizeof(full), mount, a->value);
    if (!full[0])
      continue;
    free(a->value);
    a->value = strdup(full);
  }
}

// Launches target, passing arguments to Neutrino.
// Expects arguments to be initialized
int launchTarget(Target *target, ArgumentList *arguments) {
  if (findNeutrinoELF())
    return -ENOENT;

  // Append arguments
  char *bsdValue;
  struct BackendDevice *dev = target->device;
  switch (dev->type) {
  case Device_MMCE:
    bsdValue = BSD_MMCE;
    break;
  case Device_HDD:
    // Disable quickboot for HDL, BSD is set by Device_ATA case
    appendArgument(arguments, newArgument(bsdfsArgument, BSDFS_HDL));
  case Device_ATA:
    bsdValue = ((target->flags & TitleFlag_FakeDEV9) || getFakeDEV9()) ? BSD_ATA_NET : BSD_ATA;
    break;
  case Device_MX4SIO:
    bsdValue = BSD_MX4SIO;
    break;
  case Device_UDPFS:
    bsdValue = BSD_UDPFS;
    break;
  case Device_USB:
    bsdValue = BSD_USB;
    break;
  case Device_iLink:
    bsdValue = BSD_ILINK;
    break;
  default:
    DPRINTF("neutrino: error: unsupported device\n");
    return -EINVAL;
  }

  if (dev->type != Device_HDD) // Disable quickboot for HDL
    appendArgument(arguments, newArgument("qb", ""));

  DPRINTF("neutrino: updating last launched title\n");
  if (updateLastLaunchedTitle(target))
    DPRINTF("neutrino: error: failed to update last launched title\n");

  // Cleanup storage devices before loading Neutrino
  cleanupAllBackends();
  if (!strncmp(getNHDDLRawRoot(), "hdd", 3) && strncmp(getNHDDLRoot(), getNeutrinoPath(), 4))
    cleanupRootMount(); // Do not unmount NHDDL root if Neutrino is on CWD

  DPRINTF("neutrino: mounting VMC on MMCE devices\n");
  mmceMountVMC(target->id);

  appendArgument(arguments, newArgument(bsdArgument, bsdValue));
  char fullPathBuf[PATH_MAX];
  if (!getTargetFullPath(target, fullPathBuf, sizeof(fullPathBuf)))
    appendArgument(arguments, newArgument(isoArgument, fullPathBuf));

  resolvePathArgsForLaunch(arguments, dev);

  // Assemble argv
  DPRINTF("neutrino: assembling argv\n");
  char **argv = malloc(((arguments->total) + 1) * sizeof(char *));
  int argCount = assembleArgv(arguments, &argv);

  DPRINTF("neutrino: launching %s (%s) with arguments:\n", target->name, target->id);
  for (int i = 0; i < argCount; i++)
    DPRINTF("%d: %s\n", i + 1, argv[i]);

  return launchELF(argCount, argv);
}
