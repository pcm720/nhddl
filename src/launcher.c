#include "common.h"
#include "devices.h"
#include "options.h"
#include <kernel.h>
#include <sifrpc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Loader ELF variables
extern uint8_t loader_elf[];
extern int size_loader_elf;
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

int LoadELFFromFile(int argc, char *argv[]);

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

  // Change device path to <mountpoint><device index>: since <mountpoint>%d: path will not be preserved after Neutrino resets the IOP
  int devIdx = getDeviceNumberIdx(target->fullPath);
  if (devIdx != -1)
    target->fullPath[devIdx] = target->device->index + '0';

  appendArgument(arguments, newArgument(bsdArgument, bsdValue));
  appendArgument(arguments, newArgument(isoArgument, target->fullPath));

  // Assemble argv
  char **argv = malloc(((arguments->total) + 1) * sizeof(char *));
  int argCount = assembleArgv(arguments, argv);

  printf("Launching %s (%s) with arguments:\n", target->name, target->id);
  for (int i = 0; i < argCount; i++) {
    printf("%d: %s\n", i + 1, argv[i]);
  }

  printf("ERROR: failed to load %s: %d\n", NEUTRINO_ELF_PATH, LoadELFFromFile(argCount, argv));
}

//
// All the following code is modified version of elf.c from PS2SDK with unneeded bits removed
//

typedef struct {
  uint8_t ident[16]; // struct definition for ELF object header
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint32_t entry;
  uint32_t phoff;
  uint32_t shoff;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;
} elf_header_t;

typedef struct {
  uint32_t type; // struct definition for ELF program section header
  uint32_t offset;
  void *vaddr;
  uint32_t paddr;
  uint32_t filesz;
  uint32_t memsz;
  uint32_t flags;
  uint32_t align;
} elf_pheader_t;

// ELF-loading stuff
#define ELF_MAGIC 0x464c457f
#define ELF_PT_LOAD 1

int LoadELFFromFile(int argc, char *argv[]) {
  uint8_t *boot_elf;
  elf_header_t *eh;
  elf_pheader_t *eph;
  void *pdata;
  int i;

  // Wipes memory where the loader is going to be allocated (see loader/linkfile for memory regions)
  for (i = 0x00084000; i < 0x100000; i += 64) {
    asm volatile("\tsq $0, 0(%0) \n"
                 "\tsq $0, 16(%0) \n"
                 "\tsq $0, 32(%0) \n"
                 "\tsq $0, 48(%0) \n" ::"r"(i));
  }

  /* NB: LOADER.ELF is embedded  */
  boot_elf = (uint8_t *)loader_elf;
  eh = (elf_header_t *)boot_elf;
  if (_lw((uint32_t)&eh->ident) != ELF_MAGIC)
    __builtin_trap();

  eph = (elf_pheader_t *)(boot_elf + eh->phoff);

  /* Scan through the ELF's program headers and copy them into RAM, then zero out any non-loaded regions.  */
  for (i = 0; i < eh->phnum; i++) {
    if (eph[i].type != ELF_PT_LOAD)
      continue;

    pdata = (void *)(boot_elf + eph[i].offset);
    memcpy(eph[i].vaddr, pdata, eph[i].filesz);

    if (eph[i].memsz > eph[i].filesz)
      memset((void *)((uint8_t *)(eph[i].vaddr) + eph[i].filesz), 0, eph[i].memsz - eph[i].filesz);
  }

  SifExitRpc();
  FlushCache(0);
  FlushCache(2);

  return ExecPS2((void *)eh->entry, NULL, argc, argv);
}
