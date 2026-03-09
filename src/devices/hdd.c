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
  int ret = (fileXioMount(NHDDL_ROOT_MOUNTPOINT, path, FIO_MT_RDWR) != 0) ? -1 : 0;
  *pfs = ':';

  if (ret != 0)
    DPRINTF("devices: failed to mount %s to %s\n", path, NHDDL_ROOT_MOUNTPOINT);
  return ret;
}

// Resolves HDD root from argv0 in format hdd0:<partition>:pfs:<path to ELF>.
// Returns raw path to CWD (e.g. hdd0:__common:pfs:/nhddl/) or NULL on parse/mount failure.
// Caller must free the returned string.
char *resolveHDDRoot(char *argv0) {
  // Make sure the path is valid
  char *pfs = strstr(argv0, ":pfs:");
  if (!pfs)
    return NULL;

  // Get the last slash location
  char *lastSlash = strrchr(pfs, '/');
  if (!lastSlash)
    return NULL;

  // Temporarily truncate the path at the last slash
  char saved = *(++lastSlash);
  *lastSlash = '\0';

  // Duplicate the path and restore the last slash
  char *rawPath = strdup(argv0);
  *lastSlash = saved;
  return rawPath;
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
  strcat(pfsPath, "/");

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
  if (!root || strncmp(root, "hdd0", 4))
    return;

  // "Clear" the path and unmount the partition
  pfsPath[0] = '\0';
  fileXioUmount(NHDDL_ROOT_MOUNTPOINT);
}
