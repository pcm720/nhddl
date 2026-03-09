#ifndef _DEVICES_HDD_H_
#define _DEVICES_HDD_H_

#include "devices/devices.h"

// Returns path to use for file I/O
const char *getNHDDLHDDRoot(void);

// Unmounts pfs2: when root is hdd0. Call before IOP reboot, before booting Neutrino, and on exit.
void cleanupRootMount(void);

#endif
