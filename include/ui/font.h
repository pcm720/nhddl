#ifndef _UI_FONT_H_
#define _UI_FONT_H_

// FreeType font rendering with texture atlas (single font). Use id 0 for all APIs.
#include <stddef.h>
#include <stdint.h>

#define FONT_DEFAULT 0
#define FONT_ERROR   (-1)
#define FONT_DEFAULT_SIZE 17

// Alignment bitmasks for fontRenderInRect (rect [x1,y1]-[x2,y2]). Combine with |.
// Horizontal: no flag = left; vertical: no flag = top.
#define FONT_ALIGN_LEFT   0
#define FONT_ALIGN_RIGHT  (1 << 0)
#define FONT_ALIGN_TOP    0
#define FONT_ALIGN_BOTTOM (1 << 1)
#define FONT_ALIGN_VCENTER (1 << 2)
#define FONT_ALIGN_HCENTER (1 << 3)
#define FONT_ALIGN_NONE   (FONT_ALIGN_LEFT | FONT_ALIGN_TOP)
#define FONT_ALIGN_CENTER (FONT_ALIGN_VCENTER | FONT_ALIGN_HCENTER)

void fontInit(void *gs);
void fontEnd(void);

// Load from file. fontSizePt: point size (0 = FONT_DEFAULT_SIZE). Returns 0 on success, FONT_ERROR on failure.
int fontLoadFile(const char *path, int fontSizePt);

// Load from memory (e.g. embedded font). Caller keeps ownership of data; it must remain valid until fontEnd or next load.
// Returns 0 on success, FONT_ERROR on failure.
int fontLoadMemory(const void *data, size_t size, int fontSizePt);

// Set glyph size from scale/PAR; call after scaleUpdate when mode or aspect changes. Flushes glyph cache.
void fontUpdateAspectRatio(void);

// Render at (x,y) in virtual coords. z: depth (larger = in front on PS2). aligned: FONT_ALIGN_*; width/height 0 = no clip. Returns last pen X (unscaled).
int fontRenderString(int id, int x, int y, short aligned, size_t width, size_t height, int z,
                     const char *string, uint64_t colour);

// Line height in virtual Y units (for vertical alignment). id must be 0. Returns 0 if font not ready.
int fontGetLineHeight(int id);

// Advance width of first line only (stops at '\n'), in native pixels. id must be 0.
int fontCalcDimensionsFirstLine(int id, const char *str);

// Total advance width in native pixels (whole string). id must be 0.
int fontCalcDimensions(int id, const char *str);

// Render string in rect [x1,y1]-[x2,y2] in virtual coords (640×480). Alignment bitmask (FONT_ALIGN_*).
// Clips to rect. Use for all scene text so layout stays resolution-independent.
int fontRenderInRect(int id, int x1, int y1, int x2, int y2, unsigned alignment, int z,
                     const char *string, uint64_t colour);

#endif
