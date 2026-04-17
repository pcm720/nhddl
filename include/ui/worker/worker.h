#ifndef _UI_WORKER_H_
#define _UI_WORKER_H_

#include "backends/target.h"
#include "devices/devices.h"
#include <gsKit.h>
#include <stddef.h>
#include <stdint.h>

#define WORKER_ERROR_MESSAGE_SIZE 96
#define WORKER_PENDING_COVER_TARGET_ID_SIZE 64

typedef enum {
  WorkerJob_None = 0,
  WorkerJob_InitList,
  WorkerJob_LoadCover,
  WorkerJob_SaveConfig,
  WorkerJob_InitDevice,
  WorkerJob_RescanDevices,
  WorkerJob_InvalidateCache,
  WorkerJob_ApplyEnabledDevices,
} WorkerJobType;

// LoadCover params (path and target id for matching)
typedef struct {
  char path[256];
  GSGLOBAL *gsGlobal;
  GSTEXTURE *targetTexture;
} WorkerLoadCoverParams;

// Error slot: worker writes, UI consumes and shows via OSD
typedef struct {
  int pending;
  char message[WORKER_ERROR_MESSAGE_SIZE];
  int frames;
} WorkerErrorSlot;

// Start/stop worker thread. Call after view stack exists; stop in uiCleanup.
int workerStart(void);
void workerStop(void);

// Enqueue jobs (set params then call enqueue). Worker processes one at a time.
void workerEnqueueInitList(void);
void workerEnqueueLoadCover(const char *path, GSGLOBAL *gsGlobal, GSTEXTURE *coverTexture);
void workerEnqueueSaveConfig(void);
void workerEnqueueInitDevice(DeviceType type);
void workerEnqueueRescanDevices(void);
void workerEnqueueInvalidateCache(void);
// Init any enabled device types not yet present, then rescan all backends (I/O thread only).
void workerEnqueueApplyEnabledDevices(void);

// Lock for reading shared state (pending cover, etc.). UI must hold when reading WorkerPendingCover.
void workerLock(void);
void workerUnlock(void);

// listReady: set by worker after InitList (backends inited). Splash waits for this; UI then builds view list from backends.
int workerIsListReady(void);

// listNeedsRebuild: set by worker after InitDevice or RescanDevices. UI rebuilds view list from backends on UI thread and clears.
int workerGetAndClearListNeedsRebuild(void);

// Returns 1 once after the last enqueued job has completed (consumed by caller). UI uses to know when to rebuild list after Rescan/InitDevice.
int workerPollJobDone(void);

// Returns 1 once the cover is loaded or failed to load
int workerPollCoverLoadDone(void);

// Error slot: worker sets on failure; UI consumes once per frame and calls showOSD.
void workerSetError(const char *message, int frames);
int workerHasError(void);
int workerConsumeError(char *outMessage, size_t outSize, int *outFrames);

#endif
