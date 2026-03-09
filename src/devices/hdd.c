// APA HDD functions
#include "devices/hdd.h"
#include "config/config.h"
#include "devices/utils.h"
#include "dprintf.h"
#include <dirent.h>
#include <hdd-ioctl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// PFS mount table
typedef struct PFSMountTableEntry {
  char *part;
  char *pfs;
} PFSMountTableEntry;

PFSMountTableEntry pfsMountTable[4] = {
    {NULL, "pfs0:"}, //
    {NULL, "pfs1:"}, //
    {NULL, "pfs2:"}, //
    {NULL, "pfs3:"}, //
};
static char *nhddlPFSRoot = NULL;

// Mounts partition and returns PFS mountpoint.
// Returned string must be freed by the caller.
char *mountPFSPartition(const char *path, int mountFlag) {
  if (strncmp(path, "hdd", 3))
    return NULL;

  // Get partition path
  char temp[100] = {0};
  char *pfs = strstr(path, ":pfs:");
  if (pfs)
    strncpy(temp, path, pfs - path);
  else
    strncpy(temp, path, 100);

  PFSMountTableEntry *entry = NULL;
  for (int i = 0; i < sizeof(pfsMountTable) / sizeof(PFSMountTableEntry); i++) {
    // Check if any of mounted partitions match the target partition
    if (pfsMountTable[i].part) {
      if (!strncmp(pfsMountTable[i].part, temp, strlen(temp)))
        return strdup(pfsMountTable[i].pfs);
      continue;
    }

    // Got free slot
    entry = &pfsMountTable[i];
    break;
  }
  if (!entry || fileXioMount(entry->pfs, temp, mountFlag))
    return NULL;

  DPRINTF("devices/hdd: mounted %s to %s\n", temp, entry->pfs);
  entry->part = strdup(temp);
  return strdup(entry->pfs);
}

// Unmounts partition without checking for NHDDL root
void forceUnmountPFSPartition(const char *pfsMount) {
  PFSMountTableEntry *entry = NULL;
  for (int i = 0; i < sizeof(pfsMountTable) / sizeof(PFSMountTableEntry); i++) {
    if (!strncmp(pfsMountTable[i].pfs, pfsMount, strlen(pfsMountTable[i].pfs))) {
      if (pfsMountTable[i].part) {
        DPRINTF("devices/hdd: unmounting %s (%s)\n", pfsMountTable[i].part, pfsMountTable[i].pfs);
        free(pfsMountTable[i].part);
      }

      pfsMountTable[i].part = NULL;
      fileXioUmount(pfsMountTable[i].pfs);
      return;
    }
  }
}

// Unmounts partition
void unmountPFSPartition(const char *pfsMount) {
  if (nhddlPFSRoot && !strncmp(pfsMount, nhddlPFSRoot, 4)) {
    DPRINTF("devices/hdd: not unmounting NHDDL root partition %s\n", pfsMount);
    return;
  }
  forceUnmountPFSPartition(pfsMount);
}

// Returns path to use for file I/O
const char *getNHDDLHDDRoot(void) {
  // Check if ioPath is already initalized
  if (nhddlPFSRoot)
    return nhddlPFSRoot;

  // Get raw root and get the relative path
  const char *root = getNHDDLRawRoot();
  if (!root)
    return root;

  nhddlPFSRoot = mountPFSPartition((char *)root, FIO_MT_RDWR);
  if (!nhddlPFSRoot)
    return NULL;

  return nhddlPFSRoot;
}

// Checks and returns 0 if the given device contains APA partition table.
int checkAPAHeader(const char *mountpoint) {
  int result = -1;

  uint8_t *pSectorData = (uint8_t *)malloc(512);
  if (pSectorData == NULL) {
    return -ENOMEM;
  }

  hddAtaTransfer_t *args = (hddAtaTransfer_t *)pSectorData;
  args->lba = 0;
  args->size = 1;
  result = fileXioDevctl(mountpoint, HDIOC_READSECTOR, args, sizeof(hddAtaTransfer_t), pSectorData, 512);
  if (result < 0) {
    free(pSectorData);
    return -EIO;
  }

  if (strncmp((const char *)&pSectorData[4], "APA", 3)) {
    result = 1; // Sector doesn't contain APA magic
  }

  free(pSectorData);
  return result;
}

// Unmounts NHDDL root partition. Call before IOP reboot, before booting Neutrino, and on exit.
void cleanupRootMount(void) {
  const char *root = getNHDDLRawRoot();
  if (!root || !nhddlPFSRoot || strncmp(root, "hdd", 3))
    return;

  // "Clear" the path and unmount the partition
  forceUnmountPFSPartition(nhddlPFSRoot);
  free(nhddlPFSRoot);
  nhddlPFSRoot = NULL;
}

// Returns LBA for the given APA partition
uint32_t getHDDPartitionLBA(const char *partitionPath) {
  iox_stat_t stat = {0};
  if (fileXioGetStat(partitionPath, &stat))
    return -1;

  return stat.private_5;
}

// Converts full HDD path into PFS path
char *toPFSPath(const char *hddPath, const char *pfsMountpoint) {
  if (strncmp(hddPath, "hdd", 3) || strncmp(pfsMountpoint, "pfs", 3))
    return NULL;

  // Find :pfs: part
  char *pfs = strstr(hddPath, ":pfs:");
  if (!pfs)
    return NULL;

  // Duplicate the original path from :pfs: and replace :pfs with pfs?
  char *path = strdup(pfs);
  if (!path)
    return NULL;

  memcpy(path, pfsMountpoint, 4);
  return path;
}
