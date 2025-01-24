#include "target.h"
#include "common.h"
#include "devices.h"
#include <errno.h>
#include <fcntl.h>
#include <ps2sdkapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Completely frees TargetList. Passed pointer will not be valid after this function executes
void freeTargetList(TargetList *result) {
  Target *target = result->first;
  while (target != NULL) {
    target = freeTarget(result, target);
  }
  result->first = NULL;
  result->last = NULL;
  result->total = 0;
  free(result);
}

// Finds target with given index in the list and returns a pointer to it
Target *getTargetByIdx(TargetList *targets, int idx) {
  Target *current = targets->first;
  while (1) {
    if (current->idx == idx) {
      return current;
    }

    if (current->next == NULL)
      break;

    current = current->next;
  }
  return NULL;
}

// Makes and returns a deep copy of src without prev/next pointers.
Target *copyTarget(Target *src) {
  Target *copy = calloc(sizeof(Target), 1);
  copy->idx = src->idx;

  copy->fullPath = strdup(src->fullPath);
  copy->name = strdup(src->name);
  copy->id = strdup(src->id);
  copy->device = src->device;

  return copy;
}

// Converts lowercase ASCII string into uppercase
void toUppercase(char *str) {
  for (int i = 0; i <= strlen(str); i++)
    if (str[i] >= 0x61 && str[i] <= 0x7A) {
      str[i] -= 32;
    }
}

// Inserts title in the list while keeping the alphabetical order
void insertIntoTargetList(TargetList *result, Target *title) {
  // Traverse the list in reverse
  Target *curTitle = result->last;

  // Covert title name to uppercase
  char *curUppercase = strdup(title->name);
  toUppercase(curUppercase);

  // Overall, title name should not exceed PATH_MAX
  char lastUppercase[PATH_MAX];

  while (1) {
    // Reset string buffer
    lastUppercase[0] = '\0';
    // Convert name of the last title to uppercase
    strlcpy(lastUppercase, curTitle->name, PATH_MAX);
    toUppercase(lastUppercase);

    // Compare new title name and the current title name
    if (strcmp(curUppercase, lastUppercase) >= 0) {
      // First letter of the new title is after or the same as the current one
      // New title must be inserted after the current list element
      if (curTitle->next != NULL) {
        // Current title has a next title, update the next element
        curTitle->next->prev = title;
        title->next = curTitle->next;
      } else {
        // Current title has no next title (it's the last list element)
        result->last = title;
      }
      title->prev = curTitle;
      curTitle->next = title;
      break;
    }

    if (curTitle->prev == NULL) {
      // Current title is the first in this list
      // New title must be inserted at the beginning
      curTitle->prev = title;
      title->next = curTitle;
      result->first = title;
      break;
    }

    // Keep traversing the list
    curTitle = curTitle->prev;
  }
  free(curUppercase);
}

// Completely frees Target and returns pointer to the next target in the list
Target *freeTarget(TargetList *targetList, Target *target) {
  // Update target list if target is the first or the last element
  if (targetList->first == target) {
    targetList->first = target->next;
  }
  if (targetList->last == target) {
    targetList->last = target->prev;
  }

  Target *next = NULL;
  // If target has a link to the next element
  if (target->next != NULL) {
    // Set return pointer
    next = target->next;
    if (target->prev != NULL) {
      // If target has a link to the previous element, link prev and next together
      next->prev = target->prev;
      target->prev->next = next;
    } else {
      // Else, remove link to target
      next->prev = NULL;
    }
  } else if (target->prev != NULL) {
    // If target doesn't have a link to the next element
    // but has a link to the previous element, remove link to target
    target->prev->next = NULL;
  }

  free(target->fullPath);
  free(target->name);
  if (target->id != NULL)
    free(target->id);

  free(target);
  return next;
}
