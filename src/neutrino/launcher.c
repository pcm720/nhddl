#include "dprintf.h"
#include <kernel.h>
#include <loadfile.h>
#include <string.h>

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
  if (ret || !elfdata.epc) {
    displayError("Failed to load neutrino.elf: %d\n", ret);
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
  KExit(-1);
  __builtin_trap();
}
