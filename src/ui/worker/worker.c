#include "ui/worker/worker.h"
#include "backends/backends.h"
#include "backends/cache.h"
#include "config/config.h"
#include "config/nhddl.h"
#include "dprintf.h"
#include "ui/png.h"
#include "ui/strings.h"
#include <errno.h>
#include <kernel.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MAX_DEVICE_TYPES 16
#define WORKER_STACK_SIZE 0x20000

static uint8_t s_workerStack[WORKER_STACK_SIZE] __attribute__((aligned(16)));

static int s_threadId = -1;
static volatile int s_running = 0;

static int32_t s_wakeSema = -1;

static volatile WorkerState s_state = WorkerState_Idle;
static volatile WorkerJobType s_jobType = WorkerJob_None;

static char s_coverPath[256];
static GSTEXTURE *s_coverTexture = NULL;

static WorkerResult s_result;

static void clearResult(void) {
  s_result.type = WorkerJob_None;
  s_result.ok = 1;
  s_result.message[0] = '\0';
  s_result.listChanged = 0;
}

static void setResultError(const char *message) {
  s_result.ok = 0;
  if (message && message[0]) {
    strncpy(s_result.message, message, WORKER_MESSAGE_SIZE - 1);
    s_result.message[WORKER_MESSAGE_SIZE - 1] = '\0';
  }
}

static void initMissingEnabledBackends(void) {
  DeviceType types[MAX_DEVICE_TYPES];
  int n = getEnabledDeviceTypesArray(types, MAX_DEVICE_TYPES);
  for (int i = 0; i < n; i++) {
    if (getBackendDeviceCountByType(types[i]) > 0)
      continue;
    int r = initBackend(types[i], 0);
    if (r < 0 && (r & 0x7fffffff) != 0) {
      setResultError(uiStr(UI_STR_DEVICE_INIT_FAILED));
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

  s_result.listChanged = 1;
}

static void runLoadCover(void) {
  if (!s_coverTexture || !s_coverPath[0]) {
    if (s_coverTexture) {
      s_coverTexture->Width = 0;
      s_coverTexture->Height = 0;
    }
    return;
  }
  // Decode only; UI thread binds before draw. Missing art is normal — no OSD.
  if (loadPNGFromFile(NULL, s_coverTexture, s_coverPath)) {
    s_coverTexture->Width = 0;
    s_coverTexture->Height = 0;
  }
}

static void runSaveConfig(void) {
  int r = saveOptions();
  if (r == 0) {
    strncpy(s_result.message, uiStr(UI_STR_SETTINGS_SAVED), WORKER_MESSAGE_SIZE - 1);
    s_result.message[WORKER_MESSAGE_SIZE - 1] = '\0';
    return;
  }
  if (r == -ENOENT)
    setResultError(uiStr(UI_STR_CONFIG_SAVE_NO_PATH));
  else if (r == -EBADF)
    setResultError(uiStr(UI_STR_CONFIG_SAVE_OPEN));
  else
    setResultError(uiStr(UI_STR_CONFIG_SAVE_WRITE));
}

static void runRescan(void) {
  removeBackendsDisabledInConfig();
  initMissingEnabledBackends();
  rescanAllBackendDevices();
  s_result.listChanged = 1;
}

static void runInvalidateCache(void) {
  removeBackendsDisabledInConfig();
  int n = getBackendDeviceCount();
  for (int i = 0; i < n; i++) {
    struct BackendDevice *dev = getBackendDeviceAt(i);
    if (dev)
      invalidateTitleIDCache(dev);
  }
  strncpy(s_result.message, uiStr(UI_STR_CACHE_INVALIDATED), WORKER_MESSAGE_SIZE - 1);
  s_result.message[WORKER_MESSAGE_SIZE - 1] = '\0';
}

static void workerThread(void *arg) {
  DPRINTF("ui/worker: worker thread started\n");
  while (s_running) {
    WaitSema(s_wakeSema);
    if (!s_running)
      break;

    WorkerJobType job = s_jobType;
    clearResult();
    s_result.type = job;

    switch (job) {
    case WorkerJob_InitList:
      runInitList();
      break;
    case WorkerJob_LoadCover:
      runLoadCover();
      break;
    case WorkerJob_SaveConfig:
      runSaveConfig();
      break;
    case WorkerJob_Rescan:
      runRescan();
      break;
    case WorkerJob_InvalidateCache:
      runInvalidateCache();
      break;
    case WorkerJob_None:
    default:
      break;
    }

    s_state = WorkerState_Done;
  }
  DPRINTF("ui/worker: worker thread exiting\n");
  ExitDeleteThread();
}

int workerRequest(WorkerJobType type) {
  if (type == WorkerJob_None || type == WorkerJob_LoadCover) {
    DPRINTF("ui/worker: request %d rejected (use workerRequestLoadCover for covers)\n", (int)type);
    return -1;
  }
  if (s_state != WorkerState_Idle) {
    DPRINTF("ui/worker: request %d rejected (state=%d job=%d)\n", (int)type, (int)s_state, (int)s_jobType);
    return -1;
  }
  clearResult();
  s_jobType = type;
  s_state = WorkerState_Running;
  SignalSema(s_wakeSema);
  return 0;
}

int workerStart(void) {
  if (s_threadId >= 0)
    return 0;

  s_state = WorkerState_Idle;
  s_jobType = WorkerJob_None;
  clearResult();
  s_coverTexture = NULL;
  s_coverPath[0] = '\0';

  ee_sema_t sema;
  sema.init_count = 0;
  sema.max_count = 1;
  sema.option = 0;
  s_wakeSema = CreateSema(&sema);
  if (s_wakeSema < 0)
    return -1;

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
    DeleteSema(s_wakeSema);
    s_wakeSema = -1;
    return -1;
  }
  DPRINTF("ui/worker: starting worker thread\n");
  if (StartThread(s_threadId, NULL) < 0) {
    DeleteThread(s_threadId);
    s_threadId = -1;
    s_running = 0;
    DeleteSema(s_wakeSema);
    s_wakeSema = -1;
    return -1;
  }
  return 0;
}

void workerStop(void) {
  if (s_threadId < 0)
    return;
  s_running = 0;
  SignalSema(s_wakeSema);
  // Wait until thread exits by yielding; EE has no join.
  for (int i = 0; i < 1000 && s_threadId >= 0; i++)
    RotateThreadReadyQueue(1);
  if (s_wakeSema >= 0) {
    DeleteSema(s_wakeSema);
    s_wakeSema = -1;
  }
  s_threadId = -1;
  s_state = WorkerState_Idle;
}

int workerRequestLoadCover(const char *path, GSTEXTURE *texture) {
  if (!path || !texture)
    return -1;
  if (s_state != WorkerState_Idle) {
    DPRINTF("ui/worker: LoadCover rejected (state=%d)\n", (int)s_state);
    return -1;
  }
  strncpy(s_coverPath, path, sizeof(s_coverPath) - 1);
  s_coverPath[sizeof(s_coverPath) - 1] = '\0';
  s_coverTexture = texture;
  clearResult();
  s_jobType = WorkerJob_LoadCover;
  s_state = WorkerState_Running;
  SignalSema(s_wakeSema);
  return 0;
}

WorkerState workerGetState(void) { return s_state; }

WorkerJobType workerGetJobType(void) { return s_jobType; }

int workerTakeResult(WorkerResult *out) {
  if (s_state != WorkerState_Done)
    return 0;
  if (out)
    *out = s_result;
  s_jobType = WorkerJob_None;
  s_state = WorkerState_Idle;
  return 1;
}
