#ifndef _BACKENDS_TARGET_H_
#define _BACKENDS_TARGET_H_

#include <stddef.h>
#include <stdint.h>

// Defined in backends.h
struct BackendDevice;

// Title flags (bitfield), used in Target and cache. Mask unknown bits when reading for forward compatibility.
typedef enum {
  TitleFlag_None = 0,
  TitleFlag_Favorite = (1 << 0),
  TitleFlag_FakeDEV9 = (1 << 1), // If set, will enable DEV9 faking
  /* bits 1–31 reserved for future use */
} TitleFlags;

// An entry in TargetList
typedef struct Target {
  uint16_t idx;                 // ISO index (monotonically increasing). Used to uniquely identify the list entry
  char *path;                   // Path relative to device->mountpoint (no repeated prefix)
  char *name;                   // Target name (extracted from file name)
  char *id;                     // Title ID
  uint32_t flags;               // TitleFlags bitfield
  struct BackendDevice *device; // Device entry

  struct Target *prev; // Previous target in the list
  struct Target *next; // Next target in the list
} Target;

// A linked list of launch candidates
typedef struct {
  int total;     // Total number of targets
  Target *first; // First target
  Target *last;  // Last target
} TargetList;

// Completely frees TargetList. Passed pointer will not be valid after this function executes
void freeTargetList(TargetList *result);

// Writes full path (device->mountpoint + path) into buf, always null-terminating. Returns 0 on success, negative on error/truncation.
int getTargetFullPath(const Target *target, char *buf, size_t bufSize);

// Finds target with given index in the list and returns a pointer to it
Target *getTargetByIdx(TargetList *targets, int idx);

// Makes and returns a deep copy of src without prev/next pointers.
Target *copyTarget(Target *src);

// Inserts title in the list while keeping the alphabetical order
void insertIntoTargetList(TargetList *result, Target *title);

// Completely frees Target and returns pointer to the next target in the list
Target *freeTarget(TargetList *targetList, Target *target);

#endif
