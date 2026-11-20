// Shared colors and globals used by scene modules.
#ifndef _UI_UI_SHARED_H_
#define _UI_UI_SHARED_H_

#include "ui/ui_context.h"
#include <gsKit.h>

static const uint64_t ColorBlack = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);
static const uint64_t ColorSelected = GS_SETREG_RGBA(0x00, 0x72, 0xA0, 0x80);
static const uint64_t ColorGrey = GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80);

static const uint64_t FontMainColor = ColorGrey;
static const uint64_t BGColor = ColorBlack;
static const uint64_t HeaderTextColor = GS_SETREG_RGBA(0x50, 0x50, 0x50, 0x80);
static const uint64_t ModalBGColor = GS_SETREG_RGBA(0x10, 0x10, 0x10, 0x80);
static const uint64_t DimColor = GS_SETREG_RGBA(0, 0, 0, 0x40);

struct ViewStack;

extern GSGLOBAL *gsGlobal;
extern struct ViewStack *viewStack;
extern UiContext uiContext;

void showOSD(const char *msg, int frames);

const char *getOSDMessage(void);

// Reapply getVMode()/getWidescreen()
void uiApplyDisplaySettings(void);

#endif
