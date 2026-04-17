#ifndef _UI_PNG_H_
#define _UI_PNG_H_

#include <gsKit.h>
#include <stddef.h>
#include <stdint.h>

// Loads PNG from file into GSTEXTURE and uploads to GS VRAM.
// Supports 8-bit palettized and 32-bit RGBA
int loadPNGFromFile(GSGLOBAL *gs, GSTEXTURE *texture, char *path);

// Loads PNG from memory into GSTEXTURE and uploads to GS VRAM.
// Supports 8-bit palettized (icons) and 32-bit RGBA (e.g. splash logo)
int loadPNGFromMemory(GSGLOBAL *gs, GSTEXTURE *texture, void *buf, size_t size);

#endif
