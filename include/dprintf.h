#ifndef _DPRINTF_H_
#define _DPRINTF_H_

#include <stdio.h>

#ifdef ENABLE_PRINTF
    #define DPRINTF(x...) printf(x)
#else
    #define DPRINTF(x...)
#endif

#endif

