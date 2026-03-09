#ifndef _DEVICES_HDD_H_
#define _DEVICES_HDD_H_

#include "devices/devices.h"

// Returns path to use for file I/O
const char *getNHDDLHDDRoot(void);

// Unmounts NHDDL root partition. Call before IOP reboot, before booting Neutrino, and on exit.
void cleanupRootMount(void);

// Checks and returns 0 if the given device contains APA partition table.
int checkAPAHeader(const char *mountpoint);

// Returns LBA for the given APA partition
uint32_t getHDDPartitionLBA(const char *partitionPath);

// Mounts partition and returns PFS mountpoint.
// Returned string must be freed by the caller.
char *mountPFSPartition(const char *partitionPath, int mountFlag);

// Unmounts partition
void unmountPFSPartition(const char *pfsMount);

// Converts full HDD path into PFS path
char *toPFSPath(const char *hddPath, const char *pfsMountpoint);

#endif
