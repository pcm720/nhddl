#ifndef _UI_DRAW_H_
#define _UI_DRAW_H_

#include "ui/icons.h"
#include <gsKit.h>
#include <stddef.h>
#include <stdint.h>

// Draw a filled rectangle. Rect in virtual coords (640x480); scaled to display and drawn with gsKit_prim_sprite.
void uiDrawRect(GSGLOBAL *gs, int vx1, int vy1, int vx2, int vy2, int z, uint64_t color);

// Draw a filled rectangle in native (screen) coords. Use when the rect is already in native space (e.g. ratio-preserving cover border).
void uiDrawRectNative(GSGLOBAL *gs, int nx1, int ny1, int nx2, int ny2, int z, uint64_t color);

// Draw a filled rectangle with rounded corners. radius_v is in virtual coords; clamped so rect is valid.
void uiDrawRectRounded(GSGLOBAL *gs, int vx1, int vy1, int vx2, int vy2, int radius_v, int z, uint64_t color);

// Filled circle. Center and radius in virtual coords.
void uiDrawCircleFilled(GSGLOBAL *gs, int cx_v, int cy_v, int radius_v, int z, uint64_t color);

// Selection indicator radius Y nudge (font glyphs sit low in the line box).
#define UI_SEL_INDICATOR_R 5
#define UI_SEL_INDICATOR_OPTICAL_NUDGE 1
#define UI_SEL_LABEL_INDENT (UI_SEL_INDICATOR_R * 4)

// Center Y for a selection indicator beside FONT_DEFAULT text in a row of height rowH.
int uiSelectionIndicatorCenterY(int rowY, int rowH);

// Selection indicator: when selected draws a small filled circle at (vx, vy) with radius_v; when not selected draws nothing (for list rows).
void uiDrawSelectionIndicator(GSGLOBAL *gs, int vx, int vy, int radius_v, int z, uint64_t color, int selected);

// Draw a choice row: optional selection indicator + left-aligned label.
void uiDrawChoiceRow(GSGLOBAL *gs, int leftX, int rightX, int entryY, int rowH, int z, uint64_t color, int on, const char *label);

// Trim the path into out, empty/NULL uses emptyLabel (e.g. "(none)"/"(not set)").
void uiTrimPathMiddle(const char *src, char *out, size_t outSize, const char *emptyLabel);

// Content panel width (title options/file picker) and centered left edge.
int uiContentPanelWidth(void);
int uiContentPanelLeft(void);

// Draw an icon at virtual (vx, vy). Size is scaled from icon's native size.
void uiDrawIcon(GSGLOBAL *gs, int vx, int vy, int z, uint64_t color, IconType iconType);

// Draw an icon at virtual (vx, vy) with explicit virtual size (vw, vh). Use for prompts so all icons share the same display height.
void uiDrawIconScaled(GSGLOBAL *gs, int vx, int vy, int vw, int vh, int z, uint64_t color, IconType iconType);

// Scene footer prompt for one icon with label. Pass ICON_COUNT as iconType2 via uiDrawScenePrompt2 for dual icons.
void uiDrawScenePrompt(GSGLOBAL *gs, int slot, int z, uint64_t color, IconType iconType, const char *label);
// Scene footer prompt for two icons (e.g. Cross+Circle) with one label
void uiDrawScenePrompt2(GSGLOBAL *gs, int slot, int z, uint64_t color, IconType iconType, IconType iconType2, const char *label);

// Draw texture in virtual rect [vx,vy] size (vw,vh). Ratio-preserving scale, centered. If tex is NULL or has no size, draws solid rect (placeholder).
void uiDrawTextureInVirtualRect(GSGLOBAL *gs, int vx, int vy, int vw, int vh, GSTEXTURE *tex, int z, uint64_t color, int disableAlphaTest);

#endif
