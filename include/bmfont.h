#ifndef _BMFONT_H_
#define _BMFONT_H_

#include <stdint.h>

#define ALIGN_LEFT 0 << 0
#define ALIGN_RIGHT 1 << 0
#define ALIGN_TOP 0 << 1
#define ALIGN_BOTTOM 1 << 1
#define ALIGN_VCENTER 1 << 2
#define ALIGN_HCENTER 1 << 3
#define ALIGN_NONE (ALIGN_TOP | ALIGN_LEFT)
#define ALIGN_CENTER (ALIGN_VCENTER | ALIGN_HCENTER)

int initFont();

// Draws the text with specified max dimensions relative to x and y
// Returns the bottom Y coordinate of the last line that can be used to draw the next text
int drawText(int x, int y, int z, int maxWidth, int maxHeight,  uint64_t color, const char *text);

// Draws the text in [x1,y1],[x2,y2] window.
// Doesn't draw the glyphs that do not fit in the set window.
// Returns the bottom Y coordinate of the last line that can be used to draw the next text.
// Use the faster drawText method if window limits are not important.
int drawTextWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, const char *text);

// Frees memory used by the font
void closeFont();

#endif