#ifndef _CONFIG_ARGUMENTS_H_
#define _CONFIG_ARGUMENTS_H_

// An entry in ArgumentList
typedef struct Argument {
  char *arg;   // Argument
  char *value; // Argument value
  int isDisabled;
  int isGlobal;

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

// Merges two lists into one, ignoring arguments in the second list that already exist in the first list.
// Expects result to be initialized with zeroes. All arguments in resulting list are a deep copy of arguments in source lists.
void mergeArgumentLists(ArgumentList *list1, ArgumentList *list2);

#endif
