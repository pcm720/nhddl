// Shared UI-thread context
#ifndef _UI_UI_CONTEXT_H_
#define _UI_UI_CONTEXT_H_

#include "backends/target.h"
#include "devices/devices.h"
#include <stdint.h>

#define UI_OSD_MESSAGE_SIZE 96

struct MainSceneData;

typedef struct UiContext {
  Target *selectedTitle;
  DeviceType enabledDevices; // Working copy while editing in menu
  char osdMessage[UI_OSD_MESSAGE_SIZE];
  int osdFramesLeft;
  int exitRequested;
  int listUpdating;          // 1 while a list-affecting worker job is in flight
  int openedMenuOnEmpty;     // Auto-opened main menu once for empty list
  int pendingSaveAfterRescan;
  int marqueeOffset;         // Pixel offset for selected ISO name marquee
  int marqueeHoldFrames;     // Pause at ends before scrolling
  struct MainSceneData *main;
} UiContext;

void uiContextInit(UiContext *ctx, struct MainSceneData *main);
UiContext *uiGetContext(void);

// Poll worker Done state
void uiPollWorker(void);

#endif
