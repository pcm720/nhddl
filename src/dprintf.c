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
