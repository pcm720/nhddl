#ifndef _GUI_H_
#define _GUI_H_

#include "iso.h"

int uiInit();
int uiLoop(TargetList *titles);
void uiCleanup();

// Splash screen log level types
typedef enum {
  LEVEL_INFO_NODELAY, // Prints text without delay
  LEVEL_INFO,         // Prints in regular color and waits for a second
  LEVEL_WARN,         // Prints in warning color and waits for two seconds
  LEVEL_ERROR,        // Prints in error color and waits for two seconds
} UILogLevelType;

// Initializes and starts UI splash thread
int startSplashScreen();

// Logs to splash screen and debug console in a thread-safe way
void uiSplashLogString(UILogLevelType level, const char *str, ...);

// Stops UI splash thread
void stopUISplashThread();

#endif
