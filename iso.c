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

int _findISO(DIR *directory, struct targetList *result);
char *getTitleID(char *path);

struct targetList *findISO(char *rootpath) {
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

  struct targetList *result = malloc(sizeof(struct targetList));
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

// Searches rootpath and adds discovered ISOs to targetList
int _findISO(DIR *directory, struct targetList *result) {
  if (directory == NULL)
    return -ENOENT;
  // Read directory entries
  struct dirent *entry;
  char titlePath[PATH_MAX + 1];
  char *fileext;
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
        if (!getcwd(titlePath, PATH_MAX + 1)) {
          logString("ERROR: Failed to get cwd\n");
          closedir(directory);
          return -ENOENT;
        }
        strcat(titlePath, "/");
        strcat(titlePath, entry->d_name);

        // Initialize target
        struct target *title = malloc(sizeof(struct target));
        title->prev = NULL;
        title->next = NULL;
        title->fullPath = malloc(strlen(titlePath));
        strcpy(title->fullPath, titlePath);

        // Get file name without the extension
        int nameLength = (int)(fileext - entry->d_name);
        title->name = malloc(nameLength + 1);
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
      }
    }
  }

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
    logString("ERROR: file name not found in SYSTEM.CNF\n");
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