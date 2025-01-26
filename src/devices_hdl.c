// Implements support for APA-formatted HDD with HDL partitions
#include "common.h"
#include "devices.h"
#include "gui.h"
#include <hdd-ioctl.h>
#include <stdio.h>
#include <stdlib.h>

// Used to access APA partitions and read raw sectors
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// Checks and returns 0 if hdd0 contains APA partition table
int checkAPAHeader() {
  int result = -1;

  // Allocate memory for storing data for the first sector.
  uint8_t *pSectorData = (uint8_t *)malloc(512);
  if (pSectorData == NULL) {
    return -ENOMEM;
  }

  // Read the sector via devctl
  hddAtaTransfer_t *args = (hddAtaTransfer_t *)pSectorData;
  args->lba = 0;
  args->size = 1;
  result = fileXioDevctl("hdd0:", HDIOC_READSECTOR, args, sizeof(hddAtaTransfer_t), pSectorData, 512);
  if (result < 0) {
    free(pSectorData);
    return -EIO;
  }

  // Test if sector contains APA magic
  if (strncmp((const char *)&pSectorData[4], "APA", 3)) {
    result = 1; // Sector doesn't contain APA magic
  }

  free(pSectorData);
  return result;
}

// Attempts to mount PFS partition containing OPL files and returns DeviceModeEntry for mounted filesystem
// Note: this device entry is not added to deviceModeMap and might leak memory if not freed properly
struct DeviceMapEntry *mountPFS() {
  char mountpoint[] = "pfs0:";
  static char *pfsPartitions[] = {
      "hdd0:+OPL",
      "hdd0:OPL",
      "hdd0:__common",
  };

  for (int i = 0; i < sizeof(pfsPartitions) / sizeof(char *); i++) {
    if (fileXioMount(mountpoint, pfsPartitions[i], FIO_MT_RDWR)) {
      continue;
    }

    printf("Mounted %s as pfs0:\n", pfsPartitions[i]);
    struct DeviceMapEntry *dev = malloc(sizeof(struct DeviceMapEntry));
    dev->scan = NULL;
    dev->metadev = NULL;
    dev->mode = MODE_HDL;
    dev->mountpoint = strdup(mountpoint);
    dev->index = 0;
    return dev;
  }

  return NULL;
}

void syncHDL() {
  fileXioDevctl("pfs:", PDIOC_CLOSEALL, NULL, 0, NULL, 0);
  fileXioSync("pfs0:", FXIO_WAIT);
}

// Initializes map entries for APA-formatted HDDs with HDL partitions
int initHDL(int deviceIdx) {
  char mountpoint[] = "hdd0:";
  DIR *directory;

  deviceModeMap[deviceIdx].mode = MODE_NONE;

  // Wait for IOP to initialize device driver
  for (int attempts = 0; attempts < 2; attempts++) {
    delay(2);
    directory = opendir(mountpoint);
    if (directory != NULL) {
      closedir(directory);
      break;
    }
  }
  if (directory == NULL) {
    return -ENODEV;
  }

  // Make sure hdd0: is an APA-formatted drive
  if (checkAPAHeader()) {
    printf("ERROR: failed to find APA partition table on hdd0\n");
    return -ENODEV;
  }

  // Set device mountpoint
  deviceModeMap[deviceIdx].mode = MODE_HDL;
  deviceModeMap[deviceIdx].mountpoint = strdup(mountpoint);
  deviceModeMap[deviceIdx].index = 0;
  // Set functions
  deviceModeMap[deviceIdx].scan = &findHDLTargets;
  deviceModeMap[deviceIdx].sync = &syncHDL;

  // Mount metadata partition
  deviceModeMap[deviceIdx].metadev = mountPFS();
  if (!deviceModeMap[deviceIdx].metadev) {
    printf("Failed to mount PFS partition\n");
    return -ENODEV;
  }

  uiSplashLogString(LEVEL_INFO_NODELAY, "Found device %s\n", mountpoint);

  return 1;
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
    printf("ERROR: failed to read sector\n");
    return NULL;
  }

  if (header.checksum != 0xdeadfeed) {
    printf("ERROR: invalid HDL checksum (0x%X)\n", header.checksum);
    return NULL;
  }

  Target *title = calloc(sizeof(Target), 1);
  title->prev = NULL;
  title->next = NULL;
  title->id = strdup(header.startup);
  title->name = strdup(header.gamename);
  // Build full path
  title->fullPath = calloc(sizeof(char), strlen(partitionName) + 5);
  strcpy(title->fullPath, "hdl:");
  strcat(title->fullPath, partitionName);

  return title;
}

// Scans given storage device and appends valid launch candidates to TargetList
// Returns 0 if successful, non-zero if no targets were found or an error occurs
int findHDLTargets(TargetList *result, struct DeviceMapEntry *device) {
  // Open the drive
  int fd = fileXioDopen(device->mountpoint);
  if (fd < 0) {
    printf("ERROR: failed to open %s for scanning: %d\n", device->mountpoint, fd);
    return -ENODEV;
  }

  iox_dirent_t dirent;
  // PS2HDD Dread calls return APA partitions
  uint64_t accLBA = 0; // Used to keep track of partition LBA as a workaround for upstream ps2hdd module not setting dirent.stat.private_5
  while (fileXioDread(fd, &dirent) > 0) {
    // Check partition magic and partition flag
    if (dirent.stat.mode == HDL_FS_MAGIC && (dirent.stat.attr & APA_FLAG_SUB) == 0) {
      Target *title = scanPartition(device->mountpoint, dirent.name, accLBA);
      if (title == NULL) {
        continue;
      }
      title->device = device;

      // Increment title counter and update target list
      result->total++;
      if (result->first == NULL) {
        // If this is the first entry, update both pointers
        result->first = title;
        result->last = title;
      } else {
        insertIntoTargetList(result, title);
      }
    }
    // Add partition size (in blocks) to get next partition's LBA
    accLBA += dirent.stat.size;
  }
  fileXioDclose(fd);

  if (result->total == 0)
    return -ENOENT;

  // Set indexes for each title
  int idx = 0;
  Target *curTitle = result->first;
  while (curTitle != NULL) {
    curTitle->idx = idx;
    idx++;
    curTitle = curTitle->next;
  }

  return 0;
}
