// Scaling from virtual resolution (640x480) to native display
// Based on implementation from Open PS2 Loader.
// Original code copyright:
// Copyright 2010, Volca

#include "ui/scale.h"
#include <gsKit.h>

static int displayWidth;
static int displayHeight;
static int offsetX = 0;
static int offsetY = 0;
static int spriteOffsetX = 0;
static int spriteOffsetY = 0;
static float scaleX = 1.0f;
static float scaleY = 1.0f;
static float par = 1.0f;
static int interlacedFrame = 0;
static int displayAspect = ASPECT_4_3;
static int modeAspect = ASPECT_4_3; // Native aspect ratio
static int refreshRate = 60;

// Defines video mode parameters
struct ScaleVMode {
  int mode;
  int width;
  int height;
  int interlace;
  int field;
  int par1;
  int par2;
  int ratio;
  int refreshRate;
};

static const struct ScaleVMode scaleVModeTable[] = {
    {GS_MODE_NTSC, 640, 448, GS_INTERLACED, GS_FIELD, 14, 15, ASPECT_4_3, 60},
    {GS_MODE_PAL, 640, 512, GS_INTERLACED, GS_FIELD, 16, 15, ASPECT_4_3, 50},
    {GS_MODE_DTV_480P, 640, 448, GS_NONINTERLACED, GS_FRAME, 14, 15, ASPECT_4_3, 60},
    {GS_MODE_DTV_720P, 1280, 720, GS_NONINTERLACED, GS_FRAME, 1, 1, ASPECT_16_9, 60},
    {-1, 640, 448, 0, 0, 1, 1, ASPECT_4_3},
};

static void getPARForMode(int mode, int *outPar1, int *outPar2, int *outRatio) {
  int i;
  for (i = 0; scaleVModeTable[i].mode >= 0; i++) {
    if (scaleVModeTable[i].mode == mode) {
      *outPar1 = scaleVModeTable[i].par1;
      *outPar2 = scaleVModeTable[i].par2;
      *outRatio = scaleVModeTable[i].ratio;
      return;
    }
  }
  *outPar1 = 1;
  *outPar2 = 1;
  *outRatio = ASPECT_4_3;
}

void scaleSetDisplayAspect(int ratio) { displayAspect = ratio ? ASPECT_16_9 : ASPECT_4_3; }
int scaleGetDisplayAspect(void) { return displayAspect; }

void scaleApplyVMode(GSGLOBAL *gs, VModeType v) {
  if (v == VMode_NONE || !gs)
    return;
  int i;
  for (i = 0; scaleVModeTable[i].mode >= 0; i++) {
    if (scaleVModeTable[i].mode == (int)v) {
      gs->Mode = scaleVModeTable[i].mode;
      gs->Width = scaleVModeTable[i].width;
      gs->Height = scaleVModeTable[i].height;
      gs->Interlace = scaleVModeTable[i].interlace;
      gs->Field = scaleVModeTable[i].field;
      refreshRate = scaleVModeTable[i].refreshRate;
      scaleUpdate(gs);
      return;
    }
  }
}

void scaleUpdate(GSGLOBAL *gs) {
  if (!gs) {
    displayWidth = SCALE_VIRTUAL_W;
    displayHeight = SCALE_VIRTUAL_H;
    scaleX = 1.0f;
    scaleY = 1.0f;
    par = 1.0f;
    spriteOffsetX = 0;
    spriteOffsetY = 0;
    return;
  }

  displayWidth = gs->Width;
  displayHeight = gs->Height;

  interlacedFrame = 0;
  if ((gs->Interlace == GS_INTERLACED) && (gs->Field == GS_FRAME)) {
    displayHeight /= 2;
    interlacedFrame = 1;
  }

  int par1, par2, modeRatio;
  getPARForMode(gs->Mode, &par1, &par2, &modeRatio);
  modeAspect = modeRatio;
  par = (float)par2 / (float)par1;

  // Anamorphic 16:9 on 4:3 mode
  if ((displayAspect == ASPECT_16_9) && (modeRatio == ASPECT_4_3))
    par *= 0.75f;

  if ((gs->Interlace == GS_INTERLACED) && (gs->Field == GS_FRAME))
    par *= 2.0f;

  // Scale from virtual 640x480 to display size only (no PAR in coords; PAR used for font glyph aspect).
  scaleX = (float)displayWidth / (float)SCALE_VIRTUAL_W;
  scaleY = (float)displayHeight / (float)SCALE_VIRTUAL_H;

  offsetX = 0;
  offsetY = 0;
  // When display is larger than virtual, centering offset for scaleVirtualToSpriteX/Y (e.g. letterbox).
  // Drawing uses scaleScaleX/Y + offsetX/Y (both 0) to fill the display.
  if (displayWidth > SCALE_VIRTUAL_W || displayHeight > SCALE_VIRTUAL_H) {
    spriteOffsetX = (displayWidth - SCALE_VIRTUAL_W) / 2;
    spriteOffsetY = (displayHeight - SCALE_VIRTUAL_H) / 2;
  } else {
    spriteOffsetX = 0;
    spriteOffsetY = 0;
  }
}

int scaleGetInterlacedFrame(void) { return interlacedFrame; }

float scaleGetScaleX(void) { return scaleX; }
float scaleGetScaleY(void) { return scaleY; }
// Like OPL rmSetupQuad with SCALING_RATIO: in 16:9 apply 3/4 to width so aspect is preserved.
// Use 16:9 ratio when either config says widescreen or the mode is natively 16:9 (e.g. 720p).
float scaleGetScaleXForRatio(void) {
  int useWide = (displayAspect == ASPECT_16_9) || (modeAspect == ASPECT_16_9);
  return useWide ? (scaleX * 0.75f) : scaleX;
}
float scaleGetPAR(void) { return par; }

int scaleScaleX(int x) { return (x * displayWidth) / SCALE_VIRTUAL_W; }

int scaleScaleY(int y) { return (y * displayHeight) / SCALE_VIRTUAL_H; }

int scaleUnscaleX(int x) { return (x * SCALE_VIRTUAL_W) / displayWidth; }

int scaleUnscaleY(int y) { return (y * SCALE_VIRTUAL_H) / displayHeight; }

int scaleGetOffsetX(void) { return offsetX; }
int scaleGetOffsetY(void) { return offsetY; }

int scaleVirtualToSpriteX(int x) { return x + spriteOffsetX; }

int scaleVirtualToSpriteY(int y) { return y + spriteOffsetY; }

int scaleGetRefreshRateHz(void) { return refreshRate; }
