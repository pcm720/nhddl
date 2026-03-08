#include "backends/backends.h"
#include "backends/cache.h"
#include "config/arguments.h"
#include "config/config.h"
#include "devices/utils.h"
#include "dprintf.h"
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

// Neutrino bsdfs values
#define BSDFS_HDL "hdl"

int findNeutrinoELF();
int launchELF(int argc, char *argv[]);

// Assembles argument lists into argv for loader.elf.
// Expects argv to be initialized with at least (arguments->total) elements.
int assembleArgv(ArgumentList *arguments, char **argv[]) {
  Argument *curArg = arguments->first;
  int argCount = 1; // argv[0] is always neutrino.elf
  int argSize = 0;

  *argv[0] = (char *)getNeutrinoPath();
  while (curArg != NULL) {
    if (!curArg->isDisabled) {
      argSize = strlen(curArg->arg) + (curArg->value ? strlen(curArg->value) : 0) + 3; // + \0, = and -
      char *value = calloc(sizeof(char), argSize);

      if (!curArg->value || !strlen(curArg->value))
        snprintf(value, argSize, "-%s", curArg->arg);
      else
        snprintf(value, argSize, "-%s=%s", curArg->arg, curArg->value);

      *argv[argCount] = value;
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
int launchTarget(Target *target, ArgumentList *arguments) {
  if (!findNeutrinoELF())
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
    appendArgument(arguments, newArgument("qb", ""));
    appendArgument(arguments, newArgument(bsdfsArgument, BSDFS_HDL));
  case Device_ATA:
    bsdValue = BSD_ATA;
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
    DPRINTF("ERROR: Unsupported mode\n");
    return -EINVAL;
  }

  DPRINTF("Updating last launched title\n");
  if (updateLastLaunchedTitle(target))
    DPRINTF("ERROR: Failed to update last launched title\n");

  // Cleanup storage devices before loading Neutrino
  cleanupAllBackends();

  DPRINTF("Mounting VMC on MMCE devices\n");
  mmceMountVMC(target->id);

  appendArgument(arguments, newArgument(bsdArgument, bsdValue));
  char fullPathBuf[PATH_MAX];
  if (!getTargetFullPath(target, fullPathBuf, sizeof(fullPathBuf)))
    appendArgument(arguments, newArgument(isoArgument, fullPathBuf));

  // Assemble argv
  char **argv = malloc(((arguments->total) + 1) * sizeof(char *));
  int argCount = assembleArgv(arguments, &argv);

  DPRINTF("Launching %s (%s) with arguments:\n", target->name, target->id);
  for (int i = 0; i < argCount; i++)
    DPRINTF("%d: %s\n", i + 1, argv[i]);

  return launchELF(argCount, argv);
}
