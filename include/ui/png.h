#ifndef _UI_PNG_H_
#define _UI_PNG_H_

#include <gsKit.h>
#include <stddef.h>
#include <stdint.h>

// Loads PNG from file into GSTEXTURE without binding the texture to GS. gs may be NULL.
// Supports 8-bit palettized and 32-bit RGBA. UI thread must bind before draw.
int loadPNGFromFile(GSGLOBAL *gs, GSTEXTURE *texture, char *path);

// Loads PNG from memory into GSTEXTURE without binding the texture to GS. gs may be NULL.
// Supports 8-bit palettized (icons) and 32-bit RGBA (e.g. splash logo).
int loadPNGFromMemory(GSGLOBAL *gs, GSTEXTURE *texture, void *buf, size_t size);

#endif
