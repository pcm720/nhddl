#ifndef _GUI_ARGS_H_
#define _GUI_ARGS_H_

#include "options.h"

// Compatibility modes definitions
typedef struct CompatiblityModeMap {
  int mode;
  char value;
  char *name;
} CompatiblityModeMap;

#define COMPAT_MODES_ARG "gc"
#define CM_NUM_MODES (sizeof(COMPAT_MODE_MAP) / sizeof(CompatiblityModeMap))
#define CM_IOP_FAST_READS 1 << 0
#define CM_IOP_SYNC_READS 1 << 2
#define CM_EE_UNHOOK_SYSCALLS 1 << 3
#define CM_IOP_EMULATE_DVD_DL 1 << 5
#define CM_IOP_FIX_BUFFER_OVERRUN 1 << 7
static const CompatiblityModeMap COMPAT_MODE_MAP[] = {
    {CM_IOP_FAST_READS, '0', "IOP: Fast reads"},
    {CM_IOP_SYNC_READS, '2', "IOP: Sync reads"},
    {CM_EE_UNHOOK_SYSCALLS, '3', "EE : Unhook syscalls"},
    {CM_IOP_EMULATE_DVD_DL, '5', "IOP: Emulate DVD-DL"},
    {CM_IOP_FIX_BUFFER_OVERRUN, '7', "IOP: Fix game buffer overrun"},
};

// Parses compatibility mode argument value into a bitmask
uint8_t parseCompatModes(char *stringValue);

// Stores compatibility mode from bitmask into argument value and sets isDisabled flag accordingly.
// Target must be at least 6 bytes long, including null terminator
void storeCompatModes(Argument *target, uint8_t modes);

// Inserts a new compat mode arg into the argument list
void insertCompatModeArg(ArgumentList *target, uint8_t modes);

#endif
