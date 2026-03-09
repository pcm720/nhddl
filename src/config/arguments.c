#include "config/arguments.h"
#include "backends/backends.h"
#include "dprintf.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
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
  Argument *copy = calloc(sizeof(Argument), 1);
  copy->isDisabled = src->isDisabled;
  if (src->arg)
    copy->arg = strdup(src->arg);
  if (src->value)
    copy->value = strdup(src->value);
  return copy;
}

// Replaces argument and value in dst, freeing arg and value.
// Keeps next and prev pointers.
static void replaceArgument(Argument *dst, Argument *src) {
  if (dst->arg)
    free(dst->arg);
  if (dst->value)
    free(dst->value);
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

// Merges src into dst, replacing duplicate names with src entries.
// All arguments merged from src are a deep copy. Expects both lists to be initialized.
void mergeArgumentLists(ArgumentList *dst, ArgumentList *src) {
  Argument *curArg1;
  Argument *curArg2 = src->first;
  int isDuplicate = 0;

  while (curArg2 != NULL) {
    isDuplicate = 0;
    curArg1 = dst->first;
    while (curArg1 != NULL) {
      if (!strcmp(curArg2->arg, curArg1->arg)) {
        isDuplicate = 1;
        replaceArgument(curArg1, curArg2);
        break;
      }
      curArg1 = curArg1->next;
    }
    if (!isDuplicate) {
      appendArgumentCopy(dst, curArg2);
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

static char *skip_space(char *s) {
  while (isspace((unsigned char)*s))
    s++;
  return s;
}

static void trim_trailing(char *s) {
  char *end = s + strlen(s);
  while (end > s && (isspace((unsigned char)end[-1]) || end[-1] == '\r'))
    *--end = '\0';
}

// Parses file into ArgumentList. Result may contain parsed arguments even if an error is returned.
// CNF format: one argument per line as -name=value or -name; # starts comments; # -name=value is disabled.
// If device is non-NULL, values starting with / or \ are resolved against device->mountpoint.
static int parseConfigFile(ArgumentList *result, struct BackendDevice *device, FILE *file) {
  char lineBuffer[PATH_MAX];
  lineBuffer[0] = '\0';

  while (fgets(lineBuffer, sizeof(lineBuffer), file)) {
    char *line = skip_space(lineBuffer);
    trim_trailing(line);
    if (line[0] == '\0')
      continue;
    if (line[0] != '-' && line[0] != '#')
      continue;

    int isDisabled = 0;
    if (line[0] == '#') {
      line = skip_space(line + 1);
      if (line[0] != '-')
        continue;
      isDisabled = 1;
    }
    line++; // skip '-'

    char *value = strchr(line, '=');
    if (value) {
      *value++ = '\0';
      value = skip_space(value);
      value[strcspn(value, "#\r\n")] = '\0';
      trim_trailing(value);
    } else {
      value = (char *)"";
    }
    trim_trailing(line);
    if (line[0] == '\0')
      continue;

    char *resolved = NULL;
    if (device && value[0] != '\0' && (value[0] == '/' || value[0] == '\\')) {
      resolved = malloc(strlen(device->mountpoint) + strlen(value) + 1);
      if (resolved) {
        strcpy(resolved, device->mountpoint);
        strcat(resolved, value);
      }
    }
    const char *val = resolved ? resolved : value;
    Argument *arg = newArgument(line, val[0] ? (char *)val : NULL);
    free(resolved);
    arg->isDisabled = isDisabled;
    appendArgument(result, arg);
  }

  if (ferror(file) || !feof(file)) {
    DPRINTF("config: error: failed to read config file\n");
    return -EIO;
  }
  return 0;
}

// Parses a CNF-format file into ArgumentList. Overwrites options. device may be NULL.
int loadArgumentList(ArgumentList *options, struct BackendDevice *device, char *filePath) {
  FILE *file = fopen(filePath, "r");
  if (file == NULL) {
    DPRINTF("config: error: failed to open %s\n", filePath);
    return -ENOENT;
  }

  options->total = 0;
  options->first = NULL;
  options->last = NULL;

  if (parseConfigFile(options, device, file)) {
    fclose(file);
    freeArgumentList(options);
    return -EIO;
  }

  fclose(file);
  return 0;
}
