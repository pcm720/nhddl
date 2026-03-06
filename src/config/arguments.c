#include "config/arguments.h"
#include <stdlib.h>
#include <string.h>

// Completely frees Argument and returns pointer to a previous argument in the list
Argument *freeArgument(Argument *arg) {
  Argument *prev = NULL;
  if (arg->arg)
    free(arg->arg);
  if (arg->value)
    free(arg->value);
  if (arg->prev)
    prev = arg->prev;

  free(arg);
  return prev;
}

// Completely frees ArgumentList. Passed pointer will not be valid after this function executes
void freeArgumentList(ArgumentList *result) {
  Argument *tArg = result->last;
  while (tArg != NULL) {
    tArg = freeArgument(tArg);
  }
  result->first = NULL;
  result->last = NULL;
  result->total = 0;
  free(result);
}

// Makes and returns a deep copy of src without prev/next pointers.
Argument *copyArgument(Argument *src) {
  // Do a deep copy for argument and value
  Argument *copy = calloc(sizeof(Argument), 1);
  copy->isGlobal = src->isGlobal;
  copy->isDisabled = src->isDisabled;
  if (src->arg)
    copy->arg = strdup(src->arg);
  if (src->value)
    copy->value = strdup(src->value);
  return copy;
}

// Replaces argument and value in dst, freeing arg and value.
// Keeps next and prev pointers.
void replaceArgument(Argument *dst, Argument *src) {
  // Do a deep copy for argument and value
  if (dst->arg)
    free(dst->arg);
  if (dst->value)
    free(dst->value);
  dst->isGlobal = src->isGlobal;
  dst->isDisabled = src->isDisabled;
  if (src->arg)
    dst->arg = strdup(src->arg);
  if (src->value)
    dst->value = strdup(src->value);
}

// Creates new Argument with passed argName and value.
// Copies both argName and value
Argument *newArgument(const char *argName, char *value) {
  Argument *arg = malloc(sizeof(Argument));
  arg->isDisabled = 0;
  arg->isGlobal = 0;
  arg->prev = NULL;
  arg->next = NULL;
  if (argName)
    arg->arg = strdup(argName);
  if (value)
    arg->value = strdup(value);

  return arg;
}

// Appends arg to the end of target
void appendArgument(ArgumentList *target, Argument *arg) {
  target->total++;

  if (!target->first) {
    target->first = arg;
  } else {
    target->last->next = arg;
    arg->prev = target->last;
  }
  target->last = arg;
}

// Does a deep copy of arg and inserts it into target.
// Always places COMPAT_MODES_ARG on the top of the list
void appendArgumentCopy(ArgumentList *target, Argument *arg) {
  // Do a deep copy for argument and value
  Argument *copy = copyArgument(arg);
  appendArgument(target, copy);
}

// Merges two lists into one, ignoring arguments in the second list that already exist in the first list.
// All arguments merged from the second list are a deep copy of arguments in source lists.
// Expects both lists to be initialized.
void mergeArgumentLists(ArgumentList *list1, ArgumentList *list2) {
  Argument *curArg1;
  Argument *curArg2 = list2->first;
  int isDuplicate = 0;

  // Copy arguments from the second list into result
  while (curArg2 != NULL) {
    isDuplicate = 0;
    // Look for duplicate arguments in the first list
    curArg1 = list1->first;
    while (curArg1 != NULL) {
      // If result already contains argument with the same name, skip it
      if (!strcmp(curArg2->arg, curArg1->arg)) {
        isDuplicate = 1;
        // If argument is disabled and has no value
        if (curArg1->isDisabled && (curArg1->value[0] == '\0')) {
          // Replace element in list1 with disabled element from list2
          replaceArgument(curArg1, curArg2);
          curArg1->isDisabled = 1;
        }
        break;
      }
      curArg1 = curArg1->next;
    }
    // If no duplicate was found, insert the argument
    if (!isDuplicate) {
      appendArgumentCopy(list1, curArg2);
    }
    curArg2 = curArg2->next;
  }
}

// Retrieves argument from the list
Argument *getArgument(ArgumentList *target, const char *argumentName) {
  Argument *arg = target->first;
  while (arg != NULL) {
    if (!strcmp(arg->arg, argumentName)) {
      return arg;
    }
    arg = arg->next;
  }
  return NULL;
}

// Creates new argument and inserts it into the list
Argument *insertArgument(ArgumentList *target, const char *argumentName, char *value) {
  Argument *arg = newArgument(argumentName, value);
  appendArgument(target, arg);
  return arg;
}
