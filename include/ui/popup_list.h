#ifndef _UI_POPUP_LIST_H_
#define _UI_POPUP_LIST_H_

#include "ui/view.h"
#include <stddef.h>

typedef void (*PopupValueFn)(int index, char *buf, size_t bufSize, void *userdata);
typedef void (*PopupActivateFn)(int index, View *v, void *userdata);
typedef void (*PopupAdjustFn)(int index, int dir, void *userdata);
typedef int (*PopupSelectedFn)(int index, void *userdata);

typedef enum {
  PopupList_ValueRows = 0, // label left, value right
  PopupList_Choices = 1,   // selection indicator + label
} PopupListStyle;

typedef struct {
  const char *label;
  PopupValueFn getValue;      // ValueRows, can be NULL
  PopupActivateFn onActivate; // Cross/Circle (select)
  PopupAdjustFn onAdjust;     // Left/Right (optional)
} PopupListItem;

typedef struct {
  const char *title; // NULL for no header, "" will act as a spacer
  PopupListStyle style;
  const PopupListItem *items;
  int count;
  int *selection;
  void *userdata;
  PopupSelectedFn isOn;       // Choices: filled indicator, NULL = highlight-only
  ViewResult (*onBack)(View *v, void *userdata); // NULL → Pop (Triangle)
} PopupListDesc;

void popupListDraw(const PopupListDesc *desc, int zOrder);
ViewResult popupListOnInput(const PopupListDesc *desc, View *v, int padInput);

#endif
