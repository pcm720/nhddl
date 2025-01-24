#ifndef _ISO_H_
#define _ISO_H_

#include "target.h"
#include "devices.h"

// Scans given storage device and appends valid launch candidates to TargetList
// Returns 0 if successful, non-zero if no targets were found or an error occurs
int findISO(TargetList *list, struct DeviceMapEntry *device);

#endif
