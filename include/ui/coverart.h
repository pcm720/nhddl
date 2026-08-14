#ifndef _COVERART_H_
#define _COVERART_H_

#include "target.h"
#include <gsKit.h>

// Asynchronous cover art loader.
//
// Cover art used to be loaded synchronously in the UI loop: every selection
// change blocked the render thread for a full open + read + PNG decode round
// trip (expensive on network devices like UDPFS). This module moves file IO
// and PNG decoding to a low-priority worker thread and keeps a small LRU
// cache of decoded textures, prefetching neighbors of the selected title so
// scrolling feels instant.
//
// Threading contract:
// - All functions declared here must be called from the main thread only.
// - The worker thread only reads files and decodes PNGs into RAM; it never
//   touches gsKit VRAM state (TexManager bind/free happen on the main thread).

// Creates the worker thread and cache. Must be called after gsKit is up.
// Returns 0 on success.
int coverArtInit();

// Stops the worker thread and frees all cached textures and VRAM.
// Safe to call if init failed or was never run.
void coverArtShutdown();

// Returns the decoded texture for the target if it is cached, or NULL if it
// is still loading or failed to load. Queues an async load for the target
// and its neighbors on selection change. Also binds the returned texture for
// this frame, so it must be called between gsKit_TexManager_nextFrame and
// gsKit_queue_exec.
GSTEXTURE *coverArtGet(TargetList *titles, Target *target);

// Returns placeholder text for the currently selected title:
// "Loading..." while the load is in flight, "No cover art" if it failed.
const char *coverArtStatusText();

// Pauses the worker thread, waiting for any in-flight load to finish.
// Call before doing file IO on the main thread (options screens, launch)
// so device access stays serialized.
void coverArtPause();

// Resumes the worker thread after coverArtPause.
void coverArtResume();

#endif
