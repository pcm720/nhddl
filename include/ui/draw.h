#ifndef _UI_DRAW_H_
#define _UI_DRAW_H_

#include "ui/icons.h"
#include <gsKit.h>
#include <stdint.h>

// Draw a filled rectangle. Rect in virtual coords (640x480); scaled to display and drawn with gsKit_prim_sprite.
void uiDrawRect(GSGLOBAL *gs, int vx1, int vy1, int vx2, int vy2, int z, uint64_t color);

// Draw a filled rectangle in native (screen) coords. Use when the rect is already in native space (e.g. ratio-preserving cover border).
void uiDrawRectNative(GSGLOBAL *gs, int nx1, int ny1, int nx2, int ny2, int z, uint64_t color);

// Draw a filled rectangle with rounded corners. radius_v is in virtual coords; clamped so rect is valid.
void uiDrawRectRounded(GSGLOBAL *gs, int vx1, int vy1, int vx2, int vy2, int radius_v, int z, uint64_t color);

// Filled circle. Center and radius in virtual coords.
void uiDrawCircleFilled(GSGLOBAL *gs, int cx_v, int cy_v, int radius_v, int z, uint64_t color);

// Selection indicator: when selected draws a small filled circle at (vx, vy) with radius_v; when not selected draws nothing (for list rows).
void uiDrawSelectionIndicator(GSGLOBAL *gs, int vx, int vy, int radius_v, int z, uint64_t color, int selected);

// Draw an icon at virtual (vx, vy). Size is scaled from icon's native size.
void uiDrawIcon(GSGLOBAL *gs, int vx, int vy, int z, uint64_t color, IconType iconType);

// Draw an icon at virtual (vx, vy) with explicit virtual size (vw, vh). Use for prompts so all icons share the same display height.
void uiDrawIconScaled(GSGLOBAL *gs, int vx, int vy, int vw, int vh, int z, uint64_t color, IconType iconType);

// Draw a single prompt: icon and label
void uiDrawModalPrompt(GSGLOBAL *gs, int vx1, int vx2, int slot, int y, int z, uint64_t color, IconType iconType, const char *label);
void uiDrawScenePrompt(GSGLOBAL *gs, int slot, int z, uint64_t color, IconType iconType, const char *label);

// Draw texture in virtual rect [vx,vy] size (vw,vh). Ratio-preserving scale, centered. If tex is NULL or has no size, draws solid rect (placeholder).
void uiDrawTextureInVirtualRect(GSGLOBAL *gs, int vx, int vy, int vw, int vh, GSTEXTURE *tex, int z, uint64_t color, int disableAlphaTest);

// Draw a filled rect in virtual (vx,vy,vw,vh) with ratio-preserving scale and centered (same as cover art). Use for borders around ratio-preserving
// content.
void uiDrawRectRatioPreserving(GSGLOBAL *gs, int vx, int vy, int vw, int vh, int z, uint64_t color);

// Convert virtual rect to native (screen) coords. For rare cases when native coords are needed (e.g. passing to code that expects native).
void uiVirtualRectToNative(int vx1, int vy1, int vx2, int vy2, int *out_x1, int *out_y1, int *out_x2, int *out_y2);

#endif
