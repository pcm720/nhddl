#ifndef _ISO_H_
#define _ISO_H_

// An entry in targetList
typedef struct target {
  char *fullPath; // Full path to ISO
  char *name;     // Target name (extracted from file name)
  char *id;       // Title ID

  struct target *prev; // Previous target in the list
  struct target *next; // Next target in the list
} target;

// A linked list of launch candidates
struct targetList {
  int total;            // Total number of targets
  struct target *first; // First target
  struct target *last;  // Last target
};

// Generates a list of launch candidates in rootpath
struct targetList *findISO(char *rootpath);

#endif