#include "ui/view.h"
#include "devices/pad.h"
#include "ui/ui_shared.h"
#include <gsKit.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_VIEW_DEPTH 8

struct ViewStack {
  GSGLOBAL *gs;
  View *stack[MAX_VIEW_DEPTH];
  int count;
  View *overlay;
  int overlayFramesLeft;
};

struct ViewStack *viewStackCreate(GSGLOBAL *gs) {
  if (!gs)
    return NULL;
  struct ViewStack *s = calloc(1, sizeof(*s));
  if (!s)
    return NULL;
  s->gs = gs;
  return s;
}

void viewStackDestroy(struct ViewStack *stack) { free(stack); }

int viewStackPush(struct ViewStack *stack, View *view) {
  if (!stack || !view || stack->count >= MAX_VIEW_DEPTH)
    return -1;
  if (stack->count > 0 && stack->stack[stack->count - 1]->onLeave)
    stack->stack[stack->count - 1]->onLeave(stack->stack[stack->count - 1]);
  stack->stack[stack->count++] = view;
  if (view->onEnter)
    view->onEnter(view);
  return 0;
}

int viewStackPop(struct ViewStack *stack) {
  if (!stack || stack->count == 0)
    return -1;
  View *top = stack->stack[--stack->count];
  if (top->onLeave)
    top->onLeave(top);
  if (stack->count > 0 && stack->stack[stack->count - 1]->onEnter)
    stack->stack[stack->count - 1]->onEnter(stack->stack[stack->count - 1]);
  return 0;
}

int viewStackCount(const struct ViewStack *stack) { return stack ? stack->count : 0; }

View *viewStackTop(const struct ViewStack *stack) {
  if (!stack || stack->count == 0)
    return NULL;
  return stack->stack[stack->count - 1];
}

void viewStackSetOverlay(struct ViewStack *stack, View *view, int durationFrames) {
  if (!stack)
    return;
  stack->overlay = view;
  stack->overlayFramesLeft = durationFrames > 0 ? durationFrames : 0;
}

void viewStackClearOverlay(struct ViewStack *stack) {
  if (!stack)
    return;
  stack->overlay = NULL;
  stack->overlayFramesLeft = 0;
}

void viewInit(View *v, ViewType type, void *userdata, void (*onEnter)(View *), void (*onLeave)(View *), void (*draw)(View *, int),
              ViewResult (*onInput)(View *, int)) {
  if (!v)
    return;
  v->type = type;
  v->userdata = userdata;
  v->onEnter = onEnter;
  v->onLeave = onLeave;
  v->draw = draw;
  v->onInput = onInput;
}

int viewStackRunFrame(struct ViewStack *stack) {
  if (!stack || stack->count == 0)
    return 1;

  View *top = viewStackTop(stack);
  gsKit_clear(stack->gs, BGColor);

  // Draw all views from bottom to top. PS2 depth test is GREATER (larger Z = in front).
  // Depth buffer is cleared to 0, so we use Z starting at 1 so first draw passes (1 > 0).
  for (int i = 0; i < stack->count - 1; i++) {
    if (stack->stack[i]->draw)
      stack->stack[i]->draw(stack->stack[i], i);
  }
  // Dim underlay when top is a modal; same Z as first underlay
  if (top->type == ViewType_Modal && stack->count > 1) {
    int w = stack->gs->Width;
    int h = stack->gs->Height;
    int underlayZ = 1;
    gsKit_prim_sprite(stack->gs, 0.0f, 0.0f, (float)w, (float)h, underlayZ, DimColor);
  }
  // Full-screen scene: draw BG in native coords first so it fully covers underlays
  if (top->type == ViewType_Scene) {
    gsKit_prim_sprite(stack->gs, 0.0f, 0.0f, (float)stack->gs->Width, (float)stack->gs->Height, stack->count, BGColor);
  }
  // Top view at highest Z (front)
  if (top->draw)
    top->draw(top, stack->count);

  // Overlay on top of everything; no input.
  if (stack->overlay && stack->overlay->draw) {
    int overlayZ = stack->count + 1;
    stack->overlay->draw(stack->overlay, overlayZ);
    stack->overlayFramesLeft--;
    if (stack->overlayFramesLeft <= 0) {
      stack->overlay = NULL;
      stack->overlayFramesLeft = 0;
    }
  }

  if (gsGlobal->Mode == GS_MODE_DTV_720P) {
    gsKit_hires_sync(gsGlobal);
    gsKit_hires_flip(gsGlobal);
  } else {
    gsKit_set_finish(stack->gs);
    gsKit_queue_exec(stack->gs);
    gsKit_finish();
    gsKit_sync_flip(stack->gs);
  }
  gsKit_TexManager_nextFrame(stack->gs);

  int input = readInput();
  ViewResult r = top->onInput ? top->onInput(top, input) : ViewResult_Continue;
  if (r == ViewResult_Pop) {
    viewStackPop(stack);
    return stack->count == 0 ? 1 : 0;
  }
  return 0;
}
