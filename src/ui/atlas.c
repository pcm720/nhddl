// Texture atlas: packs variable-size bitmaps into a single GS texture using
// a binary-tree allocator. Used for glyph caching in the font module.
// Based on atlas implementation from Open PS2 Loader.
// Original code copyright:
// Copyright 2010, Volca

#include "ui/atlas.h"
#include <malloc.h>
#include <string.h>

// Allocates a new node in the binary-tree allocator (x,y) and size (w,h).
static struct AtlasAllocation *allocNew(size_t x, size_t y, size_t w, size_t h) {
  struct AtlasAllocation *al = malloc(sizeof(*al));
  if (!al)
    return NULL;
  al->x = x;
  al->y = y;
  al->w = w;
  al->h = h;
  al->leaf1 = NULL;
  al->leaf2 = NULL;
  return al;
}

// Frees the allocation tree recursively.
static void allocFree(struct AtlasAllocation *alloc) {
  if (!alloc)
    return;
  allocFree(alloc->leaf1);
  allocFree(alloc->leaf2);
  free(alloc);
}

#define ALLOC_FITS(a, reqW, reqH) ((a)->w >= (reqW) && (a)->h >= (reqH))
#define ALLOC_ISFREE(a) ((a)->leaf1 == NULL && (a)->leaf2 == NULL)
// ALLOC_FITS: rectangle fits in this node. ALLOC_ISFREE: node has no children (available for split).

// Tries to place a rectangle in the tree; splits free nodes to minimise fragmentation.
// Returns the node that was used for placement, or NULL if no fit.
static struct AtlasAllocation *allocPlace(struct AtlasAllocation *alloc, size_t width, size_t height) {
  if (!ALLOC_FITS(alloc, width, height))
    return NULL;

  if (ALLOC_ISFREE(alloc)) {
    size_t dx = alloc->w - width;
    size_t dy = alloc->h - height;
    // Prefer splitting so the larger remainder is in one piece (reduces fragmentation).
    if (dx < dy) {
      alloc->leaf1 = allocNew(alloc->x + width, alloc->y, dx, height);
      alloc->leaf2 = allocNew(alloc->x, alloc->y + height, alloc->w, dy);
    } else {
      alloc->leaf1 = allocNew(alloc->x, alloc->y + height, width, dy);
      alloc->leaf2 = allocNew(alloc->x + width, alloc->y, dx, alloc->h);
    }
    return alloc;
  }

  // Recurse into children; try leaf1 first, then leaf2.
  {
    struct AtlasAllocation *p = allocPlace(alloc->leaf1, width, height);
    if (p)
      return p;
    return allocPlace(alloc->leaf2, width, height);
  }
}

// Returns bytes per pixel for the given GS pixel format.
static size_t pixelSize(uint8_t psm) {
  switch (psm) {
  case GS_PSM_CT32:
  case GS_PSM_CT24:
    return 4;
  case GS_PSM_CT16:
  case GS_PSM_CT16S:
    return 2;
  case GS_PSM_T8:
    return 1;
  default:
    return 0;
  }
}

// Copies surface data into the atlas at the allocation rect (row by row).
static void atlasCopyData(Atlas *atlas, struct AtlasAllocation *al, size_t width, size_t height, const void *surface) {
  size_t ps = pixelSize(atlas->surface.PSM);
  if (!ps)
    return;
  const char *src = surface;
  char *data = (char *)atlas->surface.Mem;
  size_t stride = atlas->allocation->w;
  data += ps * (al->y * stride + al->x);
  size_t rowSize = width * ps;
  for (size_t y = 0; y < height; y++) {
    memcpy(data, src, rowSize);
    data += ps * stride; // Next row in atlas
    src += width * ps;
  }
}

// Creates a new atlas of the given size and pixel format. gs may be NULL; then invalidate/upload are no-ops.
Atlas *atlasNew(GSGLOBAL *gs, size_t width, size_t height, uint8_t psm) {
  Atlas *atlas = malloc(sizeof(*atlas));
  if (!atlas)
    return NULL;
  atlas->gs = gs;
  atlas->allocation = allocNew(0, 0, width, height);
  if (!atlas->allocation) {
    free(atlas);
    return NULL;
  }

  atlas->surface.Width = width;
  atlas->surface.Height = height;
  atlas->surface.Filter = GS_FILTER_NEAREST;
  size_t txtSize = gsKit_texture_size(width, height, psm);
  atlas->surface.PSM = psm;
  atlas->surface.Mem = memalign(128, txtSize); // 128-byte align for GS DMA
  atlas->surface.Vram = 0;
  atlas->surface.ClutPSM = 0;
  atlas->surface.Clut = NULL;
  atlas->surface.VramClut = 0;
  atlas->surface.ClutStorageMode = GS_CLUT_STORAGE_CSM1;

  if (!atlas->surface.Mem) {
    allocFree(atlas->allocation);
    free(atlas);
    return NULL;
  }
  memset(atlas->surface.Mem, 0, txtSize);
  return atlas;
}

// Frees the atlas and its texture memory; releases VRAM if gs was set.
void atlasFree(Atlas *atlas) {
  if (!atlas)
    return;
  allocFree(atlas->allocation);
  atlas->allocation = NULL;
  if (atlas->gs)
    gsKit_TexManager_free(atlas->gs, &atlas->surface);
  free(atlas->surface.Mem);
  atlas->surface.Mem = NULL;
  free(atlas);
}

// Places a bitmap in the atlas. surface is width*height pixels in atlas PSM (e.g. 8-bit for T8).
// Adds 1 pixel padding to avoid bleeding between adjacent glyphs; invalidates texture for next upload.
// Returns the allocation or NULL if no space.
struct AtlasAllocation *atlasPlace(Atlas *atlas, size_t width, size_t height, const void *surface) {
  if (!surface)
    return NULL;
  struct AtlasAllocation *al = allocPlace(atlas->allocation, width + 1, height + 1);
  if (!al)
    return NULL;
  atlasCopyData(atlas, al, width, height, surface);
  if (atlas->gs)
    gsKit_TexManager_invalidate(atlas->gs, &atlas->surface);
  return al;
}
