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

// Launches an arbitrary ELF with PS2SDK's overlap-safe ELF loader. Unlike
// LoadExecPS2, this stages the target before the IOP reset and is safe when
// the target overlaps NHDDL's own user-memory image.
int launchExternalELF(const char *path);

// Stages the dashboard ELF in EE RAM so a front-panel press can restart even
// if the IOP/fileXio/UDPFS side is wedged during startup.
int prepareDashboardRestart(const char *path);
// Immediately executes the staged dashboard, falling back to a live load if
// staging was not yet available. Returns only on failure.
int restartDashboardNow(void);

#endif
