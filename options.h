#ifndef _OPTIONS_H_
#define _OPTIONS_H_

#include <iso.h>

// An entry in ArgumentList
typedef struct Argument {
  char *arg; // Argument
  char *value; // Argument value
  int isDisabled;

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
int getLastLaunchedTitle(const char *basePath, char *titlePath);

// Generates ArgumentList from global config file.
// Will reinitialize result without clearing existing contents. On error, result will contain invalid pointer.
int getGlobalLaunchArguments(struct ArgumentList *result, char *basePath);

// Generates ArgumentList from title-specific config file.
// Will reinitialize result without clearing existing contents. On error, result will contain invalid pointer.
int getTitleLaunchArguments(struct ArgumentList *result, struct Target *target);

// Saves title launch arguments to title-specific config file.
int updateTitleLaunchArguments(struct Target *target, struct ArgumentList *options);

// Completely frees ArgumentList. Passed pointer will not be valid after this function executes
void freeArgumentList(struct ArgumentList *result);

#endif