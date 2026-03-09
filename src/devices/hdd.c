// HDD root resolution and pfs2: mount for NHDDL root (devices/hdd.c)
#include "devices/hdd.h"
#include "config/config.h"
#include "devices/utils.h"
#include "dprintf.h"
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#define NHDDL_ROOT_MOUNTPOINT "pfs2:"
static char pfsPath[PATH_MAX] = {0};

// Mount the partition to pfs2:
static int mountRootPartition(char *path) {
  char *pfs = strstr(path, ":pfs:");
  if (!pfs)
    return -1;

  // Temporarily truncate path at ":pfs:" to get partition path
  *pfs = '\0';
  // Mount the partition and restore the path
  int ret = fileXioMount(NHDDL_ROOT_MOUNTPOINT, path, FIO_MT_RDWR);
  if (ret)
    DPRINTF("devices: failed to mount %s to %s: %d\n", path, NHDDL_ROOT_MOUNTPOINT, ret);

  *pfs = ':';
  return ret;
}

// Returns path to use for file I/O
const char *getNHDDLHDDRoot(void) {
  // Check if ioPath is already initalized
  if (pfsPath[0] != '\0')
    return pfsPath;

  // Get raw root and get the relative path
  const char *root = getNHDDLRawRoot();
  if (!root)
    return root;
  const char *pfs = strstr(root, ":pfs:");
  if (!pfs)
    return root;

  // Build the path
  strcpy(pfsPath, NHDDL_ROOT_MOUNTPOINT);
  strcat(pfsPath, pfs + 5);

  // Make sure the root is mounted
  DIR *d = opendir(NHDDL_ROOT_MOUNTPOINT);
  if (d) {
    closedir(d);
    return pfsPath;
  }
  mountRootPartition((char *)root);

  return pfsPath;
}

// Unmounts pfs2: when root is hdd0. Call before IOP reboot, before booting Neutrino, and on exit.
void cleanupRootMount(void) {
  const char *root = getNHDDLRawRoot();
  if (!root || strncmp(root, "hdd", 3))
    return;

  // "Clear" the path and unmount the partition
  pfsPath[0] = '\0';
  fileXioUmount(NHDDL_ROOT_MOUNTPOINT);
}
