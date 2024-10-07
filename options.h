#ifndef _OPTIONS_H_
#define _OPTIONS_H_

#include <iso.h>

// Compatibility modes definitions

#define COMPAT_MODES_ARG "gc"
#define CM_NUM_MODES 5
#define CM_DISABLE_BUILTIN_MODES 1 << 0
#define CM_IOP_ACCURATE_READS 1 << 1
#define CM_IOP_SYNC_READS 1 << 2
#define CM_EE_UNHOOK_SYSCALLS 1 << 3
#define CM_IOP_EMULATE_DVD_DL 1 << 5
typedef struct CompatiblityModeMap {
  int mode;
  char value;
  char *name;
} CompatiblityModeMap;
extern const CompatiblityModeMap COMPAT_MODE_MAP[CM_NUM_MODES];

// An entry in ArgumentList
typedef struct Argument {
  char *arg;   // Argument
  char *value; // Argument value
  int isDisabled;
  int isGlobal;

  struct Argument *prev; // Previous target in the list
  struct Argument *next; // Next target in the list
} Argument;

// A linked list of options from config file
struct ArgumentList {
  int total;              // Total number of arguments
  struct Argument *first; // First target
  struct Argument *last;  // Last target
};

// Sets last launched title path in global config
int updateLastLaunchedTitle(char *titlePath);

// Gets last launched title path into titlePath
int getLastLaunchedTitle(char *titlePath);

// Generates ArgumentList from global config file.
// Will reinitialize result without clearing existing contents. On error, result will contain invalid pointer.
int getGlobalLaunchArguments(struct ArgumentList *result);

// Generates ArgumentList from title-specific config file.
// Will reinitialize result without clearing existing contents. On error, result will contain invalid pointer.
int getTitleLaunchArguments(struct ArgumentList *result, struct Target *target);

// Saves title launch arguments to title-specific config file.
// '$' before the argument name is used as 'disabled' flag.
// '$' value means that the argument is empty, but still should be used without the value.
int updateTitleLaunchArguments(struct Target *target, struct ArgumentList *options);

// Completely frees ArgumentList. Passed pointer will not be valid after this function executes
void freeArgumentList(struct ArgumentList *result);

// Does a deep copy of arg and inserts it into target.
// Always places COMPAT_MODES_ARG on the top of the list.
void insertArgumentCopy(struct ArgumentList *target, struct Argument *arg);

// Merges two lists into one, ignoring arguments in the second list that already exist in the first list.
// Expects result to be initialized with zeroes. All arguments in resulting list are a deep copy of arguments in source lists.
void mergeArgumentLists(struct ArgumentList *list1, struct ArgumentList *list2);

// Parses game compatibility mode argument value into a bitmask
uint8_t parseCompatModes(char *stringValue);

// Stores compatibility mode from bitmask into string.
// Target must be at least 6 bytes long, including null terminator
void storeCompatModes(char *target, uint8_t modes);

// Inserts a new compat mode arg into the argument list
void insertCompatModeArg(struct ArgumentList *target, uint8_t modes);

// Loads both global and title launch arguments, returning pointer to a merged list
struct ArgumentList *loadLaunchArgumentLists(struct Target *target);

#endif