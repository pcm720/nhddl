#ifndef _UI_SCALE_H_
#define _UI_SCALE_H_

#include "config/config.h"
#include <gsKit.h>

// Virtual resolution used for layout (640x480).
#define SCALE_VIRTUAL_W 640
#define SCALE_VIRTUAL_H 480

// Display aspect ratio (for widescreen scaling). Set before or after scaleUpdate.
#define ASPECT_4_3  0
#define ASPECT_16_9 1
void scaleSetDisplayAspect(int ratio);
// So scenes can adapt content (e.g. more list rows in 16:9) while still using virtual coords for drawing.
int scaleGetDisplayAspect(void);

// Single source of truth: apply resolution and scaling for the given mode, then scaleUpdate.
// If v == VMode_NONE, does nothing (caller should set gs from ROM and call scaleUpdate).
void scaleApplyVMode(GSGLOBAL *gs, VModeType v);

// Call after setting gs->Mode, Width, Height, Interlace, Field. Computes scale and PAR for font/UI.
void scaleUpdate(GSGLOBAL *gs);

// Scale factors: virtual coords * scale = native. PAR is separate (used for font glyph aspect).
float scaleGetScaleX(void);
float scaleGetScaleY(void);

// Ratio-preserving scale for X (like OPL SCALING_RATIO). Use for images/textures so they are not
// stretched in widescreen: in 16:9 mode returns scaleX * 0.75f, else scaleX.
float scaleGetScaleXForRatio(void);

// Pixel aspect ratio for current mode (e.g. 14/15 NTSC, 16/15 PAL). Used by font for glyph aspect.
float scaleGetPAR(void);

// Virtual to native pixel coords (integer).
int scaleScaleX(int x);
int scaleScaleY(int y);
// Native back to virtual.
int scaleUnscaleX(int x);
int scaleUnscaleY(int y);

// Offset for drawing (0 when no letterbox).
int scaleGetOffsetX(void);
int scaleGetOffsetY(void);

// Virtual (640x480) to sprite/panel coords. Use for gsKit_prim_sprite etc.; applies offset when display > virtual so panels align.
int scaleVirtualToSpriteX(int x);
int scaleVirtualToSpriteY(int y);

// Non-zero when interlaced frame mode (glyph raster height and quad height use half size).
int scaleGetInterlacedFrame(void);

// Returns refresh rate in Hz for the current video mode
int scaleGetRefreshRateHz(void);

#endif
