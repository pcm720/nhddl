#ifndef _UI_WORKER_H_
#define _UI_WORKER_H_

#include "devices/devices.h"
#include <gsKit.h>
#include <stddef.h>
#include <stdint.h>

#define WORKER_MESSAGE_SIZE 96

typedef enum {
  WorkerJob_None = 0,
  WorkerJob_InitList,
  WorkerJob_LoadCover,
  WorkerJob_SaveConfig,
  WorkerJob_Rescan,
  WorkerJob_InvalidateCache,
} WorkerJobType;

typedef enum {
  WorkerState_Idle = 0,
  WorkerState_Running,
  WorkerState_Done,
} WorkerState;

typedef struct {
  WorkerJobType type;
  int ok; // 1 = success
  char message[WORKER_MESSAGE_SIZE];
  int listChanged; // UI should rebuild view list from backends
} WorkerResult;

// Start/stop worker thread.
int workerStart(void);
void workerStop(void);

// Request a job only when Idle. Returns 0 on accepted, -1 if busy/invalid.
// For LoadCover, use workerRequestLoadCover (needs path/texture args).
int workerRequest(WorkerJobType type);
int workerRequestLoadCover(const char *path, GSTEXTURE *texture);

WorkerState workerGetState(void);
WorkerJobType workerGetJobType(void);

// When state is Done, copy result and return to Idle. Returns 1 if taken, 0 otherwise.
int workerTakeResult(WorkerResult *out);

#endif
