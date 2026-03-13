#ifndef _UI_ICONS_H_
#define _UI_ICONS_H_

#include <gsKit.h>
#include <stddef.h>
#include <stdint.h>

// Icon types; order matches embedded icon assets (icons.c). Splash logo is not loaded here; see splash_scene.c.
typedef enum {
  ICON_CIRCLE,
  ICON_CROSS,
  ICON_SQUARE,
  ICON_TRIANGLE,
  ICON_L1,
  ICON_R1,
  ICON_SELECT,
  ICON_START,
  ICON_COUNT
} IconType;

// Load PNG from memory into texture (8-bit palettized or 32-bit RGBA). Caller allocates GSTEXTURE; this fills it and allocates Mem. Returns 0 on success.
// invertAlpha: 1 if PNG uses opposite alpha (0=opaque, 255=transparent); 0 for normal (gsKit: 0=opaque, 128=transparent).
int loadPngFromMemory(GSGLOBAL *gs, GSTEXTURE *texture, void *buf, size_t size);

// Initializes icon textures from embedded PNGs. Returns 0 on success.
int initIconsOnly(GSGLOBAL *gs);

// Draws icon at screen position (x,y) with size (w,h). No-op if icon not loaded.
void drawIconAt(GSGLOBAL *gs, float x, float y, float w, float h, int z, uint64_t color, IconType iconType);

int getIconWidth(IconType iconType);
int getIconHeight(IconType iconType);

#endif
