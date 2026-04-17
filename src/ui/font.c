// FreeType atlas-based font rendering
// Based on implementation from Open PS2 Loader.
// Original code copyright:
// Copyright 2010, Volca

#include "ui/font.h"
#include "ui/atlas.h"
#include "ui/scale.h"
#include "ui/utf8.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <gsKit.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/fcntl.h>
#include <unistd.h>

// Atlas and glyph cache layout.
#define ATLAS_MAX 4
#define ATLAS_W 256
#define ATLAS_H 256
#define GLYPH_PAGE 256
// DPI for FT_Set_Char_Size resolution.
#define FONT_DPI 72.0f

static GSGLOBAL *fontGs;
static FT_Library ftLib;

// CLUT for 8-bit alpha texture: index -> RGBA (white, alpha). Shared by all atlases.
typedef struct {
  uint8_t psm;
  uint8_t clutPsm;
  uint32_t *clut;
  uint32_t vramClut;
} FontClut;
static FontClut fontClut;

// Single glyph cache entry: bitmap size, pen offset, advance, and atlas placement.
typedef struct {
  int valid;
  int width, height;
  int ox, oy;   // Offset from pen (bitmap_left, -bitmap_top)
  int shx, shy; // Advance in 26.6 format
  Atlas *atlas;
  struct AtlasAllocation *allocation;
} FontGlyph;

// Font slot: FreeType face, glyph cache (pages by codepoint range), and atlases.
typedef struct {
  FontGlyph **glyphCache;
  int cacheMaxPage;
  FT_Face face;
  int valid;
  int fontSizePt; // Point size; used by fontUpdateAspectRatio and baseline/line height.
  Atlas *atlases[ATLAS_MAX];
  void *dataPtr; // TTF file buffer when loaded from file (or pointer to embedded data)
  int dataOwned; // 1 if dataPtr was allocated by us (file load), 0 if external (e.g. embedded)
} FontSlot;

static FontSlot font[2];

#define GLYPH_PAGE_OK(slot, page) ((page) <= (slot)->cacheMaxPage && (slot)->glyphCache[page])

static int minInt(int a, int b) { return a < b ? a : b; }

// Returns next UTF-8 codepoint and advances *s. Uses ui/utf8.h; invalid bytes are skipped.
static uint32_t utf8Next(const char **s) {
  const char *p = *s;
  if (!*p)
    return 0;
  uint32_t state = UTF8_ACCEPT;
  uint32_t cp = 0;
  for (;;) {
    uint32_t r = utf8Decode(&state, &cp, *p);
    p++;
    if (state == UTF8_ACCEPT) {
      *s = p;
      return cp;
    }
    if (state == UTF8_REJECT)
      state = UTF8_ACCEPT;
    if (!*p) {
      *s = p;
      return 0;
    }
  }
}

// Invalidates one glyph cache page (marks all entries as unused).
static void fontCacheFlushPage(FontGlyph *page) {
  for (int i = 0; i < GLYPH_PAGE; i++, page++) {
    page->valid = 0;
    page->allocation = NULL;
    page->atlas = NULL;
  }
}

// Frees all glyph cache pages and atlases for the slot.
static void fontCacheFlush(FontSlot *slot) {
  for (int i = 0; i <= slot->cacheMaxPage; i++) {
    if (slot->glyphCache[i]) {
      fontCacheFlushPage(slot->glyphCache[i]);
      free(slot->glyphCache[i]);
      slot->glyphCache[i] = NULL;
    }
  }
  free(slot->glyphCache);
  slot->glyphCache = NULL;
  slot->cacheMaxPage = -1;
  for (int i = 0; i < ATLAS_MAX; i++) {
    atlasFree(slot->atlases[i]);
    slot->atlases[i] = NULL;
  }
}

// Ensures the glyph cache has a page for the given page id; allocates it if needed.
static int fontPreparePage(FontSlot *slot, int pageId) {
  if (pageId > slot->cacheMaxPage) {
    size_t n = (size_t)(pageId + 1) * sizeof(FontGlyph *);
    FontGlyph **np = realloc(slot->glyphCache, n);
    if (!np)
      return 0;
    slot->glyphCache = np;
    for (int p = slot->cacheMaxPage + 1; p <= pageId; p++)
      slot->glyphCache[p] = NULL;
    slot->cacheMaxPage = pageId;
  }
  if (slot->glyphCache[pageId])
    return 1;
  slot->glyphCache[pageId] = calloc(GLYPH_PAGE, sizeof(FontGlyph));
  return slot->glyphCache[pageId] ? 1 : 0;
}

// Builds the shared CLUT. FreeType bitmap: 0 = background, 255 = glyph (same as OPL fntPrepareCLUT).
static void fontPrepareClut(void) {
  fontClut.psm = GS_PSM_T8;
  fontClut.clutPsm = GS_PSM_CT32;
  fontClut.clut = memalign(128, 256 * 4);
  fontClut.vramClut = 0;
  if (!fontClut.clut)
    return;
  for (size_t i = 0; i < 256; i++) {
    unsigned int a = (unsigned int)(i * 128 / 255);
    fontClut.clut[i] = GS_SETREG_RGBA(0xFF, 0xFF, 0xFF, (uint8_t)a);
  }
}

// Frees the shared CLUT.
static void fontDestroyClut(void) {
  if (fontClut.clut) {
    free(fontClut.clut);
    fontClut.clut = NULL;
  }
}

// Resets a font slot to empty state (no face, no cache, no atlases).
static void fontInitSlot(FontSlot *slot) {
  slot->face = NULL;
  slot->glyphCache = NULL;
  slot->cacheMaxPage = -1;
  slot->fontSizePt = 0;
  slot->dataPtr = NULL;
  slot->dataOwned = 0;
  slot->valid = 0;
  for (int i = 0; i < ATLAS_MAX; i++)
    slot->atlases[i] = NULL;
}

// Frees all resources owned by the font slot and resets it.
static void fontDeleteSlot(FontSlot *slot) {
  fontCacheFlush(slot);
  if (slot->face)
    FT_Done_Face(slot->face);
  slot->face = NULL;
  if (slot->dataPtr && slot->dataOwned)
    free(slot->dataPtr);
  slot->dataPtr = NULL;
  slot->dataOwned = 0;
  slot->valid = 0;
}

// Allocates a new atlas for glyphs and wires it to the shared CLUT.
static Atlas *fontNewAtlas(void) {
  Atlas *atl = atlasNew(fontGs, ATLAS_W, ATLAS_H, GS_PSM_T8);
  if (!atl || !atl->surface.Mem)
    return atl;
  atl->surface.ClutPSM = GS_PSM_CT32;
  atl->surface.Clut = (u32 *)fontClut.clut;
  atl->surface.ClutStorageMode = GS_CLUT_STORAGE_CSM1;
  return atl;
}

// Tries to place the glyph bitmap from the FreeType slot into one of the font's atlases. Empty glyphs = success.
static int fontGlyphPlace(FontSlot *slot, FontGlyph *glyph, FT_GlyphSlot ftSlot) {
  if (!ftSlot || (ftSlot->bitmap.width == 0 && ftSlot->bitmap.rows == 0))
    return 1;
  for (int i = 0; i < ATLAS_MAX; i++) {
    if (!slot->atlases[i]) {
      slot->atlases[i] = fontNewAtlas();
      if (!slot->atlases[i] || !slot->atlases[i]->surface.Mem)
        return 0;
    }
    glyph->allocation = atlasPlace(slot->atlases[i], (size_t)ftSlot->bitmap.width, (size_t)ftSlot->bitmap.rows, ftSlot->bitmap.buffer);
    if (glyph->allocation) {
      glyph->atlas = slot->atlases[i];
      return 1;
    }
  }
  return 0;
}

// Ensures the codepoint is rendered and placed in an atlas; returns the cache entry or NULL.
static FontGlyph *fontCacheGlyph(FontSlot *slot, uint32_t codepoint) {
  int pageId = (int)(codepoint / GLYPH_PAGE);
  int idx = (int)(codepoint % GLYPH_PAGE);
  if (!GLYPH_PAGE_OK(slot, pageId) && !fontPreparePage(slot, pageId))
    return NULL;
  FontGlyph *glyph = &slot->glyphCache[pageId][idx];
  if (glyph->valid)
    return glyph;
  if (!slot->face)
    return NULL;
  if (FT_Load_Char(slot->face, codepoint, FT_LOAD_RENDER) != 0)
    return NULL;
  FT_GlyphSlot ftSlot = slot->face->glyph;
  if (!ftSlot)
    return NULL;
  if (!fontGlyphPlace(slot, glyph, ftSlot))
    return NULL;
  glyph->width = ftSlot->bitmap.width;
  glyph->height = ftSlot->bitmap.rows;
  glyph->shx = ftSlot->advance.x;
  glyph->shy = ftSlot->advance.y;
  glyph->ox = ftSlot->bitmap_left;
  glyph->oy = -ftSlot->bitmap_top;
  glyph->valid = 1;
  return glyph;
}

// Draws one textured quad (glyph) with gsKit. -0.5f for pixel centering.
static void fontDrawQuad(GSTEXTURE *txt, float ulX, float ulY, float ulU, float ulV, float brX, float brY, float brU, float brV, int z,
                         uint64_t color) {
  if (!fontGs || !txt || !txt->Mem)
    return;
  gsKit_TexManager_bind(fontGs, txt);
  gsKit_prim_sprite_texture(fontGs, txt, ulX - 0.5f, ulY - 0.5f, ulU, ulV, brX - 0.5f, brY - 0.5f, brU, brV, z, color);
}

// Draws one glyph. Integer position; quad size = bitmap size (rasterized with PAR via FT_Set_Char_Size).
static void fontRenderGlyph(FontGlyph *glyph, int penX, int penY, int z, uint64_t color) {
  if (!glyph->allocation || !glyph->atlas)
    return;
  int ulX = penX + glyph->ox;
  int brX = ulX + glyph->width;
  int ulY, brY;
  if (scaleGetInterlacedFrame()) {
    ulY = penY + glyph->oy / 2; // Interlaced: glyph height halved
    brY = ulY + glyph->height / 2;
  } else {
    ulY = penY + glyph->oy;
    brY = ulY + glyph->height;
  }
  float ulU = (float)glyph->allocation->x;
  float ulV = (float)glyph->allocation->y;
  fontDrawQuad(&glyph->atlas->surface, (float)ulX, (float)ulY, ulU, ulV, (float)brX, (float)brY, ulU + (float)glyph->width,
               ulV + (float)glyph->height, z, color);
}

// Initializes FreeType, the shared CLUT, and the single font slot.
void fontInit(void *gs) {
  fontGs = gs;
  if (FT_Init_FreeType(&ftLib) != 0)
    return;
  fontPrepareClut();
  fontInitSlot(&font[0]);
  fontInitSlot(&font[1]);
}

// Frees the font slot, shuts down FreeType and the shared CLUT.
void fontEnd(void) {
  fontInitSlot(&font[0]);
  fontInitSlot(&font[1]);
  FT_Done_FreeType(ftLib);
  fontDestroyClut();
  fontGs = NULL;
}

// Load from memory (e.g. embedded font). Caller keeps ownership of data.
int fontLoadMemory(const void *data, size_t size, int fontSizePt) {
  if (!data || size == 0)
    return FONT_ERROR;
  fontInitSlot(&font[0]);
  fontInitSlot(&font[1]);
  //
  // Main font
  //
  font[0].dataPtr = (void *)data;
  font[0].dataOwned = 1;
  if (FT_New_Memory_Face(ftLib, (const FT_Byte *)data, (FT_Long)size, 0, &font[0].face) != 0) {
    fontDeleteSlot(&font[0]);
    return FONT_ERROR;
  }
  font[0].valid = 1;
  font[0].fontSizePt = (fontSizePt > 0) ? fontSizePt : FONT_DEFAULT_SIZE;
  // Default Char_Size; fontUpdateAspectRatio() sets correct size from scale module.
  FT_Set_Char_Size(font[0].face, (FT_F26Dot6)(font[0].fontSizePt * 64), (FT_F26Dot6)(font[0].fontSizePt * 64), (FT_UInt)FONT_DPI, (FT_UInt)FONT_DPI);
  //
  // Prompt font
  //
  font[1].dataPtr = (void *)data;
  font[1].dataOwned = 1;
  if (FT_New_Memory_Face(ftLib, (const FT_Byte *)data, (FT_Long)size, 0, &font[1].face) != 0) {
    fontDeleteSlot(&font[1]);
    return FONT_ERROR;
  }
  font[1].valid = 1;
  font[1].fontSizePt = font[0].fontSizePt - 2;
  // Default Char_Size; fontUpdateAspectRatio() sets correct size from scale module.
  FT_Set_Char_Size(font[0].face, (FT_F26Dot6)(font[0].fontSizePt * 64), (FT_F26Dot6)(font[0].fontSizePt * 64), (FT_UInt)FONT_DPI, (FT_UInt)FONT_DPI);
  return 0;
}

// Loads a TTF from path into the single slot. fontSizePt: point size (0 = FONT_DEFAULT_SIZE). Returns 0 on success.
int fontLoadFile(const char *path, int fontSizePt) {
  int fd = open(path, O_RDONLY, 0666);
  if (fd < 0)
    return FONT_ERROR;
  long sz = lseek(fd, 0, SEEK_END);
  lseek(fd, 0, SEEK_SET);
  if (sz <= 0) {
    close(fd);
    return FONT_ERROR;
  }
  void *buf = malloc((size_t)sz);
  if (!buf) {
    close(fd);
    return FONT_ERROR;
  }
  if (read(fd, buf, (size_t)sz) != (ssize_t)sz) {
    free(buf);
    close(fd);
    return FONT_ERROR;
  }
  close(fd);
  return fontLoadMemory(buf, sz, fontSizePt);
}

// Set glyph size from scale (FT_Set_Char_Size with PAR in DPI). DPI truncated for integer scaling.
void fontUpdateAspectRatio(void) {
  if (!font[0].valid || !font[0].face || font[0].fontSizePt <= 0)
    return;
  float hs = scaleGetScaleY();
  float ws = hs * scaleGetPAR();
  if (scaleGetInterlacedFrame())
    hs *= 2.0f;
  FT_UInt horz = (FT_UInt)(FONT_DPI * ws);
  FT_UInt vert = (FT_UInt)(FONT_DPI * hs);
  if (horz < 1)
    horz = 1;
  if (vert < 1)
    vert = 1;
  // Glyph bitmaps depend on DPI; must re-rasterize
  fontCacheFlush(&font[0]);
  FT_Set_Char_Size(font[0].face, (FT_F26Dot6)(font[0].fontSizePt * 64), (FT_F26Dot6)(font[0].fontSizePt * 64), horz, vert);
  fontCacheFlush(&font[1]);
  FT_Set_Char_Size(font[1].face, (FT_F26Dot6)(font[1].fontSizePt * 64), (FT_F26Dot6)(font[1].fontSizePt * 64), horz, vert);
}

// Renders string at (x,y). Integer native coords via scaleScaleX/Y; id must be FONT_DEFAULT or FONT_PROMPT. z: depth (larger = in front on PS2).
int fontRenderString(int id, int x, int y, short aligned, size_t width, size_t height, int z, const char *string, uint64_t colour) {
  if (id < 0 || id > sizeof(font) / sizeof(FontSlot) || !font[id].valid)
    return 0;
  FontSlot *slot = &font[id];

  int xNat = scaleScaleX(x) + scaleGetOffsetX();
  int yNat = scaleScaleY(y) + scaleGetOffsetY();
  int wNat = width ? scaleScaleX((int)width) : 0;
  int hNat = height ? scaleScaleY((int)height) : 0;

  if (aligned & FONT_ALIGN_HCENTER) {
    int strW = fontCalcDimensions(0, string);
    int clipW = width ? scaleScaleX((int)width) : strW;
    xNat -= minInt(strW, clipW) / 2;
  }
  if (aligned & FONT_ALIGN_VCENTER)
    yNat += scaleScaleY(slot->fontSizePt - 4) / 2;
  else
    yNat += scaleScaleY(slot->fontSizePt + 2);

  int penX = xNat;
  int xmax = width ? xNat + wNat : 0x7fffffff;
  int ymax = height ? yNat + hNat : 0x7fffffff;
  int useKerning = FT_HAS_KERNING(slot->face);
  FT_UInt prevIndex = 0;

  const char *s = string;
  uint32_t cp;
  while ((cp = utf8Next(&s)) != 0) {
    FontGlyph *glyph = fontCacheGlyph(slot, cp);
    if (!glyph)
      continue;
    if (useKerning && prevIndex) {
      FT_Vector delta;
      FT_UInt idx = FT_Get_Char_Index(slot->face, cp);
      if (idx && FT_Get_Kerning(slot->face, prevIndex, idx, FT_KERNING_DEFAULT, &delta) == 0)
        penX += delta.x >> 6;
      prevIndex = idx;
    } else {
      prevIndex = FT_Get_Char_Index(slot->face, cp);
    }
    if (width) {
      if (cp == '\n') {
        penX = xNat;
        yNat += scaleScaleY(slot->fontSizePt + 2);
        continue;
      }
      if (yNat > ymax)
        break;
      if (penX + glyph->width > xmax)
        continue;
    }
    fontRenderGlyph(glyph, penX, yNat, z, colour);
    penX += glyph->shx >> 6;
  }
  return scaleUnscaleX(penX - scaleGetOffsetX());
}

// Returns total advance width in native pixels (rasterization already has PAR). id must be FONT_DEFAULT or FONT_PROMPT.
int fontCalcDimensions(int id, const char *str) {
  int w = 0;
  if (id < 0 || id > sizeof(font) / sizeof(FontSlot) || !font[id].valid)
    return 0;
  FontSlot *slot = &font[id];
  int useKerning = FT_HAS_KERNING(slot->face);
  FT_UInt prevIndex = 0;
  const char *s = str;
  uint32_t cp;
  while ((cp = utf8Next(&s)) != 0) {
    FontGlyph *glyph = fontCacheGlyph(slot, cp);
    if (!glyph)
      continue;
    if (useKerning && prevIndex) {
      FT_Vector delta;
      FT_UInt idx = FT_Get_Char_Index(slot->face, cp);
      if (idx && FT_Get_Kerning(slot->face, prevIndex, idx, FT_KERNING_DEFAULT, &delta) == 0)
        w += delta.x >> 6;
      prevIndex = idx;
    } else {
      prevIndex = FT_Get_Char_Index(slot->face, cp);
    }
    w += glyph->shx >> 6;
  }
  return w;
}

// Line height in virtual Y units (matches offset used in fontRenderString). id must be FONT_DEFAULT or FONT_PROMPT.
int fontGetLineHeight(int id) {
  if (id < 0 || id > sizeof(font) / sizeof(FontSlot) || !font[id].valid)
    return 0;
  return font[id].fontSizePt + 2;
}

// Advance width of first line only (stops at '\n'), in native pixels.
int fontCalcDimensionsFirstLine(int id, const char *str) {
  int w = 0;
  if (id < 0 || id > sizeof(font) / sizeof(FontSlot) || !font[id].valid)
    return 0;
  FontSlot *slot = &font[id];
  int useKerning = FT_HAS_KERNING(slot->face);
  FT_UInt prevIndex = 0;
  const char *s = str;
  uint32_t cp;
  while ((cp = utf8Next(&s)) != 0) {
    if (cp == '\n')
      break;
    FontGlyph *glyph = fontCacheGlyph(slot, cp);
    if (!glyph)
      continue;
    if (useKerning && prevIndex) {
      FT_Vector delta;
      FT_UInt idx = FT_Get_Char_Index(slot->face, cp);
      if (idx && FT_Get_Kerning(slot->face, prevIndex, idx, FT_KERNING_DEFAULT, &delta) == 0)
        w += delta.x >> 6;
      prevIndex = idx;
    } else {
      prevIndex = FT_Get_Char_Index(slot->face, cp);
    }
    w += glyph->shx >> 6;
  }
  return w;
}

// Render string in rect [x1,y1]-[x2,y2] in virtual coords (640×480). Alignment bitmask. Clips to rect.
int fontRenderInRect(int id, int x1, int y1, int x2, int y2, unsigned alignment, int z, const char *string, uint64_t colour) {
  if (id < 0 || id > sizeof(font) / sizeof(FontSlot) || !font[id].valid)
    return 0;
  int lineH = fontGetLineHeight(id);
  int firstLineW_nat = fontCalcDimensionsFirstLine(id, string);
  int firstLineW_virtual = scaleUnscaleX(firstLineW_nat);
  int rectW = x2 - x1;
  int rectH = y2 - y1;
  int x = x1;
  int y = y1;
  if (rectW > 0) {
    if (alignment & FONT_ALIGN_HCENTER)
      x = x1 + (rectW - firstLineW_virtual) / 2;
    else if (alignment & FONT_ALIGN_RIGHT)
      x = x2 - firstLineW_virtual;
  }
  if (rectH > 0) {
    if (alignment & FONT_ALIGN_VCENTER)
      y = y1 + (rectH - lineH) / 2;
    else if (alignment & FONT_ALIGN_BOTTOM)
      y = y2 - lineH;
  }
  size_t clipW = (rectW > 0) ? (size_t)rectW : 0;
  size_t clipH = (rectH > 0) ? (size_t)rectH : 0;
  return fontRenderString(id, x, y, 0, clipW, clipH, z, string, colour);
}
