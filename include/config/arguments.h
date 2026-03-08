#ifndef _CONFIG_ARGUMENTS_H_
#define _CONFIG_ARGUMENTS_H_

struct BackendDevice;

// An entry in ArgumentList
typedef struct Argument {
  char *arg;   // Argument
  char *value; // Argument value
  int isDisabled;

  struct Argument *prev; // Previous argument in the list
  struct Argument *next; // Next argument in the list
} Argument;

// A linked list of options from config file
typedef struct {
  int total;       // Total number of arguments
  Argument *first; // First argument
  Argument *last;  // Last argument
} ArgumentList;

// Completely frees ArgumentList. Passed pointer will not be valid after this function executes
void freeArgumentList(ArgumentList *result);

// Retrieves argument from the list
Argument *getArgument(ArgumentList *target, const char *argumentName);

// Creates new argument and inserts it into the list
Argument *insertArgument(ArgumentList *target, const char *argumentName, char *value);

// Creates new Argument with passed argName and value. Copies both argName and value
Argument *newArgument(const char *argName, char *value);

// Appends arg to the end of target
void appendArgument(ArgumentList *target, Argument *arg);

// Does a deep copy of arg and inserts it into target.
// Always places COMPAT_MODES_ARG on the top of the list
void appendArgumentCopy(ArgumentList *target, Argument *arg);

// Merges src into dst, replacing duplicate names with src entries.
// Expects both lists to be initialized. All arguments merged from src are a deep copy.
void mergeArgumentLists(ArgumentList *dst, ArgumentList *src);

// Parses a CNF-format file into ArgumentList. Overwrites options. device may be NULL (no path resolution).
// Returns 0 on success; on error may leave options partially filled.
int loadArgumentList(ArgumentList *options, struct BackendDevice *device, char *filePath);

#endif
