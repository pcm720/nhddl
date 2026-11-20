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

// Frees loaded icon textures (EE Mem/Clut). Safe if none loaded.
void freeIcons(void);

// Initializes icon textures from embedded PNGs. Returns 0 on success.
int initIcons(GSGLOBAL *gs);

// Draws icon at screen position (x,y) with size (w,h). No-op if icon not loaded.
void drawIconAt(GSGLOBAL *gs, float x, float y, float w, float h, int z, uint64_t color, IconType iconType);

int getIconWidth(IconType iconType);
int getIconHeight(IconType iconType);

#endif
