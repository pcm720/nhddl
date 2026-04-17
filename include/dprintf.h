#ifndef _DPRINTF_H_
#define _DPRINTF_H_

#include <stdio.h>


// printf implementation for EE SIO
int sio_printf(const char *format, ...);

// Displays the error on screen
void displayFatalError(const char *format, ...);

#ifdef ENABLE_PRINTF
#ifndef USE_EESIO
#define DPRINTF(x...) printf(x)
#else
#define DPRINTF(x...) sio_printf(x)
#endif
#else
#define DPRINTF(x...)
#endif

#endif
