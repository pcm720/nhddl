#ifndef _BACKENDS_TARGET_H_
#define _BACKENDS_TARGET_H_

#include <stddef.h>
#include <stdint.h>

// Defined in backends.h
struct BackendDevice;

// Title flags (bitfield), used in Target and cache.
// For cachem, mask unknown bits with CachedTitleFlagsMask for forward compatibility.
typedef enum {
  TitleFlag_None = 0,
  // Flags stored in cache
  TitleFlag_Favorite = (1 << 0),
  TitleFlag_FakeDEV9 = (1 << 1), // If set, will enable DEV9 faking
  // Compressed formats
  TitleFlag_ZSO = (1 << 2),
  TitleFlag_CSO = (1 << 3),
  TitleFlag_CHD = (1 << 4),
  /* bits 5–31 reserved for future use */
} TitleFlags;

// Used to mask off flags that should be stored in title cache
#define CachedTitleFlagsMask (TitleFlag_Favorite | TitleFlag_FakeDEV9)

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

// Inserts title in the list while keeping the alphabetical order. Returns 0 on success, -1 if insertion failed (e.g. OOM).
int insertIntoTargetList(TargetList *result, Target *title);

// Completely frees Target and returns pointer to the next target in the list
Target *freeTarget(TargetList *targetList, Target *target);

#endif
