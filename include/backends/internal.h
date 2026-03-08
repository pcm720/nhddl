#ifndef _BACKENDS_INTERNAL_H_
#define _BACKENDS_INTERNAL_H_

#include "backends/backends.h"

// Init layer only: backend device array (defined in backends.c).
extern struct BackendDevice backendDevices[MAX_DEVICES];

void removeConflictingBackends(DeviceType conflictMask);
void rescanAllBackendDevices(void);

#endif
