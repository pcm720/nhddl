/*
# _____     ___ ____     ___ ____
#  ____|   |    ____|   |        | |____|
# |     ___|   |____ ___|    ____| |    \    PS2DEV Open Source Project.
#-----------------------------------------------------------------------
# (c) 2020 Francisco Javier Trujillo Mata <fjtrujy@gmail.com>
# Licenced under Academic Free License version 2.0
# Review ps2sdk README & LICENSE files for further details.
#
# Modified to not reset IOP for use with NHDDL
*/

#include <errno.h>
#include <iopcontrol.h>
#include <kernel.h>
#include <loadfile.h>
#include <ps2sdkapi.h>
#include <sifrpc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//--------------------------------------------------------------
// Redefinition of init/deinit libc:
//--------------------------------------------------------------
// DON'T REMOVE is for reducing binary size.
// These funtios are defined as weak in /libc/src/init.c
//--------------------------------------------------------------
void _libcglue_init() {}
void _libcglue_deinit() {}
void _libcglue_args_parse(int argc, char **argv) {}

DISABLE_PATCHED_FUNCTIONS();
DISABLE_EXTRA_TIMERS_FUNCTIONS();
PS2_DISABLE_AUTOSTART_PTHREAD();

//--------------------------------------------------------------
// Start of function code:
//--------------------------------------------------------------
// Clear user memory
// PS2Link (C) 2003 Tord Lindstrom (pukko@home.se)
//         (C) 2003 adresd (adresd_ps2dev@yahoo.com)
//--------------------------------------------------------------
static void wipeUserMem(void) {
  int i;
  for (i = 0x100000; i < GetMemorySize(); i += 64) {
    asm volatile("\tsq $0, 0(%0) \n"
                 "\tsq $0, 16(%0) \n"
                 "\tsq $0, 32(%0) \n"
                 "\tsq $0, 48(%0) \n" ::"r"(i));
  }
}

int main(int argc, char *argv[]) {
  static t_ExecData elfdata;
  int ret, i;

  elfdata.epc = 0;

  // arg[0] partition if exists, otherwise is ""
  // arg[1]=path to ELF
  if (argc < 1) {
    return -EINVAL;
  }

  char *new_argv[argc - 1];
  int fullPath_length = 1 + strlen(argv[0]);
  char fullPath[fullPath_length];
  // strcpy(fullPath, argv[0]);
  strcpy(fullPath, argv[0]);
  // final new_argv[0] is partition + path to elf
  new_argv[0] = fullPath;
  for (i = 2; i < argc; i++) {
    new_argv[i - 1] = argv[i];
  }

  // Initialize
  SifInitRpc(0);
  wipeUserMem();

  // Writeback data cache before loading ELF.
  FlushCache(0);
  SifLoadFileInit();
  ret = SifLoadElf(argv[0], &elfdata);
  SifLoadFileExit();
  if (ret == 0 && elfdata.epc != 0) {
    FlushCache(0);
    FlushCache(2);
    return ExecPS2((void *)elfdata.epc, (void *)elfdata.gp, argc - 1, new_argv);
  } else {
    SifExitRpc();
    return -ENOENT;
  }
}