#ifndef _NEUTRINO_H_
#define _NEUTRINO_H_

#include "devices/init.h"
#include "options.h"
#include "target.h"

// Attempts to find neutrino.elf at current path or one of fallback paths
int findNeutrinoELF(char *cwdPath);
// Reads version.txt from NEUTRINO_ELF_PATH
// Returns empty string if the file could not be read
char *getNeutrinoVersion();
// Launches target, passing arguments to Neutrino.
// Expects arguments to be initialized
void launchTitle(Target *target, ArgumentList *arguments);

#endif
