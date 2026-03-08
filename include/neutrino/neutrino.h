#ifndef _NEUTRINO_H_
#define _NEUTRINO_H_

#include "backends/target.h"
#include "config/arguments.h"
#include "devices/devices.h"

// Reads version.txt for located Neutrino
// Tries to locate Neutrino if Neutrino path is unknown.
// Returns empty string if the file could not be read
char *getNeutrinoVersion();
// Launches target, passing arguments to Neutrino.
// Expects arguments to be initialized
int launchTarget(Target *target, ArgumentList *arguments);

#endif
