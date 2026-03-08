#ifndef _CONFIG_NEUTRINO_ARGS_H_
#define _CONFIG_NEUTRINO_ARGS_H_

#include "backends/backends.h"
#include "backends/target.h"
#include "config/arguments.h"
#include <ps2sdkapi.h>
#include <stdint.h>

// Loads ArgumentList from global config file on device.
// Expects result to be an initialized/empty list; overwrites its contents. On error, result may be invalid.
int loadGlobalNeutrinoArguments(ArgumentList *result, struct BackendDevice *device);

// Loads ArgumentList from title-specific config file only (not global).
// Expects result to be an initialized/empty list; overwrites its contents. On error, result may be invalid.
int loadTitleNeutrinoArguments(ArgumentList *result, Target *target);

// Saves title Neutrino arguments to title-specific config file (CNF format: -name=value, #-name=value for disabled).
// Accepts only the title list; writes every argument in the list to the title .cnf.
int saveTitleNeutrinoArguments(Target *target, ArgumentList *title_list);

// Saves global Neutrino arguments to global.cnf on device.
int saveGlobalNeutrinoArguments(struct BackendDevice *device, ArgumentList *options);

// Merges global and per-title Neutrino arguments for display or launch.
// Can be used to merge global and per-title Neutrino arguments for display or launch.
// Semantic: dst ← src — per-title options (including overrides) are merged into global; per-title wins on duplicate names.
// Caller must free the returned list with freeArgumentList.
ArgumentList *mergeNeutrinoArguments(ArgumentList *dst, ArgumentList *src);

#endif
