#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//
// This code is based on isofs.irx and title ID parsing code from Neutrino
//

#define SECTOR_SIZE 2048
#define TOC_LBA 16
#define SYSTEM_CNF_NAME "SYSTEM.CNF;1"

struct dirTOCEntry {
  short length;
  uint32_t fileLBA;         // 2
  uint32_t fileLBA_bigend;  // 6
  uint32_t fileSize;        // 10
  uint32_t fileSize_bigend; // 14
  uint8_t dateStamp[6];     // 18
  uint8_t reserved1;        // 24
  uint8_t fileProperties;   // 25
  uint8_t reserved2[6];     // 26
  uint8_t filenameLength;   // 32
  char filename[128];       // 33
} __attribute__((packed));

static unsigned char iso_buf[SECTOR_SIZE];

// Sets file offset to specified LBA while handling large LBAs
static void longLseek(int fd, unsigned int lba);
// Reads Primary Volume Descriptor from specified LBA and extracts root directory LBA
static int getPVD(int fd, uint32_t *lba, int *length);
// Retrieves SYSTEM.CNF TOC entry using specified root directory TOC
static struct dirTOCEntry *getTOCEntry(int fd, uint32_t tocLBA, int tocLength);

// Loads SYSTEM.CNF from ISO and extracts title ID
char *getTitleID(char *path) {
  // Open ISO
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    logString("%s:\nERROR: Failed to open file: %d\n", fd, path);
    return NULL;
  }

  // Get location of root directory entry
  uint32_t rootLBA = 0;
  int rootLength = 0;
  if (getPVD(fd, &rootLBA, &rootLength) != 0) {
    logString("%s:\nERROR: Failed to parse ISO PVD\n", path);
    close(fd);
    return NULL;
  }

  // Get SYSTEM.CNF entry
  struct dirTOCEntry *tocEntry = getTOCEntry(fd, rootLBA, rootLength);
  if (tocEntry == NULL) {
    logString("%s:\nERROR: Failed to find SYSTEM.CNF\n", path);
    close(fd);
    return NULL;
  }

  // Seek to SYSTEM.CNF location and read file contents
  longLseek(fd, tocEntry->fileLBA);
  char *systemCNF = malloc(tocEntry->length);
  if (read(fd, systemCNF, tocEntry->length) != tocEntry->length) {
    logString("%s:\nERROR: Failed to read SYSTEM.CNF\n", path);
    free(systemCNF);
    close(fd);
    return NULL;
  }

  char *boot2Arg = strstr(systemCNF, "BOOT2");
  if (boot2Arg == NULL) {
    logString("%s:\nERROR: BOOT2 not found in SYSTEM.CNF\n", path);
    free(systemCNF);
    close(fd);
    return NULL;
  }

  char *titleID = calloc(sizeof(char), 12);
  // Locate and set ELF file name
  char *selfFile = strstr(boot2Arg, "cdrom0:");
  char *argEnd = strstr(boot2Arg, ";");
  if (selfFile == NULL || argEnd == NULL) {
    logString("%s:\nERROR: File name not found in SYSTEM.CNF\n", path);
    free(titleID);
    titleID = NULL;
  } else {
    // Extract title ID
    argEnd[1] = '1';
    argEnd[2] = '\0';
    memcpy(titleID, &selfFile[8], 11);
  }

  free(systemCNF);
  close(fd);
  return titleID;
}

// Sets file offset to specified LBA while handling large LBAs
static void longLseek(int fd, unsigned int lba) {
  // If offset fits into INT_MAX, seek and return
  if (lba <= INT_MAX / SECTOR_SIZE) {
    lseek(fd, lba * SECTOR_SIZE, SEEK_SET);
    return;
  }

  // Else, seek while handling overflows
  unsigned int remaining, toSeek;
  lseek(fd, INT_MAX / SECTOR_SIZE * SECTOR_SIZE, SEEK_SET);
  remaining = lba - INT_MAX / SECTOR_SIZE;
  while (remaining > 0) {
    toSeek = remaining > INT_MAX / SECTOR_SIZE ? INT_MAX / SECTOR_SIZE : remaining;
    lseek(fd, toSeek * SECTOR_SIZE, SEEK_CUR);
    remaining -= toSeek;
  }
}

// Reads Primary Volume Descriptor from specified LBA and extracts root directory LBA
static int getPVD(int fd, uint32_t *lba, int *length) {
  // Seek to PVD LBA
  longLseek(fd, TOC_LBA);
  // Read the sector
  if (read(fd, iso_buf, SECTOR_SIZE) == SECTOR_SIZE) {
    // Make sure the sector contains PVD (type code 1, identifier CD001)
    if ((iso_buf[0x00] == 1) && (!memcmp(&iso_buf[0x01], "CD001", 5))) {
      // Read root directory entry and get LBA and length
      struct dirTOCEntry *tocEntryPointer = (struct dirTOCEntry *)&iso_buf[0x9c];
      *lba = tocEntryPointer->fileLBA;
      *length = tocEntryPointer->length;
      return 0;
    } else {
      return -EINVAL;
    }
  }
  return -EIO;
}

// Retrieves SYSTEM.CNF TOC entry using specified root directory TOC
static struct dirTOCEntry *getTOCEntry(int fd, uint32_t tocLBA, int tocLength) {
  // Read TOC entries
  while (tocLength > 0) {
    // Seek to next LBA
    longLseek(fd, tocLBA);
    // Read the sector
    if (read(fd, iso_buf, SECTOR_SIZE) != SECTOR_SIZE) {
      return NULL;
    }

    // Read directory entries until the end of sector
    int tocPos = 0;
    struct dirTOCEntry *tocEntryPointer;
    do {
      tocEntryPointer = (struct dirTOCEntry *)&iso_buf[tocPos];

      if (tocEntryPointer->length == 0)
        break;

      if (tocEntryPointer->filenameLength && !strcmp(SYSTEM_CNF_NAME, tocEntryPointer->filename)) {
        // File has been found
        return tocEntryPointer;
      }
      // Advance to the next entry
      tocPos += (tocEntryPointer->length << 16) >> 16;
    } while (tocPos < 2016);

    // Get next sector LBA
    tocLength -= SECTOR_SIZE;
    tocLBA++;
  }

  return NULL;
}
