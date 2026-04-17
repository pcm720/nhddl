// Shared state and helpers used by multiple scene modules. Defined in ui_main.c.
#ifndef _UI_UI_SHARED_H_
#define _UI_UI_SHARED_H_

#include <gsKit.h>

// Predefined colors
// static const uint64_t ColorWhite = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, 0x80);
static const uint64_t ColorBlack = GS_SETREG_RGBA(0x00, 0x00, 0x00, 0x80);
static const uint64_t ColorSelected = GS_SETREG_RGBA(0x00, 0x72, 0xA0, 0x80);
static const uint64_t ColorGrey = GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80);

static const uint64_t FontMainColor = ColorGrey;
static const uint64_t BGColor =  ColorBlack;
static const uint64_t HeaderTextColor = GS_SETREG_RGBA(0x50, 0x50, 0x50, 0x80);
static const uint64_t ModalBGColor =  GS_SETREG_RGBA(0x10, 0x10, 0x10, 0x80);
static const uint64_t WarnTextColor = GS_SETREG_RGBA(0x60, 0x60, 0x00, 0x80);
static const uint64_t ErrorTextColor = GS_SETREG_RGBA(0x60, 0x00, 0x00, 0x80);
static const uint64_t DimColor = GS_SETREG_RGBA(0, 0, 0, 0x40);

struct ViewStack;

extern GSGLOBAL *gsGlobal;
extern struct ViewStack *viewStack;

void showOSD(const char *msg, int frames);

// OSD overlay reads these (set by showOSD).
const char *getOSDMessage(void);
int getOSDFramesLeft(void);

#endif
