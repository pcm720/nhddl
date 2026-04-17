#ifndef _UI_VIEW_H_
#define _UI_VIEW_H_

#include "backends/target.h"
#include <gsKit.h>
#include <stdint.h>

// View categories for the UI framework. Scenes are full-screen; popups and
// modals overlay the current scene. Modals block input to views below until dismissed.
// ViewType_Overlay: used for the optional full-screen overlay (see viewStackSetOverlay).
// Overlay views are not on the stack; only draw is used; onEnter, onLeave, onInput are ignored.
typedef enum {
  ViewType_Scene,
  ViewType_Modal,
  ViewType_Overlay,
} ViewType;

// Result of view frame: continue, or request to pop this view (e.g. exit, back).
typedef enum {
  ViewResult_Continue,
  ViewResult_Pop,
} ViewResult;

// Opaque view stack; defined in view.c
struct ViewStack;

// View lifecycle and frame callbacks. draw and onInput are called each frame.
// onEnter when the view becomes top; onLeave when it is popped or another view is pushed on top.
// draw receives zOrder: larger = in front (PS2 depth test is GREATER). Underlays get 1,2,...; top gets stack->count.
typedef struct View {
  ViewType type;
  void *userdata;

  void (*onEnter)(struct View *v);
  void (*onLeave)(struct View *v);
  void (*draw)(struct View *v, int zOrder);
  ViewResult (*onInput)(struct View *v, int padInput);
} View;

// Creates an empty view stack. gs is used for draw/flip. Returns NULL on failure.
struct ViewStack *viewStackCreate(GSGLOBAL *gs);

// Frees the stack and any remaining views. Does not call onLeave on views.
void viewStackDestroy(struct ViewStack *stack);

// Pushes a view onto the stack. The previous top receives onLeave; the new top receives onEnter.
// Returns 0 on success, -1 on failure.
int viewStackPush(struct ViewStack *stack, View *view);

// Pops the top view. The popped view receives onLeave; the new top receives onEnter.
// Returns 0 if a view was popped, -1 if stack was empty.
int viewStackPop(struct ViewStack *stack);

// Returns the number of views on the stack.
int viewStackCount(const struct ViewStack *stack);

// Returns the top view, or NULL if stack is empty.
View *viewStackTop(const struct ViewStack *stack);

// Overlay: drawn on top of everything for a limited time. No input. Use a View with only draw set (e.g. type ViewType_Overlay).
// Replaces any current overlay. durationFrames: how many frames to show (e.g. 120 for ~2 s at 60 fps).
void viewStackSetOverlay(struct ViewStack *stack, View *view, int durationFrames);

// Clears the current overlay immediately (stops drawing and countdown).
void viewStackClearOverlay(struct ViewStack *stack);

// Initializes a view with the given type, userdata, and callbacks. Use to zero and set all fields.
void viewInit(View *v, ViewType type, void *userdata,
              void (*onEnter)(View *), void (*onLeave)(View *),
              void (*draw)(View *, int zOrder), ViewResult (*onInput)(View *, int));

// Runs one frame: draw all views from bottom to top (so popups/modals overlay),
// then poll pad and dispatch input to top view only.
// If top view returns ViewResult_Pop, pops it. Returns 1 when stack empty (exit loop), 0 to continue.
int viewStackRunFrame(struct ViewStack *stack);

#endif
