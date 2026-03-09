#ifndef _DEVICES_HDD_H_
#define _DEVICES_HDD_H_

#include "devices/devices.h"

// Resolves HDD root from argv0 in format hdd0:<partition>:pfs:<path to ELF>.
// Returns raw path to CWD (e.g. hdd0:__common:pfs:/nhddl/) or NULL on parse/mount failure.
// Caller must free the returned string.
char *resolveHDDRoot(char *argv0);

// Returns path to use for file I/O
const char *getNHDDLHDDRoot(void);

// Unmounts pfs2: when root is hdd0. Call before IOP reboot, before booting Neutrino, and on exit.
void cleanupRootMount(void);

#endif
