// Implements titleScanFunc for APA-formatted HDD with HDL partitions
#include "common.h"
#include "devices.h"
#include <hdd-ioctl.h>
#include <stdio.h>
#include <stdlib.h>

// Used to access APA partitions and read raw sectors
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

// The following code is based on hdlfs.irx

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

  // Initialize target
  printf("Found %s (%s) as %s\n", header.gamename, header.startup, partitionName);

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
  // Open HDD
  int fd = fileXioDopen(device->mountpoint);
  if (fd < 0) {
    printf("ERROR: failed to open %s for scanning: %d\n", device->mountpoint, fd);
    return -ENODEV;
  }

  iox_dirent_t dirent;
  // PS2HDD-BDM module enables using dread calls to read APA partitions
  while (fileXioDread(fd, &dirent) > 0) {
    // Check partition magic and partition flag
    if (dirent.stat.mode == HDL_FS_MAGIC && (dirent.stat.attr & APA_FLAG_SUB) == 0) {
      Target *title = scanPartition(device->mountpoint, dirent.name, dirent.stat.private_5);
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
  }

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

  fileXioDclose(fd);
  return 0;
}
