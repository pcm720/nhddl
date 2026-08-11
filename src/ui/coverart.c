#include "ui/coverart.h"
#include "devices/devices.h"
#include "dprintf.h"
#include <gsKit.h>
#include <kernel.h>
#include <malloc.h>
#include <png.h>
#include <ps2sdkapi.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Decoded texture cache size. A 140x200 cover is ~28 KB as 8-bit palette or
// ~112 KB as CT32, so even the worst case stays under 2 MB of EE RAM.
#define COVER_CACHE_SLOTS 16
// Upper bound on total decoded texture memory. Oversized cover art (full-size
// scans) would otherwise multiply across cache slots and exhaust EE RAM.
#define COVER_CACHE_MAX_BYTES (4 * 1024 * 1024)
// Reject absurdly large cover art outright (the UI draws covers at 140x200)
#define COVER_MAX_DIMENSION 512
// How many neighbors of the selected title to prefetch in each direction
#define COVER_PREFETCH 2
// Maximum queued load requests (selected + prefetched neighbors)
#define COVER_QUEUE_SIZE (2 * COVER_PREFETCH + 1)
// Title ID buffer size
#define COVER_ID_LEN 16

#define WORKER_STACK_SIZE 0x8000
#define WORKER_PRIORITY 0x12

// Path relative to storage device mountpoint (same as the old loader used)
static const char coverArtDir[] = "/ART";

typedef enum {
  SLOT_EMPTY = 0, // Free slot
  SLOT_QUEUED,    // Requested, not yet picked up by the worker
  SLOT_LOADING,   // Worker is reading/decoding this cover
  SLOT_READY,     // Texture decoded and usable
  SLOT_FAILED,    // Load failed (missing art); cached to avoid retrying
} SlotState;

typedef struct {
  char id[COVER_ID_LEN];          // Title ID this slot holds art for
  struct DeviceMapEntry *device;  // Device the art was requested from
  SlotState state;
  uint32_t lastUse;               // LRU tick of last request/draw
  uint32_t memBytes;              // Decoded texture memory (Mem + Clut)
  GSTEXTURE tex;                  // Decoded texture (Mem/Clut owned by slot)
} CoverSlot;

typedef struct {
  CoverSlot *slot;                // Slot reserved for this request
  char id[COVER_ID_LEN];          // Title ID (validated against slot on pickup)
  struct DeviceMapEntry *device;  // Device to load from
} CoverRequest;

static CoverSlot cache[COVER_CACHE_SLOTS];
static CoverRequest queue[COVER_QUEUE_SIZE];
static int queueCount = 0;

static uint32_t lruTick = 0;
static uint32_t cacheBytes = 0; // Total decoded texture memory in the cache
static char lastRequestedID[COVER_ID_LEN] = {0};
static struct DeviceMapEntry *lastRequestedDevice = NULL;
static const char *statusText = "";

// Worker thread state
static int32_t mutexSema = -1;  // Protects cache, queue and flags
static int32_t workSema = -1;   // Signaled when there is work (or state changed)
static int32_t ackSema = -1;    // Signals pause ack / worker exit
static int32_t workerThreadID = -1;
static int workerBusy = 0;      // Worker is processing a request
static int pauseRequested = 0;  // Worker must idle
static int shutdownRequested = 0;
static int needAck = 0;         // Main thread waits on ackSema
static uint8_t workerStack[WORKER_STACK_SIZE] __attribute__((aligned(16)));

static void coverArtWorker();

//
// PNG decoding (worker thread)
//
// This replicates gsKit_texture_png from gsKit's gsToolkit.c exactly
// (including the CSM1 CLUT rotation and PS2 alpha conversion), but decodes
// into plain EE RAM without touching any gsKit state, making it safe to run
// off the main thread. VRAM upload happens later via gsKit_TexManager_bind.
//

// Matches gsKit_texture_size_ee for the PSMs used below
static uint32_t textureSizeEE(int width, int height, int psm) {
  switch (psm) {
  case GS_PSM_CT32:
  case GS_PSM_CT24:
    return (width * height * 4);
  case GS_PSM_T8:
    return (width * height);
  case GS_PSM_T4:
    return (width * height / 2);
  }
  return 0;
}

// Total EE RAM held by a decoded texture (pixel data + CLUT)
static uint32_t textureMemBytes(const GSTEXTURE *tex) {
  uint32_t bytes = textureSizeEE(tex->Width, tex->Height, tex->PSM);
  if (tex->Clut)
    bytes += (tex->PSM == GS_PSM_T8) ? (256 * 4) : (16 * 4);
  return bytes;
}

// Decodes a PNG file into tex->Mem/tex->Clut. Returns 0 on success.
// Only touches the passed texture struct and the heap.
static int decodePNG(GSTEXTURE *tex, const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL)
    return -1;

  png_structp pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if (!pngPtr) {
    fclose(file);
    return -1;
  }
  png_infop infoPtr = png_create_info_struct(pngPtr);
  if (!infoPtr) {
    png_destroy_read_struct(&pngPtr, NULL, NULL);
    fclose(file);
    return -1;
  }

  // volatile: modified after setjmp and read in the longjmp cleanup path,
  // so its value must be guaranteed to survive the longjmp (C11 7.13.2.1)
  png_bytep *volatile rowPointers = NULL;
  if (setjmp(png_jmpbuf(pngPtr))) {
    // libpng longjmps here on any decode error
    png_destroy_read_struct(&pngPtr, &infoPtr, NULL);
    fclose(file);
    if (rowPointers) {
      for (uint32_t row = 0; row < tex->Height; row++)
        free(rowPointers[row]);
      free(rowPointers);
    }
    free(tex->Mem);
    tex->Mem = NULL;
    free(tex->Clut);
    tex->Clut = NULL;
    return -1;
  }

  png_init_io(pngPtr, file);
  png_set_sig_bytes(pngPtr, 0);
  png_read_info(pngPtr, infoPtr);

  png_uint_32 width, height;
  int bitDepth, colorType, interlaceType;
  png_get_IHDR(pngPtr, infoPtr, &width, &height, &bitDepth, &colorType, &interlaceType, NULL, NULL);

  // Reject oversized art: covers are drawn at 140x200, and huge decoded
  // textures multiplied across cache slots would exhaust EE RAM
  if ((width == 0) || (height == 0) || (width > COVER_MAX_DIMENSION) || (height > COVER_MAX_DIMENSION)) {
    DPRINTF("coverart: rejecting %s (%ldx%ld)\n", path, width, height);
    png_destroy_read_struct(&pngPtr, &infoPtr, NULL);
    fclose(file);
    return -1;
  }

  if (bitDepth == 16)
    png_set_strip_16(pngPtr);
  if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 4)
    png_set_expand(pngPtr);
  if (png_get_valid(pngPtr, infoPtr, PNG_INFO_tRNS))
    png_set_tRNS_to_alpha(pngPtr);
  png_set_filler(pngPtr, 0xff, PNG_FILLER_AFTER);
  png_read_update_info(pngPtr, infoPtr);

  tex->Width = width;
  tex->Height = height;
  tex->VramClut = 0;
  tex->Clut = NULL;

  int outColorType = png_get_color_type(pngPtr, infoPtr);
  if (outColorType == PNG_COLOR_TYPE_RGB_ALPHA || outColorType == PNG_COLOR_TYPE_RGB) {
    int rowBytes = png_get_rowbytes(pngPtr, infoPtr);
    tex->PSM = (outColorType == PNG_COLOR_TYPE_RGB_ALPHA) ? GS_PSM_CT32 : GS_PSM_CT24;
    tex->Mem = memalign(128, textureSizeEE(tex->Width, tex->Height, tex->PSM));
    rowPointers = calloc(height, sizeof(png_bytep));
    if (!tex->Mem || !rowPointers)
      png_error(pngPtr, "out of memory"); // longjmps to the cleanup path
    for (uint32_t row = 0; row < height; row++) {
      if (!(rowPointers[row] = malloc(rowBytes)))
        png_error(pngPtr, "out of memory");
    }
    png_read_image(pngPtr, rowPointers);

    if (tex->PSM == GS_PSM_CT32) {
      struct pixel {
        uint8_t r, g, b, a;
      } *pixels = (struct pixel *)tex->Mem;
      int k = 0;
      for (uint32_t i = 0; i < height; i++)
        for (uint32_t j = 0; j < width; j++) {
          pixels[k].r = rowPointers[i][4 * j];
          pixels[k].g = rowPointers[i][4 * j + 1];
          pixels[k].b = rowPointers[i][4 * j + 2];
          // Convert 0-255 PNG alpha to inverted 0-128 PS2 alpha (as gsKit does)
          pixels[k++].a = 128 - ((int)rowPointers[i][4 * j + 3] * 128 / 255);
        }
    } else {
      struct pixel3 {
        uint8_t r, g, b;
      } *pixels = (struct pixel3 *)tex->Mem;
      int k = 0;
      for (uint32_t i = 0; i < height; i++)
        for (uint32_t j = 0; j < width; j++) {
          pixels[k].r = rowPointers[i][4 * j];
          pixels[k].g = rowPointers[i][4 * j + 1];
          pixels[k++].b = rowPointers[i][4 * j + 2];
        }
    }

    for (uint32_t row = 0; row < height; row++)
      free(rowPointers[row]);
    free(rowPointers);
    rowPointers = NULL;
  } else if (outColorType == PNG_COLOR_TYPE_PALETTE && (bitDepth == 4 || bitDepth == 8)) {
    png_colorp palette = NULL;
    int numPalette = 0;
    png_bytep trans = NULL;
    int numTrans = 0;
    png_get_PLTE(pngPtr, infoPtr, &palette, &numPalette);
    png_get_tRNS(pngPtr, infoPtr, &trans, &numTrans, NULL);
    tex->ClutPSM = GS_PSM_CT32;

    struct pngClut {
      uint8_t r, g, b, a;
    };

    int rowBytes = png_get_rowbytes(pngPtr, infoPtr);
    int clutEntries = (bitDepth == 4) ? 16 : 256;
    tex->PSM = (bitDepth == 4) ? GS_PSM_T4 : GS_PSM_T8;
    tex->Mem = memalign(128, textureSizeEE(tex->Width, tex->Height, tex->PSM));
    tex->Clut = memalign(128, clutEntries * 4);
    rowPointers = calloc(height, sizeof(png_bytep));
    if (!tex->Mem || !tex->Clut || !rowPointers)
      png_error(pngPtr, "out of memory"); // longjmps to the cleanup path
    memset(tex->Clut, 0, clutEntries * 4);
    for (uint32_t row = 0; row < height; row++) {
      if (!(rowPointers[row] = malloc(rowBytes)))
        png_error(pngPtr, "out of memory");
    }
    png_read_image(pngPtr, rowPointers);

    struct pngClut *clut = (struct pngClut *)tex->Clut;
    for (int i = 0; i < numPalette; i++) {
      clut[i].r = palette[i].red;
      clut[i].g = palette[i].green;
      clut[i].b = palette[i].blue;
      clut[i].a = 0x80;
    }
    for (int i = 0; i < numTrans; i++)
      clut[i].a = trans[i] >> 1;

    unsigned char *pixel = (unsigned char *)tex->Mem;
    if (bitDepth == 8) {
      // Rotate CLUT for CSM1 storage (as gsKit does)
      for (int i = 0; i < numPalette; i++) {
        if ((i & 0x18) == 8) {
          struct pngClut tmp = clut[i];
          clut[i] = clut[i + 8];
          clut[i + 8] = tmp;
        }
      }
      int k = 0;
      for (uint32_t i = 0; i < tex->Height; i++)
        for (uint32_t j = 0; j < tex->Width; j++)
          pixel[k++] = rowPointers[i][j];
    } else {
      // 4-bit: pack two pixels per byte, then swap nibbles (as gsKit does)
      int k = 0;
      for (uint32_t i = 0; i < tex->Height; i++)
        for (uint32_t j = 0; j < tex->Width / 2; j++)
          pixel[k++] = rowPointers[i][j];
      uint32_t texSize = textureSizeEE(tex->Width, tex->Height, tex->PSM);
      for (uint32_t byte = 0; byte < texSize; byte++)
        pixel[byte] = (pixel[byte] << 4) | (pixel[byte] >> 4);
    }

    for (uint32_t row = 0; row < height; row++)
      free(rowPointers[row]);
    free(rowPointers);
    rowPointers = NULL;
  } else {
    DPRINTF("coverart: unsupported PNG type %d depth %d\n", outColorType, bitDepth);
    png_destroy_read_struct(&pngPtr, &infoPtr, NULL);
    fclose(file);
    return -1;
  }

  tex->Filter = GS_FILTER_NEAREST;
  tex->Delayed = 1;
  tex->Vram = 0;
  tex->VramClut = 0;
  png_read_end(pngPtr, NULL);
  png_destroy_read_struct(&pngPtr, &infoPtr, NULL);
  fclose(file);
  return 0;
}

//
// Cache/queue management
//

static void lock() { WaitSema(mutexSema); }
static void unlock() { SignalSema(mutexSema); }

// Frees a slot's texture memory and VRAM. Must run on the main thread with
// the slot not in LOADING state.
static void freeSlot(GSGLOBAL *gsGlobal, CoverSlot *slot) {
  if (slot->state == SLOT_READY) {
    gsKit_TexManager_free(gsGlobal, &slot->tex);
    free(slot->tex.Mem);
    free(slot->tex.Clut);
  }
  cacheBytes -= (slot->memBytes > cacheBytes) ? cacheBytes : slot->memBytes;
  slot->memBytes = 0;
  memset(&slot->tex, 0, sizeof(GSTEXTURE));
  slot->id[0] = '\0';
  slot->device = NULL;
  slot->state = SLOT_EMPTY;
}

// Finds the slot holding art for (id, device), or NULL
static CoverSlot *findSlot(const char *id, struct DeviceMapEntry *device) {
  for (int i = 0; i < COVER_CACHE_SLOTS; i++) {
    if ((cache[i].state != SLOT_EMPTY) && (cache[i].device == device) && !strncmp(cache[i].id, id, COVER_ID_LEN))
      return &cache[i];
  }
  return NULL;
}

// Picks a reusable slot: prefers EMPTY, else evicts the least recently used
// READY/FAILED slot. Never touches QUEUED/LOADING slots. Returns NULL if
// every slot is queued or loading (can't happen with sane queue/cache sizes).
static CoverSlot *takeSlot(GSGLOBAL *gsGlobal) {
  CoverSlot *victim = NULL;
  for (int i = 0; i < COVER_CACHE_SLOTS; i++) {
    if (cache[i].state == SLOT_EMPTY)
      return &cache[i];
    if ((cache[i].state == SLOT_READY) || (cache[i].state == SLOT_FAILED)) {
      if ((victim == NULL) || (cache[i].lastUse < victim->lastUse))
        victim = &cache[i];
    }
  }
  if (victim != NULL)
    freeSlot(gsGlobal, victim);
  return victim;
}

// Queues a load for (id, device) if it isn't already cached or queued.
// Must be called with the mutex held.
static void queueLoad(GSGLOBAL *gsGlobal, const char *id, struct DeviceMapEntry *device) {
  if (queueCount >= COVER_QUEUE_SIZE)
    return;
  if (findSlot(id, device) != NULL) // Already cached, queued or loading
    return;

  CoverSlot *slot = takeSlot(gsGlobal);
  if (slot == NULL)
    return;

  strncpy(slot->id, id, COVER_ID_LEN - 1);
  slot->id[COVER_ID_LEN - 1] = '\0';
  slot->device = device;
  slot->state = SLOT_QUEUED;
  slot->lastUse = lruTick;

  CoverRequest *req = &queue[queueCount++];
  req->slot = slot;
  strncpy(req->id, slot->id, COVER_ID_LEN);
  req->device = device;
}

// Drops all pending requests and releases their slots for eviction.
// Must be called with the mutex held.
static void flushQueue() {
  for (int i = 0; i < queueCount; i++) {
    CoverSlot *slot = queue[i].slot;
    // Only reset slots still waiting for this request
    if ((slot->state == SLOT_QUEUED) && !strncmp(slot->id, queue[i].id, COVER_ID_LEN)) {
      slot->id[0] = '\0';
      slot->device = NULL;
      slot->state = SLOT_EMPTY;
    }
  }
  queueCount = 0;
}

//
// Worker thread
//

static void coverArtWorker() {
  char pathBuffer[PATH_MAX + 1];

  while (1) {
    WaitSema(workSema);

    lock();
    if (shutdownRequested) {
      unlock();
      break;
    }
    if (pauseRequested || (queueCount == 0)) {
      unlock();
      continue;
    }
    // Pop the front request (selected title first, then neighbors)
    CoverRequest req = queue[0];
    queueCount--;
    for (int i = 0; i < queueCount; i++)
      queue[i] = queue[i + 1];

    // Validate that the slot still belongs to this request
    if ((req.slot->state != SLOT_QUEUED) || strncmp(req.slot->id, req.id, COVER_ID_LEN)) {
      unlock();
      SignalSema(workSema); // There may be more requests
      continue;
    }
    req.slot->state = SLOT_LOADING;
    workerBusy = 1;
    unlock();

    // Build art path; fall back to the metadata device like the old loader
    struct DeviceMapEntry *device = req.device;
    if (device->metadev)
      device = device->metadev;
    snprintf(pathBuffer, sizeof(pathBuffer), "%s%s/%s_COV.png", device->mountpoint, coverArtDir, req.id);

    GSTEXTURE tex;
    memset(&tex, 0, sizeof(GSTEXTURE));
    int res = decodePNG(&tex, pathBuffer);

    lock();
    workerBusy = 0;
    if (req.slot->state == SLOT_LOADING) {
      if (res == 0) {
        req.slot->tex = tex;
        req.slot->memBytes = textureMemBytes(&tex);
        cacheBytes += req.slot->memBytes;
        req.slot->state = SLOT_READY;
      } else {
        req.slot->state = SLOT_FAILED;
      }
    } else {
      // Slot was invalidated while loading (shutdown); drop the texture
      free(tex.Mem);
      free(tex.Clut);
    }
    // needAck is only set by coverArtPause while a load is in flight;
    // acknowledge as soon as that load completes
    int mustAck = needAck;
    needAck = 0;
    int moreWork = (queueCount > 0);
    unlock();

    if (mustAck)
      SignalSema(ackSema);
    if (moreWork)
      SignalSema(workSema);
  }

  SignalSema(ackSema); // Signal exit
  ExitDeleteThread();
}

//
// Public API (main thread)
//

extern GSGLOBAL *gsGlobal; // Owned by gui.c

int coverArtInit() {
  memset(cache, 0, sizeof(cache));
  queueCount = 0;
  cacheBytes = 0;
  workerBusy = 0;
  pauseRequested = 0;
  shutdownRequested = 0;
  needAck = 0;
  lastRequestedID[0] = '\0';
  lastRequestedDevice = NULL;
  statusText = "";

  ee_sema_t semaphore;
  semaphore.init_count = 1;
  semaphore.max_count = 1;
  semaphore.option = 0;
  mutexSema = CreateSema(&semaphore);

  semaphore.init_count = 0;
  semaphore.max_count = 255;
  workSema = CreateSema(&semaphore);

  semaphore.init_count = 0;
  semaphore.max_count = 1;
  ackSema = CreateSema(&semaphore);

  if ((mutexSema < 0) || (workSema < 0) || (ackSema < 0))
    goto fail;

  ee_thread_t thread;
  thread.func = coverArtWorker;
  thread.stack = workerStack;
  thread.stack_size = WORKER_STACK_SIZE;
  thread.gp_reg = &_gp;
  thread.initial_priority = WORKER_PRIORITY;
  thread.attr = thread.option = 0;

  if ((workerThreadID = CreateThread(&thread)) < 0)
    goto fail;
  if (StartThread(workerThreadID, NULL) < 0) {
    DeleteThread(workerThreadID);
    workerThreadID = -1;
    goto fail;
  }
  return 0;

fail:
  // Roll back partially created resources so retrying init stays possible
  if (mutexSema >= 0)
    DeleteSema(mutexSema);
  if (workSema >= 0)
    DeleteSema(workSema);
  if (ackSema >= 0)
    DeleteSema(ackSema);
  mutexSema = workSema = ackSema = -1;
  return -1;
}

void coverArtShutdown() {
  if (workerThreadID < 0)
    return;

  lock();
  shutdownRequested = 1;
  flushQueue();
  unlock();
  SignalSema(workSema);
  WaitSema(ackSema); // Wait for the worker to exit

  // Free all cached textures (worker is gone; no locking needed)
  for (int i = 0; i < COVER_CACHE_SLOTS; i++) {
    if (cache[i].state == SLOT_LOADING) // Load result was dropped by the worker
      cache[i].state = SLOT_EMPTY;
    freeSlot(gsGlobal, &cache[i]);
  }

  DeleteSema(mutexSema);
  DeleteSema(workSema);
  DeleteSema(ackSema);
  mutexSema = workSema = ackSema = -1;
  workerThreadID = -1;
}

GSTEXTURE *coverArtGet(TargetList *titles, Target *target) {
  if ((workerThreadID < 0) || (target == NULL))
    return NULL;

  GSTEXTURE *result = NULL;
  lruTick++;

  lock();
  // On selection change, re-request the selection and its neighbors.
  // The device is part of the key: different devices can carry the same ID.
  if (strncmp(lastRequestedID, target->id, COVER_ID_LEN) || (lastRequestedDevice != target->device)) {
    strncpy(lastRequestedID, target->id, COVER_ID_LEN - 1);
    lastRequestedID[COVER_ID_LEN - 1] = '\0';
    lastRequestedDevice = target->device;

    flushQueue();
    // Touch the selected slot first so queueing neighbors can never pick it
    // as the LRU eviction victim (it would never be re-requested)
    CoverSlot *selectedSlot = findSlot(target->id, target->device);
    if (selectedSlot != NULL)
      selectedSlot->lastUse = lruTick;
    queueLoad(gsGlobal, target->id, target->device);
    // Prefetch neighbors, nearest first
    Target *next = target;
    Target *prev = target;
    for (int i = 0; i < COVER_PREFETCH; i++) {
      if (next && (next = next->next) != NULL)
        queueLoad(gsGlobal, next->id, next->device);
      if (prev && (prev = prev->prev) != NULL)
        queueLoad(gsGlobal, prev->id, prev->device);
    }
  }

  statusText = "Loading...";
  CoverSlot *slot = findSlot(target->id, target->device);
  if (slot != NULL) {
    slot->lastUse = lruTick;
    if (slot->state == SLOT_READY) {
      result = &slot->tex;
      statusText = "";
    } else if (slot->state == SLOT_FAILED) {
      statusText = "No cover art";
    }
  }

  // Enforce the cache memory budget by evicting least recently used covers
  // (never the selected slot or slots the worker is still using)
  while (cacheBytes > COVER_CACHE_MAX_BYTES) {
    CoverSlot *victim = NULL;
    for (int i = 0; i < COVER_CACHE_SLOTS; i++) {
      if ((&cache[i] == slot) || ((cache[i].state != SLOT_READY) && (cache[i].state != SLOT_FAILED)))
        continue;
      if ((victim == NULL) || (cache[i].lastUse < victim->lastUse))
        victim = &cache[i];
    }
    if (victim == NULL)
      break;
    freeSlot(gsGlobal, victim);
  }
  int pending = queueCount;
  unlock();

  if (pending > 0)
    SignalSema(workSema);

  // Bind (uploads to VRAM only when not already resident)
  if (result != NULL)
    gsKit_TexManager_bind(gsGlobal, result);

  return result;
}

const char *coverArtStatusText() {
  return statusText;
}

void coverArtPause() {
  if (workerThreadID < 0)
    return;

  lock();
  pauseRequested = 1;
  flushQueue();
  if (workerBusy) {
    needAck = 1;
    unlock();
    WaitSema(ackSema); // Wait for the in-flight load to finish
    return;
  }
  unlock();
}

void coverArtResume() {
  if (workerThreadID < 0)
    return;

  lock();
  pauseRequested = 0;
  // Force the next coverArtGet to re-request the selection and its
  // neighbors (their queued loads were flushed by coverArtPause)
  lastRequestedID[0] = '\0';
  lastRequestedDevice = NULL;
  unlock();
}
