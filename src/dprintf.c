#include "dprintf.h"
#include <debug.h>
#include <sio.h>
#include <stdarg.h>
#include <stdio.h>

// printf implementation for EE SIO
int sio_printf(const char *format, ...) {
  static char buf[1024];
  va_list args;
  int size;

  va_start(args, format);
  size = vsnprintf(buf, 1024, format, args);
  va_end(args);

  sio_write(buf, size);

  return size;
}

static int isScreenInitialized = 0;

// Displays the error on screen
void displayFatalError(const char *format, ...) {
  if (!isScreenInitialized) {
    init_scr();
    scr_setCursor(0);
    scr_clear();
    isScreenInitialized = 1;
    scr_printf(".\n\n\n\n");
  }

  scr_printf("\t");
  va_list args;
  va_start(args, format);
  scr_vprintf(format, args);
  DPRINTF(format, args);
  va_end(args);
}
