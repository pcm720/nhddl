#include "ui/worker/worker.h"
#include "backends/backends.h"
#include "backends/cache.h"
#include "config/config.h"
#include "config/nhddl.h"
#include "dprintf.h"
#include "ui/png.h"
#include <kernel.h>
#include <malloc.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DEVICE_TYPES 16
/* ISO scan uses PATH_MAX buffers per recursion level (see _findISO); 8 KiB overflows easily. */
#define WORKER_STACK_SIZE 0x20000
#define DEFAULT_ERROR_FRAMES 120

static uint8_t s_workerStack[WORKER_STACK_SIZE] __attribute__((aligned(16)));

static int s_threadId = -1;
static int s_running = 0;

static int32_t s_jobPendingSema = -1;
static int32_t s_jobDoneSema = -1;
static int32_t s_listSema = -1;

static volatile WorkerJobType s_jobType = WorkerJob_None;
static WorkerLoadCoverParams s_loadCoverParams;
static DeviceType s_initDeviceType = Device_None;

static volatile int s_listReady = 0;
static volatile int s_listNeedsRebuild = 0;
static volatile int s_jobDoneFlag = 0;
static volatile int s_coverDoneFlag = 1;

static WorkerErrorSlot s_errorSlot;

static void setErrorLocked(const char *message, int frames) {
  s_errorSlot.pending = 1;
  strncpy(s_errorSlot.message, message, WORKER_ERROR_MESSAGE_SIZE - 1);
  s_errorSlot.message[WORKER_ERROR_MESSAGE_SIZE - 1] = '\0';
  s_errorSlot.frames = frames > 0 ? frames : DEFAULT_ERROR_FRAMES;
}

// Init any enabled device type that has no backend slot (e.g. probe failed earlier, or user just enabled it).
static void initMissingEnabledBackends(void) {
  DeviceType types[MAX_DEVICE_TYPES];
  int n = getEnabledDeviceTypesArray(types, MAX_DEVICE_TYPES);
  for (int i = 0; i < n; i++) {
    if (getBackendDeviceCountByType(types[i]) > 0)
      continue;
    int r = initBackend(types[i], 0);
    if (r < 0 && (r & 0x7fffffff) != 0) {
      WaitSema(s_listSema);
      setErrorLocked("Device init failed", DEFAULT_ERROR_FRAMES);
      SignalSema(s_listSema);
      DPRINTF("worker: initBackend failed for type %d\n", (int)types[i]);
    }
  }
}

static void runInitList(void) {
  removeBackendsDisabledInConfig();
  initMissingEnabledBackends();

  int n = getBackendDeviceCount();
  for (int i = 0; i < n; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    DPRINTF("ui/worker: scanning %s\n", dev->mountpoint);
    if (dev->scan && !dev->titles)
      scanBackendDevice(dev);
  }

  s_listReady = 1;
}

static void runLoadCover(void) {
  if (loadPNGFromFile(s_loadCoverParams.gsGlobal, s_loadCoverParams.targetTexture, s_loadCoverParams.path)) {
    s_loadCoverParams.targetTexture->Width = 0;
    s_loadCoverParams.targetTexture->Height = 0;
    s_coverDoneFlag = 1;
    return;
  }
  gsKit_TexManager_bind(s_loadCoverParams.gsGlobal, s_loadCoverParams.targetTexture);

  s_coverDoneFlag = 1;
}

static void runSaveConfig(void) {
  if (saveOptions() != 0) {
    WaitSema(s_listSema);
    setErrorLocked("Config save failed", DEFAULT_ERROR_FRAMES);
    SignalSema(s_listSema);
  }
}

static void runInitDevice(void) {
  removeBackendsDisabledInConfig();
  int r = initBackend(s_initDeviceType, 0);
  if (r < 0 && (r & 0x7fffffff) != 0) {
    WaitSema(s_listSema);
    setErrorLocked("Device init failed", DEFAULT_ERROR_FRAMES);
    SignalSema(s_listSema);
    return;
  }
  s_listNeedsRebuild = 1;
}

static void runRescanDevices(void) {
  removeBackendsDisabledInConfig();
  initMissingEnabledBackends();
  rescanAllBackendDevices();
  s_listNeedsRebuild = 1;
}

static void runInvalidateCache(void) {
  removeBackendsDisabledInConfig();
  int n = getBackendDeviceCount();
  for (int i = 0; i < n; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (dev)
      invalidateTitleIDCache(dev);
  }
}

static void runApplyEnabledDevices(void) {
  removeBackendsDisabledInConfig();
  initMissingEnabledBackends();
  rescanAllBackendDevices();
  s_listNeedsRebuild = 1;
}

static void workerThread(void *arg) {
  DPRINTF("ui/worker: worker thread started\n");
  (void)arg;
  while (s_running) {
    WaitSema(s_jobPendingSema);
    if (!s_running)
      break;
    WorkerJobType job = s_jobType;
    s_jobType = WorkerJob_None;

    switch (job) {
    case WorkerJob_None:
      break;
    case WorkerJob_InitList:
      runInitList();
      break;
    case WorkerJob_LoadCover:
      runLoadCover();
      break;
    case WorkerJob_SaveConfig:
      runSaveConfig();
      break;
    case WorkerJob_InitDevice:
      runInitDevice();
      break;
    case WorkerJob_RescanDevices:
      runRescanDevices();
      break;
    case WorkerJob_InvalidateCache:
      runInvalidateCache();
      break;
    case WorkerJob_ApplyEnabledDevices:
      runApplyEnabledDevices();
      break;
    }
    s_jobDoneFlag = 1;
    SignalSema(s_jobDoneSema);
  }
  DPRINTF("ui/worker: worker thread exiting\n");
  ExitDeleteThread();
}

int workerStart(void) {
  if (s_threadId >= 0)
    return 0;

  s_listReady = 0;
  s_listNeedsRebuild = 0;
  s_errorSlot.pending = 0;

  ee_sema_t sema;
  sema.init_count = 0;
  sema.max_count = 1;
  sema.option = 0;
  s_jobPendingSema = CreateSema(&sema);
  s_jobDoneSema = CreateSema(&sema);
  SignalSema(s_jobDoneSema);
  sema.init_count = 1;
  s_listSema = CreateSema(&sema);

  if (s_jobPendingSema < 0 || s_jobDoneSema < 0 || s_listSema < 0) {
    if (s_listSema >= 0)
      DeleteSema(s_listSema);
    if (s_jobDoneSema >= 0)
      DeleteSema(s_jobDoneSema);
    if (s_jobPendingSema >= 0)
      DeleteSema(s_jobPendingSema);
    return -1;
  }

  s_running = 1;
  ee_thread_t thread;
  thread.func = workerThread;
  thread.stack = s_workerStack;
  thread.stack_size = WORKER_STACK_SIZE;
  thread.gp_reg = &_gp;
  thread.initial_priority = 1;
  thread.attr = thread.option = 0;

  s_threadId = CreateThread(&thread);
  if (s_threadId < 0) {
    s_running = 0;
    DeleteSema(s_listSema);
    DeleteSema(s_jobDoneSema);
    DeleteSema(s_jobPendingSema);
    return -1;
  }
  DPRINTF("ui/worker: starting worker thread\n");
  if (StartThread(s_threadId, NULL) < 0) {
    DeleteThread(s_threadId);
    s_threadId = -1;
    s_running = 0;
    DeleteSema(s_listSema);
    DeleteSema(s_jobDoneSema);
    DeleteSema(s_jobPendingSema);
    return -1;
  }
  return 0;
}

void workerStop(void) {
  if (s_threadId < 0)
    return;
  s_running = 0;
  SignalSema(s_jobPendingSema);
  while (PollSema(s_jobDoneSema) != s_jobDoneSema)
    /* wait for worker to exit */;
  DeleteSema(s_jobPendingSema);
  DeleteSema(s_jobDoneSema);
  DeleteSema(s_listSema);
  s_jobPendingSema = s_jobDoneSema = s_listSema = -1;
  s_threadId = -1;
}

static void enqueueJob(WorkerJobType type) {
  WaitSema(s_jobDoneSema);
  // Previous job posted done sema; flag may still be 1 if UI never polled — clear so rebuild cannot fire early.
  s_jobDoneFlag = 0;
  s_jobType = type;
  SignalSema(s_jobPendingSema);
}

void workerEnqueueInitList(void) { enqueueJob(WorkerJob_InitList); }

void workerEnqueueLoadCover(const char *path, GSGLOBAL *gsGlobal, GSTEXTURE *coverTexture) {
  s_coverDoneFlag = 0;
  strncpy(s_loadCoverParams.path, path, sizeof(s_loadCoverParams.path) - 1);
  s_loadCoverParams.path[sizeof(s_loadCoverParams.path) - 1] = '\0';
  s_loadCoverParams.targetTexture = coverTexture;
  s_loadCoverParams.gsGlobal = gsGlobal;
  enqueueJob(WorkerJob_LoadCover);
}

void workerEnqueueSaveConfig(void) { enqueueJob(WorkerJob_SaveConfig); }

void workerEnqueueInitDevice(DeviceType type) {
  s_initDeviceType = type;
  enqueueJob(WorkerJob_InitDevice);
}

void workerEnqueueRescanDevices(void) { enqueueJob(WorkerJob_RescanDevices); }

void workerEnqueueInvalidateCache(void) { enqueueJob(WorkerJob_InvalidateCache); }

void workerEnqueueApplyEnabledDevices(void) { enqueueJob(WorkerJob_ApplyEnabledDevices); }

void workerLock(void) { WaitSema(s_listSema); }

void workerUnlock(void) { SignalSema(s_listSema); }

int workerIsListReady(void) { return s_listReady; }

// Returns 1 once the cover is loaded or failed to load
int workerPollCoverLoadDone(void) { return s_coverDoneFlag; }

int workerGetAndClearListNeedsRebuild(void) {
  if (!s_listNeedsRebuild)
    return 0;
  s_listNeedsRebuild = 0;
  return 1;
}

int workerPollJobDone(void) {
  if (!s_jobDoneFlag)
    return 0;
  s_jobDoneFlag = 0;
  return 1;
}

void workerSetError(const char *message, int frames) {
  WaitSema(s_listSema);
  setErrorLocked(message, frames);
  SignalSema(s_listSema);
}

int workerHasError(void) { return s_errorSlot.pending; }

int workerConsumeError(char *outMessage, size_t outSize, int *outFrames) {
  if (!s_errorSlot.pending)
    return 0;
  WaitSema(s_listSema);
  if (!s_errorSlot.pending) {
    SignalSema(s_listSema);
    return 0;
  }
  if (outMessage && outSize > 0) {
    strncpy(outMessage, s_errorSlot.message, outSize - 1);
    outMessage[outSize - 1] = '\0';
  }
  if (outFrames)
    *outFrames = s_errorSlot.frames;
  s_errorSlot.pending = 0;
  SignalSema(s_listSema);
  return 1;
}
