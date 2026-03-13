#ifndef _UI_ATLAS_H_
#define _UI_ATLAS_H_

// Texture atlas: packs variable-size bitmaps into a single GS texture.
// Used by the font module for glyph caching. See atlas.c for implementation.

#include <gsKit.h>
#include <stddef.h>
#include <stdint.h>

// Node in the binary-tree allocator; describes a rectangle and optional children.
struct AtlasAllocation {
  size_t x, y, w, h;
  struct AtlasAllocation *leaf1;
  struct AtlasAllocation *leaf2;
};

// Atlas state: GS context, root allocation, and the packed texture surface.
typedef struct {
  GSGLOBAL *gs;
  struct AtlasAllocation *allocation;
  GSTEXTURE surface;
} Atlas;

// Creates a new atlas. gs may be NULL until first draw; invalidate/upload are then no-ops.
Atlas *atlasNew(GSGLOBAL *gs, size_t width, size_t height, uint8_t psm);
// Frees the atlas and its texture memory.
void atlasFree(Atlas *atlas);

// Places a bitmap in the atlas (adds 1 pixel padding). surface is width*height pixels in atlas PSM (e.g. 8-bit for T8). Returns allocation or NULL if no space.
struct AtlasAllocation *atlasPlace(Atlas *atlas, size_t width, size_t height, const void *surface);

#endif
