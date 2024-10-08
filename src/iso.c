#include "iso.h"
#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <kernel.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Required for mounting and loading ISOs to retrieve Title ID
#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>
#include <io_common.h>

int _findISO(DIR *directory, struct TargetList *result);
void insertIntoList(struct TargetList *result, struct Target *title);
char toUppercase(char a);
char *getTitleID(char *path);

struct TargetList *findISO() {
  DIR *directory;
  // Try to open directory, giving a chance to IOP modules to init
  for (int i = 0; i < 1000; i++) {
    directory = opendir(STORAGE_BASE_PATH);
    if (directory != NULL)
      break;
    nopdelay();
  }
  // Check if the directory can be opened
  if (directory == NULL) {
    logString("ERROR: Can't open %s\n", STORAGE_BASE_PATH);
    return NULL;
  }

  struct TargetList *result = malloc(sizeof(struct TargetList));
  result->total = 0;
  result->first = NULL;
  result->last = NULL;
  chdir(STORAGE_BASE_PATH);
  if (_findISO(directory, result)) {
    free(result);
    closedir(directory);
    return NULL;
  }
  closedir(directory);
  return result;
}

// Searches rootpath and adds discovered ISOs to TargetList
int _findISO(DIR *directory, struct TargetList *result) {
  if (directory == NULL)
    return -ENOENT;
  // Read directory entries
  struct dirent *entry;
  char *fileext;
  char *titlePath = calloc(sizeof(char), PATH_MAX + 1);
  if (!getcwd(titlePath, PATH_MAX + 1)) { // Initialize titlePath with current working directory
    logString("ERROR: Failed to get cwd\n");
    free(titlePath);
    return -ENOENT;
  }
  int cwdLen = strlen(titlePath); // Get the length of base path string
  while ((entry = readdir(directory)) != NULL) {
    // Check if the entry is a directory using d_type
    switch (entry->d_type) {
    case DT_DIR:
      // Open dir and change cwd
      DIR *d = opendir(entry->d_name);
      chdir(entry->d_name);
      // Process inner directory recursively
      _findISO(d, result);
      // Return back to root directory
      chdir("..");
      closedir(d);
      continue;
    default:
      if (entry->d_name[0] == '.') // Ignore .files (most likely macOS doubles)
        continue;

      // Make sure file has .iso extension
      fileext = strrchr(entry->d_name, '.');
      if ((fileext != NULL) && !strcmp(fileext, ".iso")) {
        // Generate full path
        strcat(titlePath, "/");
        strcat(titlePath, entry->d_name);

        // Initialize target
        struct Target *title = calloc(sizeof(char), sizeof(struct Target));
        title->prev = NULL;
        title->next = NULL;
        title->fullPath = calloc(sizeof(char), strlen(titlePath) + 1);
        strcpy(title->fullPath, titlePath);

        // Get file name without the extension
        int nameLength = (int)(fileext - entry->d_name);
        title->name = calloc(sizeof(char), nameLength + 1);
        strncpy(title->name, entry->d_name, nameLength);

        // Get title ID
        title->id = getTitleID(title->fullPath);

        // Increment title counter and update target list
        result->total++;
        if (result->first == NULL) {
          // If this is the first entry, update both pointers
          result->first = title;
          result->last = title;
        } else {
          insertIntoList(result, title);
        }
        titlePath[cwdLen] = '\0'; // reset titlePath by ending string on base path
      }
    }
  }

  // Set indexes for each title
  int idx = 0;
  struct Target *curTitle = result->first;
  while (curTitle != NULL) {
    curTitle->idx = idx;
    idx++;
    curTitle = curTitle->next;
  }

  free(titlePath);
  return 0;
}

// Inserts title in the list while keeping the alphabetical order
void inline insertIntoList(struct TargetList *result, struct Target *title) {
  // Traverse the list in reverse
  struct Target *curTitle = result->last;
  char current = toUppercase(title->name[0]);
  char last;
  while (1) {
    // Compare first letters of the new title and the current title
    last = toUppercase(curTitle->name[0]);

    if ((current - last) >= 0) {
      // First letter of the new title is after or the same as the current one
      // New title must be inserted after the current list element
      if (curTitle->next != NULL) {
        // Current title has a next title, update the next element
        curTitle->next->prev = title;
        title->next = curTitle->next;
      } else {
        // Current title has no next title (it's the last list element)
        result->last = title;
      }
      title->prev = curTitle;
      curTitle->next = title;
      break;
    }

    if (curTitle->prev == NULL) {
      // Current title is the first in this list
      // New title must be inserted at the beginning
      curTitle->prev = title;
      title->next = curTitle;
      result->first = title;
      break;
    }

    // Keep traversing the list
    curTitle = curTitle->prev;
  }
}

// Converts lower-case ASCII letter into upper-case
char toUppercase(char a) {
  if (a >= 0x61 && a <= 0x7A) {
    return a - 32;
  }
  return a;
}

// The following code was copied from neutrino with minimal changes:

// Loads SYSTEM.CNF from ISO and extracts title ID
char *getTitleID(char *path) {
  if (fileXioMount("iso:", path, FIO_MT_RDONLY) < 0) {
    logString("ERROR: Unable to mount %s as iso\n", path);
    return NULL;
  }

  int system_cnf_fd = open("iso:/SYSTEM.CNF;1", O_RDONLY);
  if (system_cnf_fd < 0) {
    logString("ERROR: Unable to open SYSTEM.CNF: %s\n", path);
    fileXioUmount("iso:");
    return NULL;
  }

  // Read file contents
  char system_cnf_data[128];
  read(system_cnf_fd, system_cnf_data, 128);
  close(system_cnf_fd);

  // Locate and set ELF file name
  char *selfFile = strstr(system_cnf_data, "cdrom0:");
  char *fname_end = strstr(system_cnf_data, ";");
  if (selfFile == NULL || fname_end == NULL) {
    logString("ERROR: File name not found in SYSTEM.CNF: %s\n", path);
    fileXioUmount("iso:");
    return NULL;
  }
  fname_end[1] = '1';
  fname_end[2] = '\0';

  char *titleID = malloc(12);
  // Locate and set title ID
  memcpy(titleID, &selfFile[8], 11);
  titleID[11] = '\0';

  fileXioUmount("iso:");

  return titleID;
}

// Completely frees Target and returns pointer to a previous argument in the list
struct Target *freeTarget(struct Target *target) {
  struct Target *prev = NULL;
  free(target->fullPath);
  free(target->name);
  free(target->id);
  if (target->prev != NULL) {
    prev = target->prev;
  }
  free(target);
  return prev;
}

// Completely frees TargetList. Passed pointer will not be valid after this function executes
void freeTargetList(struct TargetList *result) {
  struct Target *target = result->last;
  while (target != NULL) {
    target = freeTarget(target);
  }
  result->first = NULL;
  result->last = NULL;
  result->total = 0;
  free(result);
}

// Finds target with given index in the list and returns a pointer to it
struct Target *getTargetByIdx(struct TargetList *targets, int idx) {
  struct Target *current = targets->first;
  while (1) {
    if (current->idx == idx) {
      return current;
    }

    if (current->next == NULL)
      break;

    current = current->next;
  }
  return NULL;
}

// Makes and returns a deep copy of src without prev/next pointers.
struct Target *copyTarget(struct Target *src) {
  struct Target *copy = calloc(sizeof(struct Target), 1);
  copy->idx = src->idx;

  copy->fullPath = malloc(strlen(src->fullPath)+1);
  strcpy(copy->fullPath, src->fullPath);
  copy->name = malloc(strlen(src->name)+1);
  strcpy(copy->name, src->name);
  copy->id = malloc(strlen(src->id)+1);
  strcpy(copy->id, src->id);

  return copy;
}