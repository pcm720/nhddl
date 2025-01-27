#ifndef _OPTIONS_H_
#define _OPTIONS_H_

#include "devices.h"
#include "target.h"
#include <ps2sdkapi.h>

// Location of configuration directory relative to storage mountpoint
extern const char BASE_CONFIG_PATH[];
extern const size_t BASE_CONFIG_PATH_LEN;

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
typedef struct {
  int total;       // Total number of arguments
  Argument *first; // First target
  Argument *last;  // Last target
} ArgumentList;

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
void buildConfigFilePath(char *targetPath, const char *targetMountpoint, const char *targetFileName);

// Gets last launched title path into titlePath
// Searches for the latest file across all mounted BDM devices
int getLastLaunchedTitle(char *titlePath);

// Writes last launched title path into lastTitle file on title mountpoint
int updateLastLaunchedTitle(struct DeviceMapEntry *device, char *titlePath);

// Generates ArgumentList from global config file located on device
// Will reinitialize result without clearing existing contents. On error, result will contain invalid pointer.
int getGlobalLaunchArguments(ArgumentList *result, struct DeviceMapEntry *device);

// Generates ArgumentList from title-specific config file.
// Will reinitialize result without clearing existing contents. On error, result will contain invalid pointer.
int getTitleLaunchArguments(ArgumentList *result, Target *target);

// Saves title launch arguments to title-specific config file.
// '$' before the argument name is used as 'disabled' flag.
// Empty value means that the argument is empty, but still should be used without the value.
int updateTitleLaunchArguments(Target *target, ArgumentList *options);

// Completely frees ArgumentList. Passed pointer will not be valid after this function executes
void freeArgumentList(ArgumentList *result);

// Retrieves argument from the list
// Creates new argument and inserts it into the list if argument with argumentName doesn't exist
Argument *getArgument(ArgumentList *target, char *argumentName, char *defaultValue);

// Creates new Argument with passed argName and value (without copying)
Argument *newArgument(char *argName, char *value);

// Appends arg to the end of target
void appendArgument(ArgumentList *target, Argument *arg);

// Does a deep copy of arg and inserts it into target.
// Always places COMPAT_MODES_ARG on the top of the list.
void appendArgumentCopy(ArgumentList *target, Argument *arg);

// Merges two lists into one, ignoring arguments in the second list that already exist in the first list.
// Expects result to be initialized with zeroes. All arguments in resulting list are a deep copy of arguments in source lists.
void mergeArgumentLists(ArgumentList *list1, ArgumentList *list2);

// Loads both global and title launch arguments, returning pointer to a merged list
ArgumentList *loadLaunchArgumentLists(Target *target);

// Parses options file into ArgumentList
int loadArgumentList(ArgumentList *options, struct DeviceMapEntry *device, char *filePath);

#endif
