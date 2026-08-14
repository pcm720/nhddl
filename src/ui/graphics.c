#include "ui/graphics.h"
#include "dprintf.h"
#include "ui/dejavu_sans.h"
#include "ui/icons.h"
#include <dmaKit.h>
#include <gsKit.h>
#include <gsToolkit.h>
#include <malloc.h>
#include <png.h>
#include <stdlib.h>

// Loads 32-bit RGBA PNG texture from memory into GSTEXTURE and uploads it to GS VRAM.
int gsKit_texture_png_mem(GSGLOBAL *gsGlobal, GSTEXTURE *texture, void *buf, size_t size);

// Array of initialized GS textures containing font pages
GSTEXTURE **fontPages;
// Graphics textures
GSTEXTURE *icons;
GSTEXTURE *logo;

// Used font
const struct BMFont font = BMFONT_DEJAVU_SANS;

// UI scale factor for HD video modes: fonts and icons render at fixed pixel
// sizes tuned for 448/512-line modes and would look tiny at 720/1080 lines.
// All text/icon metrics and draws are multiplied by this factor.
static float uiScale = 1.0f;

// Sets the UI scale factor (1.0 for SD modes; >1 for HD modes)
void setUIScale(float scale) { uiScale = scale; }
float getUIScale() { return uiScale; }

// Initializes and uploads graphics resources to GS VRAM
int initGraphics() {
  if (font.pageCount == 0) {
    DPRINTF("ERROR: Invalid number of font pages\n");
    return -1;
  }
  fontPages = calloc(sizeof(GSTEXTURE *), font.pageCount);

  // Upload font pages to GS
  for (int i = 0; i < font.pageCount; i++) {
    fontPages[i] = calloc(sizeof(GSTEXTURE), 1);
    if (gsKit_texture_png_mem(gsGlobal, fontPages[i], font.pages[i].data, font.pages[i].size)) {
      DPRINTF("ERROR: Failed to load page %d\n", i);
      return -1;
    }
    // Non-integer HD scaling needs filtering. At 1.0 this remains a 1:1
    // sample, while 1.25/1.5 no longer turns the bitmap atlas into stair-steps.
    fontPages[i]->Filter = GS_FILTER_LINEAR;
  }

  // Upload icons texture to GS
  icons = calloc(sizeof(GSTEXTURE), 1);
  if (gsKit_texture_png_mem(gsGlobal, icons, ICONS_PNG, SIZE_ICONS_PNG)) {
    DPRINTF("ERROR: Failed to load icons texture\n");
    return -1;
  }
  icons->Filter = GS_FILTER_LINEAR;

  // Upload logo texture to GS
  logo = calloc(sizeof(GSTEXTURE), 1);
  if (gsKit_texture_png_mem(gsGlobal, logo, LOGO_PNG, SIZE_LOGO_PNG)) {
    DPRINTF("ERROR: Failed to load logo texture\n");
    return -1;
  }
  logo->Filter = GS_FILTER_LINEAR; // Enable bilinear filtering

  return 0;
}

// Frees memory used by font pages, logo and icon textures
void closeFont() {
  for (int i = 0; i < font.pageCount; i++) {
    free(fontPages[i]->Mem);
    free(fontPages[i]);
  }
  free(fontPages);

  free(icons->Mem);
  free(icons);
  free(logo->Mem);
  free(logo);
  return;
}

// Returns icon height
int getIconHeight(IconType iconType) { return (int)(ICONS[iconType].height * uiScale); }

// Returns icon width
int getIconWidth(IconType iconType) { return (int)(ICONS[iconType].width * uiScale); }

// Draws the icon at specified coordinates
void drawIcon(float x, float y, int z, uint64_t color, IconType iconType) {
  Icon icon = ICONS[iconType];

  // Keep the texture resident: cover art binds can evict it from VRAM,
  // and an evicted texture would otherwise draw garbage from a stale address
  gsKit_TexManager_bind(gsGlobal, icons);
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite_texture(gsGlobal, icons,               // font page
                            x,                             // x1 (destination)
                            y,                             // y1
                            icon.x,                        // u1 (source texture)
                            icon.y,                        // v1
                            x + icon.width * uiScale,      // x2 (destination)
                            y + icon.height * uiScale,     // y2
                            icon.x + icon.width + 1,       // u2 (source texture)
                            icon.y + icon.height + 1,      // v2
                            z, color);
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

// Returns logo height
int getLogoHeight() { return logo->Height; }

// Returns logo width
int getLogoWidth() { return logo->Width; }

// Draws the logo at specified coordinates
void drawLogo(float x, float y, int z) {
  // Keep the texture resident (see drawIcon)
  gsKit_TexManager_bind(gsGlobal, logo);
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);
  gsKit_prim_sprite_texture(gsGlobal, logo,   // Logo texture
                            x,                // x1 (destination)
                            y,                // y1
                            0,                // u1 (source texture)
                            0,                // v1
                            x + logo->Width,  // x2 (destination)
                            y + logo->Height, // y2
                            logo->Width + 1,  // u2 (source texture)
                            logo->Height + 1, // v2
                            z, GS_SETREG_RGBA(0x80, 0x80, 0x80, 0x80));
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
}

// Draws the icon in [x1,y1],[x2,y2] window.
void drawIconWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, IconType iconType) {
  Icon icon = ICONS[iconType];

  // Apply vertical alignment (using scaled icon dimensions)
  int iconW = getIconWidth(iconType);
  int iconH = getIconHeight(iconType);
  if (y2) {
    if (alignment & ALIGN_VCENTER) {
      y1 += ((y2 - y1) - iconH) / 2;
    } else if (alignment & ALIGN_BOTTOM) {
      y1 = y2 - iconH;
    }
  }

  // Apply horizontal alignment
  if (x2) {
    if (alignment & ALIGN_HCENTER) {
      x1 = x1 + (((x2 - x1) - iconW) / 2);
    } else if (alignment & ALIGN_RIGHT) {
      x1 = x2 - iconW;
    }
  }

  drawIcon(x1, y1, z, color, iconType);
}

// Returns line height for used font
int getFontLineHeightScaled(float scale) { return (int)(font.lineHeight * scale + 0.5f); }
uint8_t getFontLineHeight() { return (uint8_t)getFontLineHeightScaled(uiScale); }

// Returns pointer to the glyph or NULL if the font doesn't have a glyph for this character
const BMFontChar *getGlyph(uint32_t character) {
  for (int i = 0; i < font.bucketCount; i++) {
    if ((font.buckets[i].startChar <= character) && (font.buckets[i].endChar >= character)) {
      return &font.buckets[i].chars[character - font.buckets[i].startChar];
    }
  }
  return NULL;
}

// Draws glyph at specified coordinates
static void drawGlyphScaled(const BMFontChar *glyph, float x, float y, int z, uint64_t color, float scale) {
  // Keep the font page resident: cover art binds can evict it from VRAM,
  // and an evicted texture would otherwise draw garbage from a stale address
  gsKit_TexManager_bind(gsGlobal, fontPages[glyph->page]);
  gsKit_prim_sprite_texture(gsGlobal, fontPages[glyph->page],                       // font page
                            x + glyph->xoffset * scale,                             // x1 (destination)
                            y + glyph->yoffset * scale,                             // y1
                            glyph->x,                                               // u1 (source texture)
                            glyph->y,                                               // v1
                            x + (glyph->xoffset + glyph->width) * scale,            // x2 (destination)
                            y + (glyph->yoffset + glyph->height) * scale,           // y2
                            glyph->x + glyph->width + 1,                            // u2 (source texture, without +1 all characters are cut off on real hardware)
                            glyph->y + glyph->height + 1,                           // v2
                            z, color);
}

// Draws the text with specified max dimensions relative to x and y
// Returns the bottom Y coordinate of the last line that can be used to draw the next text
int drawTextScaled(int x, int y, int z, int maxWidth, int maxHeight, uint64_t color, const char *text, float scale) {
  float curX = x;
  const BMFontChar *glyph;

  // Set alpha
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);

  int curHeight = 0;
  int lineHeight = getFontLineHeightScaled(scale);
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\n') {
      curX = x;
      curHeight += lineHeight;
      continue;
    }

    glyph = getGlyph(text[i]);
    if (glyph == NULL) {
      continue;
    }

    if (maxHeight && ((curHeight + lineHeight) > maxHeight)) {
      break;
    }

    // maxWidth is an absolute right edge. Stop before any part of a glyph can
    // enter the neighboring panel instead of drawing one character too far.
    if (maxWidth && (curX + (glyph->xoffset + glyph->width) * scale > maxWidth))
      break;

    drawGlyphScaled(glyph, curX, y + curHeight, z, color, scale);
    curX += glyph->xadvance * scale;

    // Account for kerning if kernings are present and next char is not a null terminator
    if (glyph->kernings && (text[i + 1] != '\0')) {
      for (int i = 0; i < glyph->kerningsCount; i++) {
        if (glyph->kernings[i].secondChar == text[i + 1]) {
          curX += glyph->kernings[i].amount * scale;
        }
      }
    }
  }

  // Reset alpha
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

  return (y + curHeight + lineHeight);
}

int drawText(int x, int y, int z, int maxWidth, int maxHeight, uint64_t color, const char *text) {
  return drawTextScaled(x, y, z, maxWidth, maxHeight, color, text, uiScale);
}

// Gets the line width for the first line in text
float getLineWidthScaled(const char *text, float scale) {
  float lineWidth = 0;
  const BMFontChar *glyph;
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\n') {
      return lineWidth;
    }

    glyph = getGlyph(text[i]);
    if (glyph == NULL) {
      continue;
    }

    lineWidth += glyph->xadvance * scale;
    // Account for kerning
    if (glyph->kernings && (text[i + 1] != '\0')) {
      for (int i = 0; i < glyph->kerningsCount; i++) {
        if (glyph->kernings[i].secondChar == text[i + 1]) {
          lineWidth += glyph->kernings[i].amount * scale;
        }
      }
    }
  }
  return lineWidth;
}

float getLineWidth(const char *text) { return getLineWidthScaled(text, uiScale); }

// Draws the text in [x1,y1],[x2,y2] window.
// Doesn't draw the glyphs that do not fit in the set window.
// Returns the bottom Y coordinate of the last line that can be used to draw the next text.
// Use the faster drawText method if window limits are not important.
int drawTextWindowScaled(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, const char *text, float scale) {
  if (!x2 && !y2) {
    // If window limits are not set, use faster drawing function
    return drawTextScaled(x1, y1, z, 0, 0, color, text, scale);
  }
  float curX = x1;
  float curY = y1;
  int lineHeight = getFontLineHeightScaled(scale);

  // Determine text height
  int maxHeight = lineHeight;
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\n')
      maxHeight += lineHeight;
  }

  // Apply vertical alignment if text fits within set y2
  if (y2) {
    if ((alignment & ALIGN_VCENTER) && (maxHeight < (y2 - y1))) {
      curY += ((y2 - y1) - maxHeight) / 2;
    } else if ((alignment & ALIGN_BOTTOM) && (maxHeight < (y2 - y1))) {
      curY = y2 - maxHeight;
    }
  }

  // Set alpha
  gsKit_set_primalpha(gsGlobal, GS_BLEND_BACK2FRONT, 0);
  gsKit_set_test(gsGlobal, GS_ATEST_OFF);

  // Get the width of the first line
  int lineWidth = getLineWidthScaled(text, scale);
  // Determine line offset according to alignment
  if (x2) {
    if (alignment & ALIGN_HCENTER) {
      curX = x1 + (((x2 - x1) - lineWidth) / 2);
    } else if (alignment & ALIGN_RIGHT) {
      curX = x2 - lineWidth;
    }
  }

  const BMFontChar *glyph;
  for (int i = 0; text[i] != '\0'; i++) {
    if (text[i] == '\n') {
      curX = x1;
      curY += lineHeight;
      // Get the width of the next line
      lineWidth = getLineWidthScaled(&text[i + 1], scale);
      // Set line offset according to alignment
      if (x2) {
        if (alignment & ALIGN_HCENTER) {
          curX = x1 + (((x2 - x1) - lineWidth) / 2);
        } else if (alignment & ALIGN_RIGHT) {
          curX = x2 - lineWidth;
        }
      }
      continue;
    }

    glyph = getGlyph(text[i]);
    if (glyph == NULL) {
      continue;
    }

    if (y2 && ((curY + lineHeight) > y2)) {
      // If window bottom border has been reached, break
      break;
    }

    // Skip drawing glyph if doesn't fit in the window
    float glyphLeft = curX + glyph->xoffset * scale;
    float glyphRight = curX + (glyph->xoffset + glyph->width) * scale;
    if (!((curY < y1) || (glyphLeft < x1) || (x2 && (glyphRight > x2)))) {
      drawGlyphScaled(glyph, curX, curY, z, color, scale);
    }

    curX += glyph->xadvance * scale;
    // Account for kerning if kernings are present and next char is not a null terminator
    if (glyph->kernings && (text[i + 1] != '\0')) {
      for (int i = 0; i < glyph->kerningsCount; i++) {
        if (glyph->kernings[i].secondChar == text[i + 1]) {
          curX += glyph->kernings[i].amount * scale;
        }
      }
    }
  }

  // Reset alpha
  gsKit_set_test(gsGlobal, GS_ATEST_ON);
  gsKit_set_primalpha(gsGlobal, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);

  return curY + lineHeight;
}

int drawTextWindow(int x1, int y1, int x2, int y2, int z, uint64_t color, uint8_t alignment, const char *text) {
  return drawTextWindowScaled(x1, y1, x2, y2, z, color, alignment, text, uiScale);
}

// Loads 32-bit RGBA PNG texture from memory into GSTEXTURE and uploads it to GS VRAM.
// Code based on gsToolkit.
int gsKit_texture_png_mem(GSGLOBAL *gsGlobal, GSTEXTURE *texture, void *buf, size_t size) {
  FILE *file = fmemopen(buf, size, "rb");
  if (file == NULL) {
    DPRINTF("ERROR: Failed to load PNG file\n");
    return -1;
  }

  png_structp png_ptr;
  png_infop info_ptr;
  png_uint_32 width, height;
  png_bytep *row_pointers;

  uint32_t sig_read = 0;
  int row, i, k = 0, j, bit_depth, color_type, interlace_type;

  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);

  if (!png_ptr) {
    DPRINTF("ERROR: Failed to init libpng read struct\n");
    fclose(file);
    return -1;
  }

  info_ptr = png_create_info_struct(png_ptr);

  if (!info_ptr) {
    DPRINTF("ERROR: Failed to init libpng info struct\n");
    fclose(file);
    png_destroy_read_struct(&png_ptr, (png_infopp)NULL, (png_infopp)NULL);
    return -1;
  }

  if (setjmp(png_jmpbuf(png_ptr))) {
    DPRINTF("ERROR: Failed to setup libpng long jump\n");
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    fclose(file);
    return -1;
  }

  png_init_io(png_ptr, file);
  png_set_sig_bytes(png_ptr, sig_read);
  png_read_info(png_ptr, info_ptr);
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

  if (color_type != PNG_COLOR_TYPE_RGB_ALPHA) {
    DPRINTF("ERROR: Only 32-bit RGBA textures are supported\n");
    png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
    fclose(file);
    return -1;
  }

  if (bit_depth == 16)
    png_set_strip_16(png_ptr);

  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(png_ptr);

  png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
  png_read_update_info(png_ptr, info_ptr);

  texture->Width = width;
  texture->Height = height;
  texture->VramClut = 0;
  texture->Clut = NULL;
  texture->PSM = GS_PSM_CT32;
  texture->Filter = GS_FILTER_NEAREST;
  texture->Mem = memalign(128, gsKit_texture_size(texture->Width, texture->Height, texture->PSM));

  int row_bytes = png_get_rowbytes(png_ptr, info_ptr);
  row_pointers = calloc(height, sizeof(png_bytep));
  for (row = 0; row < height; row++)
    row_pointers[row] = malloc(row_bytes);

  png_read_image(png_ptr, row_pointers);

  struct pixel {
    uint8_t r, g, b, a;
  };
  struct pixel *pixels = (struct pixel *)texture->Mem;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      pixels[k].r = row_pointers[i][4 * j];
      pixels[k].g = row_pointers[i][4 * j + 1];
      pixels[k].b = row_pointers[i][4 * j + 2];
      pixels[k++].a = 128 - ((int)row_pointers[i][4 * j + 3] * 128 / 255);
    }
  }

  for (row = 0; row < height; row++)
    free(row_pointers[row]);

  free(row_pointers);
  png_read_end(png_ptr, NULL);
  png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
  fclose(file);

  // Upload texture to GS
  gsKit_TexManager_bind(gsGlobal, texture);

  return 0;
}
