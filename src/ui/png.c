#include "ui/png.h"
#include "dprintf.h"
#include <gsKit.h>
#include <gsToolkit.h>
#include <malloc.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>

// Loads PNG into GSTEXTURE and uploads to GS VRAM.
// Supports 8-bit palettized and 32-bit RGBA
int loadPNG(GSGLOBAL *gs, GSTEXTURE *texture, FILE *file) {
  if (!file || !gs || !texture)
    return -1;

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!png_ptr)
    return -1;

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if (!info_ptr) {
    png_destroy_read_struct(&png_ptr, NULL, NULL);
    return -1;
  }
  if (setjmp(png_jmpbuf(png_ptr))) {
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return -1;
  }

  png_init_io(png_ptr, file);
  png_set_sig_bytes(png_ptr, 0);
  png_read_info(png_ptr, info_ptr);

  png_uint_32 width, height;
  int bit_depth, color_type, interlace_type;
  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);

  texture->Width = width;
  texture->Height = height;
  texture->VramClut = 0;
  texture->Clut = NULL;
  texture->PSM = GS_PSM_CT32;
  struct pixel {
    uint8_t r, g, b, a;
  };
  struct pixel *pixels = NULL;
  png_bytep *row_pointers = NULL;
  int row_bytes = 0;

  switch (color_type) {
  case PNG_COLOR_TYPE_RGB_ALPHA:
    // 32-bit RGBA path
    if (bit_depth == 16)
      png_set_strip_16(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
      png_set_tRNS_to_alpha(png_ptr);
    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(png_ptr, info_ptr);

    texture->Mem = memalign(128, gsKit_texture_size(width, height, texture->PSM));
    if (!texture->Mem) {
      png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
      return -1;
    }

    row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    png_bytep *row_pointers = calloc(height, sizeof(png_bytep));
    if (!row_pointers) {
      free(texture->Mem);
      texture->Mem = NULL;
      png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
      return -1;
    }
    for (png_uint_32 row = 0; row < height; row++)
      row_pointers[row] = malloc(row_bytes);
    png_read_image(png_ptr, row_pointers);

    pixels = (struct pixel *)texture->Mem;
    int k = 0;
    for (png_uint_32 i = 0; i < height; i++) {
      for (png_uint_32 j = 0; j < width; j++) {
        pixels[k].r = row_pointers[i][4 * j];
        pixels[k].g = row_pointers[i][4 * j + 1];
        pixels[k].b = row_pointers[i][4 * j + 2];
        pixels[k].a = (uint8_t)((int)row_pointers[i][4 * j + 3] * 128 / 255);
        k++;
      }
    }

    for (png_uint_32 row = 0; row < height; row++)
      free(row_pointers[row]);
    free(row_pointers);
    break;
  case PNG_COLOR_TYPE_PALETTE:
    // 8-bit palettized path
    if (bit_depth != 8) {
      DPRINTF("ui/png: only 8-bit palettized are supported\n");
      png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
      return -1;
    }

    png_colorp palette = NULL;
    int num_palette = 0;
    png_bytep trans = NULL;
    int num_trans = 0;
    if (!png_get_PLTE(png_ptr, info_ptr, &palette, &num_palette) || num_palette <= 0) {
      DPRINTF("ui/png: missing or empty PLTE\n");
      png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
      return -1;
    }
    png_get_tRNS(png_ptr, info_ptr, &trans, &num_trans, NULL);

    texture->Mem = memalign(128, gsKit_texture_size(width, height, texture->PSM));
    if (!texture->Mem) {
      png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
      return -1;
    }

    row_bytes = png_get_rowbytes(png_ptr, info_ptr);
    row_pointers = calloc(height, sizeof(png_bytep));
    if (!row_pointers) {
      free(texture->Mem);
      texture->Mem = NULL;
      png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
      return -1;
    }
    for (png_uint_32 row = 0; row < height; row++)
      row_pointers[row] = malloc(row_bytes);
    png_read_image(png_ptr, row_pointers);

    pixels = (struct pixel *)texture->Mem;
    for (png_uint_32 i = 0; i < height; i++) {
      png_bytep row = row_pointers[i];
      for (png_uint_32 j = 0; j < width; j++) {
        int idx = row[j];
        pixels->r = idx < num_palette ? palette[idx].red : 0;
        pixels->g = idx < num_palette ? palette[idx].green : 0;
        pixels->b = idx < num_palette ? palette[idx].blue : 0;
        if (idx < num_trans)
          pixels->a = (uint8_t)((int)trans[idx] * 128 / 255);
        else
          pixels->a = 0;
        pixels++;
      }
    }

    for (png_uint_32 row = 0; row < height; row++)
      free(row_pointers[row]);
    free(row_pointers);
    break;
  default:
    DPRINTF("ui/png: only 8-bit palettized or 32-bit RGBA PNG are supported\n");
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return -1;
  }

  png_read_end(png_ptr, NULL);
  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

  gsKit_TexManager_bind(gs, texture);
  return 0;
}

// Loads PNG from file into GSTEXTURE and uploads to GS VRAM.
// Supports 8-bit palettized and 32-bit RGBA
int loadPNGFromFile(GSGLOBAL *gs, GSTEXTURE *texture, char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    DPRINTF("ui/png: failed to open %s\n", path);
    return -1;
  }
  int res = loadPNG(gs, texture, file);
  fclose(file);
  if (res)
    DPRINTF("ui/png: failed to load %s: %d\n", path, res);
  return res;
}

// Loads PNG from memory into GSTEXTURE and uploads to GS VRAM.
// Supports 8-bit palettized (icons) and 32-bit RGBA (e.g. splash logo)
int loadPNGFromMemory(GSGLOBAL *gs, GSTEXTURE *texture, void *buf, size_t size) {
  FILE *file = fmemopen(buf, size, "rb");
  if (!file) {
    DPRINTF("ui/png: failed to fmemopen file at %p (size %d)\n", buf, size);
    return -1;
  }
  int res = loadPNG(gs, texture, file);
  fclose(file);
  if (res)
    DPRINTF("ui/png: failed to load file at %p (size %d): %d\n", buf, size, res);
  return res;
}
