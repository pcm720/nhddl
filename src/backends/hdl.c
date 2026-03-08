// Implements support for APA-formatted HDD with HDL partitions
#include "backends/backends.h"
#include "backends/cache.h"
#include "common.h"
#include "config/config.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "ui/ui.h"
#include <hdd-ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Used to access APA partitions and read raw sectors
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

#define OPL_CONF_PARTITION_ARG "hdd_partition"

// Checks and returns 0 if the given device contains APA partition table.
static int checkAPAHeader(const char *mountpoint) {
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

// Parses OPL config file for partition name from __common/OPL/conf_hdd.cfg. Returns NULL if invalid or missing.
// deviceMountpoint is the HDL device (e.g. "hdd0:"). pfsMount is the PFS mount to use (e.g. "pfs0:").
static char *readOPLConfig(const char *deviceMountpoint, const char *pfsMount) {
  char commonPath[32];
  snprintf(commonPath, sizeof(commonPath), "%s__common", deviceMountpoint);
  if (fileXioMount(pfsMount, commonPath, FIO_MT_RDONLY))
    return NULL;

  char buf[PATH_MAX];
  buf[0] = '\0';

  char cfgPath[32];
  snprintf(cfgPath, sizeof(cfgPath), "%s/OPL/conf_hdd.cfg", pfsMount);
  FILE *fd = fopen(cfgPath, "rb");
  if (!fd) {
    fileXioUmount(pfsMount);
    return NULL;
  }

  while (fgets(buf, sizeof(buf), fd) != NULL) {
    if (!strncmp(buf, OPL_CONF_PARTITION_ARG, sizeof(OPL_CONF_PARTITION_ARG) - 1))
      break;
    buf[0] = '\0';
  }
  fclose(fd);
  fileXioUmount(pfsMount);

  if (buf[0] == '\0')
    return NULL;

  char *val = strchr(buf, '=');
  if (!val)
    return NULL;
  val++;

  char *newline = strchr(val, '\r');
  if (newline)
    *newline = '\0';
  else if ((newline = strchr(val, '\n')) != NULL)
    *newline = '\0';

  size_t prefixLen = strlen(deviceMountpoint);
  char *partitionName = calloc(prefixLen + strlen(val) + 1, sizeof(char));
  if (!partitionName)
    return NULL;
  strcpy(partitionName, deviceMountpoint);
  strcat(partitionName, val);
  return partitionName;
}

// Creates BackendDevice for metadata device. deviceMountpoint is the HDL device (e.g. "hdd0:"). pfsMount is the PFS base (e.g. "pfs0:").
static struct BackendDevice *createMetadataEntry(const char *deviceMountpoint, char *partitionPath, const char *pfsMount) {
  struct BackendDevice *dev = malloc(sizeof(struct BackendDevice));
  dev->scan = NULL;
  dev->sync = NULL;
  dev->cleanup = NULL;
  dev->metadev = NULL;
  dev->type = Device_HDD;
  dev->index = 0;
  dev->titles = NULL;

  char commonPath[32];
  snprintf(commonPath, sizeof(commonPath), "%s__common", deviceMountpoint);
  if (!strcmp(partitionPath, commonPath)) {
    dev->mountpoint = malloc(strlen(pfsMount) + 5);
    if (dev->mountpoint) {
      strcpy(dev->mountpoint, pfsMount);
      strcat(dev->mountpoint, "/OPL");
    } else {
      free(dev);
      return NULL;
    }
  } else
    dev->mountpoint = strdup(pfsMount);

  if (dev->mountpoint)
    DPRINTF("Using %s for HDL metadata\n", dev->mountpoint);
  return dev;
}

// Mounts PFS partition with OPL metadata for the given HDL device. pfsMount is the PFS mount to use (e.g. "pfs0:"). Returns BackendDevice or NULL.
static struct BackendDevice *mountPFS(const char *deviceMountpoint, const char *pfsMount) {
  char partitionBuf[32];

  char *oplPartition = readOPLConfig(deviceMountpoint, pfsMount);
  if (oplPartition) {
    if (fileXioMount(pfsMount, oplPartition, FIO_MT_RDWR))
      DPRINTF("WARN: failed to mount %s, will try fallbacks\n", oplPartition);
    else {
      DPRINTF("Mounted %s as %s\n", oplPartition, pfsMount);
      struct BackendDevice *dev = createMetadataEntry(deviceMountpoint, oplPartition, pfsMount);
      free(oplPartition);
      return dev;
    }
    free(oplPartition);
  }

  snprintf(partitionBuf, sizeof(partitionBuf), "%s+OPL", deviceMountpoint);
  if (!fileXioMount(pfsMount, partitionBuf, FIO_MT_RDWR)) {
    DPRINTF("Mounted %s as %s\n", partitionBuf, pfsMount);
    return createMetadataEntry(deviceMountpoint, partitionBuf, pfsMount);
  }
  snprintf(partitionBuf, sizeof(partitionBuf), "%s__common", deviceMountpoint);
  if (!fileXioMount(pfsMount, partitionBuf, FIO_MT_RDWR)) {
    DPRINTF("Mounted %s as %s\n", partitionBuf, pfsMount);
    return createMetadataEntry(deviceMountpoint, partitionBuf, pfsMount);
  }
  return NULL;
}

static void syncHDL(struct BackendDevice *device) {
  char pfsBase[12];
  if (!device || !device->metadev || !device->metadev->mountpoint ||
      getMountpointFromPath(device->metadev->mountpoint, pfsBase, sizeof(pfsBase)) != 0)
    return;
  fileXioDevctl("pfs:", PDIOC_CLOSEALL, NULL, 0, NULL, 0);
  fileXioSync(pfsBase, FXIO_WAIT);
}

static void cleanupHDL(struct BackendDevice *device) {
  char pfsBase[12];
  if (!device || !device->metadev || !device->metadev->mountpoint ||
      getMountpointFromPath(device->metadev->mountpoint, pfsBase, sizeof(pfsBase)) != 0)
    return;
  fileXioUmount(pfsBase);
}

// Initializes one backend device slot for APA-formatted HDL.
// Uses device info for mountpoint (e.g. hdd0:, hdd1:). Returns 1 on success, negative on error.
int initHDL(struct BackendDevice *slot) {
  char baseMountpoint[8];
  int maxDevices = getDeviceInfo(Device_HDD, baseMountpoint, sizeof(baseMountpoint));
  if (maxDevices <= 0)
    return -ENODEV;

  slot->type = Device_None;

  char path[12];
  for (int i = 0; i < maxDevices; i++) {
    snprintf(path, sizeof(path), "%s%d:", baseMountpoint, i);

    int maxAttempts = getProbeDelay() > 0 ? getProbeDelay() : 1;
    DIR *directory = NULL;
    for (int attempt = 0; attempt < maxAttempts; attempt++) {
      directory = opendir(path);
      if (directory != NULL) {
        closedir(directory);
        break;
      }
      sleep(1);
    }
    if (directory == NULL)
      continue;

    if (checkAPAHeader(path) != 0) {
      DPRINTF("No APA partition table on %s\n", path);
      continue;
    }

    slot->type = Device_HDD;
    slot->mountpoint = strdup(path);
    if (!slot->mountpoint)
      return -ENOMEM;
    slot->index = i;
    slot->scan = &findHDLTargets;
    slot->sync = &syncHDL;
    slot->cleanup = &cleanupHDL;
    slot->titles = NULL;
    char pfsMount[12];
    snprintf(pfsMount, sizeof(pfsMount), "pfs%d:", i);
    slot->metadev = mountPFS(path, pfsMount);
    if (!slot->metadev) {
      DPRINTF("Failed to mount PFS partition on %s\n", path);
      free(slot->mountpoint);
      slot->mountpoint = NULL;
      slot->type = Device_None;
      continue;
    }
    DPRINTF("Found device %s\n", slot->mountpoint);
    return 1;
  }
  return -ENODEV;
}

//
// The following code is based on hdlfs.irx
//

typedef struct // size = 1024
{
  u32 checksum; // HDL uses 0xdeadfeed magic here
  u32 magic;
  char gamename[160];
  u8 hdl_compat_flags;
  u8 ops2l_compat_flags;
  u8 dma_type;
  u8 dma_mode;
  char startup[60];
  u32 layer1_start;
  u32 discType;
  int num_partitions;
  struct {
    u32 part_offset; // in 2048b sectors
    u32 data_start;  // in 512b sectors
    u32 part_size;   // in bytes
  } part_specs[65];
} HDLHeader;

#define HDL_GAME_DATA_OFFSET 0x100000 // Sector 0x800 in the extended attribute area.
#define HDL_FS_MAGIC 0x1337

// Scans given partition and returns new Target if partition has a valid HDL header
Target *scanPartition(char *deviceMountpoint, char *partitionName, uint32_t startSector) {
  HDLHeader header;

  // Note: The APA specification states that there is a 4KB area used for storing the partition's information, before the extended attribute area.
  uint32_t lba = startSector + (HDL_GAME_DATA_OFFSET + 4096) / 512;
  uint32_t nsectors = 2; // 2 * 512 = 1024 byte

  // Read HDLoader header
  hddAtaTransfer_t *args = (hddAtaTransfer_t *)&header;
  args->lba = lba;
  args->size = nsectors;
  if (fileXioDevctl(deviceMountpoint, HDIOC_READSECTOR, args, sizeof(hddAtaTransfer_t), &header, nsectors * 512) != 0) {
    DPRINTF("ERROR: failed to read sector\n");
    return NULL;
  }

  if (header.checksum != 0xdeadfeed) {
    DPRINTF("ERROR: invalid HDL checksum (0x%X)\n", header.checksum);
    return NULL;
  }

  Target *title = calloc(sizeof(Target), 1);
  title->prev = NULL;
  title->next = NULL;
  title->id = strdup(header.startup);
  title->name = strdup(header.gamename);
  title->path = strdup(partitionName);

  return title;
}

// Scans given storage device and fills device->titles with valid launch candidates
// Returns 0 if successful, non-zero if no targets were found or an error occurs
int findHDLTargets(struct BackendDevice *device) {
  if (!device || !device->mountpoint)
    return -ENODEV;
  if (device->titles)
    freeTargetList(device->titles);
  device->titles = calloc(1, sizeof(TargetList));
  if (!device->titles)
    return -ENOMEM;
  TargetList *result = device->titles;

  int fd = fileXioDopen(device->mountpoint);
  if (fd < 0) {
    DPRINTF("ERROR: failed to open %s for scanning: %d\n", device->mountpoint, fd);
    free(device->titles);
    device->titles = NULL;
    return -ENODEV;
  }

  iox_dirent_t dirent = {0};
  while (fileXioDread(fd, &dirent) > 0) {
    if (dirent.stat.mode == HDL_FS_MAGIC && (dirent.stat.attr & APA_FLAG_SUB) == 0) {
      Target *title = scanPartition(device->mountpoint, dirent.name, dirent.stat.private_5);
      if (!title)
        continue;
      title->device = device;
      result->total++;
      if (result->first == NULL) {
        result->first = title;
        result->last = title;
      } else
        insertIntoTargetList(result, title);
    }
  }
  fileXioDclose(fd);

  if (result->total == 0) {
    freeTargetList(device->titles);
    device->titles = NULL;
    return -ENOENT;
  }

  TitleIDCache *cache = malloc(sizeof(TitleIDCache));
  int cacheNeedsSave = 0;
  if (loadTitleIDCache(cache, device) == 0) {
    if (cache->total != result->total)
      cacheNeedsSave = 1;
    Target *curTarget = result->first;
    while (curTarget != NULL) {
      CacheEntry *cached = getCachedEntry(curTarget->path, cache);
      if (cached != NULL)
        curTarget->flags = cached->flags;
      curTarget = curTarget->next;
    }
  } else
    cacheNeedsSave = 1;
  freeTitleCache(cache);

  int idx = 0;
  Target *curTitle = result->first;
  while (curTitle != NULL) {
    curTitle->idx = idx;
    idx++;
    curTitle = curTitle->next;
  }

  if (cacheNeedsSave) {
    DPRINTF("Updating title cache...\n");
    if (storeTitleIDCache(result, device))
      DPRINTF("Failed to save title cache\n");
  }
  return 0;
}
