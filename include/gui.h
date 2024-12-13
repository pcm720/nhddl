#ifndef _GUI_H_
#define _GUI_H_

#include "iso.h"

int uiInit();
int uiLoop(TargetList *titles);
void uiCleanup();

typedef struct {
  int32_t readySemaphore;
} SplashThreadArguments;

// Splash screen log level types
typedef enum {
  LEVEL_INFO_NODELAY,
  LEVEL_INFO,
  LEVEL_WARN,
  LEVEL_ERROR,
} UILogLevelType;

// Initializes and starts UI splash thread
int startSplashScreen();

// Logs to splash screen and debug console in a thread-safe way
void uiSplashLogString(UILogLevelType level, const char *str, ...);

// Stops UI splash thread
void stopUISplashThread();

#endif
