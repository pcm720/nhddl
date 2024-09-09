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

#include "common.h"
#include "iso.h"

int _findISO(DIR *directory, struct TargetList *result);
char *getTitleID(char *path);

struct TargetList *findISO(char *rootpath) {
  DIR *directory;
  // Try to open directory, giving a chance to IOP modules to init
  for (int i = 0; i < 1000; i++) {
    directory = opendir(rootpath);
    if (directory != NULL)
      break;
    nopdelay();
  }
  // Check if the directory can be opened
  if (directory == NULL) {
    logString("ERROR: Can't open %s\n", rootpath);
    return NULL;
  }

  struct TargetList *result = malloc(sizeof(struct TargetList));
  result->total = 0;
  result->first = NULL;
  result->last = NULL;
  chdir(rootpath);
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
        title->idx = result->total;
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
          // Else, update the last entry
          result->last->next = title;
          title->prev = result->last;
          result->last = title;
        }
        titlePath[cwdLen] = '\0'; // reset titlePath by ending string on base path
      }
    }
  }

  free(titlePath);
  return 0;
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
    logString("ERROR: Unable to open SYSTEM.CNF from disk\n");
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
    logString("ERROR: File name not found in SYSTEM.CNF\n");
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