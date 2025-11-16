#include "common.h"
#include "devices.h"
#include "options.h"
#include <debug.h>
#include <kernel.h>
#include <loadfile.h>
#include <sifrpc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Arguments
static char isoArgument[] = "dvd";
static char bsdArgument[] = "bsd";
static char bsdfsArgument[] = "bsdfs";

// Neutrino bsd values
#define BSD_ATA "ata"
#define BSD_MX4SIO "mx4sio"
#define BSD_UDPBD "udpbd"
#define BSD_USB "usb"
#define BSD_ILINK "ilink"
#define BSD_MMCE "mmce"

// Neutrino bsdfs values
#define BSDFS_HDL "hdl"

int launchELF(int argc, char *argv[]);

// Assembles argument lists into argv for loader.elf.
// Expects argv to be initialized with at least (arguments->total) elements.
int assembleArgv(ArgumentList *arguments, char *argv[]) {
  Argument *curArg = arguments->first;
  int argCount = 1; // argv[0] is always neutrino.elf
  int argSize = 0;

  argv[0] = NEUTRINO_ELF_PATH;
  while (curArg != NULL) {
    if (!curArg->isDisabled) {
      argSize = strlen(curArg->arg) + strlen(curArg->value) + 3; // + \0, = and -
      char *value = calloc(sizeof(char), argSize);

      if (!strlen(curArg->value))
        snprintf(value, argSize, "-%s", curArg->arg);
      else
        snprintf(value, argSize, "-%s=%s", curArg->arg, curArg->value);

      argv[argCount] = value;
      argCount++;
    }
    curArg = curArg->next;
  }

  // Free unused memory
  if (argCount != arguments->total)
    argv = realloc(argv, argCount * sizeof(char *));

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
  case MODE_UDPBD:
    bsdValue = BSD_UDPBD;
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
    printf("ERROR: Unsupported mode\n");
    return;
  }

  printf("Updating last launched title\n");
  if (updateLastLaunchedTitle(target->device, target->fullPath)) {
    printf("ERROR: Failed to update last launched title\n");
  }

  // Sync storage device before loading Neutrino
  if (target->device->sync)
    target->device->sync();

  printf("Mounting VMC on MMCE devices\n");
  mmceMountVMC(target->id);

  // Append bsd and ISO path
  appendArgument(arguments, newArgument(bsdArgument, bsdValue));
  appendArgument(arguments, newArgument(isoArgument, target->fullPath));
  // Use quickboot to reduce load times (except for HDL mode because it requires hdlfs module)
  if (target->device->mode != MODE_HDL)
    appendArgument(arguments, newArgument("qb", ""));

  // Assemble argv
  char **argv = malloc(((arguments->total) + 1) * sizeof(char *));
  int argCount = assembleArgv(arguments, argv);

  printf("Launching %s (%s) with arguments:\n", target->name, target->id);
  for (int i = 0; i < argCount; i++) {
    printf("%d: %s\n", i + 1, argv[i]);
  }

  printf("ERROR: failed to load %s: %d\n", NEUTRINO_ELF_PATH, launchELF(argCount, argv));
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
  Exit(-1);
  __builtin_trap();
}
