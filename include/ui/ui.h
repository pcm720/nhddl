#ifndef _UI_H_
#define _UI_H_

#include "target.h"

int uiInit();
int uiLoop(TargetList *titles);
void uiCleanup();

// Blocks until the next vertical blank without spinning, yielding the CPU
// to background threads. No-op if the UI is not initialized.
void uiWaitVSync();

// Splash screen log level types
typedef enum {
  LEVEL_INFO_NODELAY, // Prints text without delay
  LEVEL_INFO,         // Prints in regular color and waits for a second
  LEVEL_WARN,         // Prints in warning color and waits for two seconds
  LEVEL_ERROR,        // Prints in error color and waits for two seconds
  LEVEL_PROGRESS,     // Internal: used by uiSplashLogProgress
} UILogLevelType;

// Initializes and starts UI splash thread
int startSplashScreen();

// Logs to splash screen and debug console in a thread-safe way
void uiSplashLogString(UILogLevelType level, const char *str, ...);

// Shows scan progress on the splash screen without delay.
// total > 0 draws a progress bar with a cur/total counter;
// total == 0 draws a spinner with just the count (unknown total).
void uiSplashLogProgress(const char *label, int cur, int total);

// Sets Neutrino version on the splash screen
void uiSplashSetNeutrinoVersion(const char *str);

// Stops UI splash thread
void stopUISplashThread();

#endif
