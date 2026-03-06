#ifndef _CONFIG_CONFIG_H_
#define _CONFIG_CONFIG_H_

#include "config/arguments.h"
#include "backends/backends.h"
#include "target.h"
#include <ps2sdkapi.h>
#include <stdint.h>

// Location of configuration directory relative to storage mountpoint
extern const char BASE_CONFIG_PATH[];
extern const size_t BASE_CONFIG_PATH_LEN;

// Writes full path to targetFileName into targetPath.
// If targetFileName is NULL, will return path to config directory
void buildConfigFilePath(char *targetPath, const char *targetMountpoint, const char *targetFileName);

// Gets last launched title path into titlePath
// Searches for the latest file across all mounted BDM devices
int getLastLaunchedTitle(char *titlePath);

// Writes last launched title path into lastTitle file on title mountpoint
int updateLastLaunchedTitle(struct DeviceMapEntry *device, char *titlePath);

// Generates ArgumentList from global config file located on device
// Will reinitialize result without clearing existing contents. On error, result will contain invalid pointer.
int getGlobalLaunchArguments(ArgumentList *result, struct DeviceMapEntry *device);

// Generates ArgumentList from title-specific config file.
// Will reinitialize result without clearing existing contents. On error, result will contain invalid pointer.
int getTitleLaunchArguments(ArgumentList *result, Target *target);

// Saves title launch arguments to title-specific config file (CNF format: -name=value, #-name=value for disabled).
// Empty value means that the argument is empty, but still should be used without the value.
int updateTitleLaunchArguments(Target *target, ArgumentList *options);

// Loads both global and title launch arguments, returning pointer to a merged list
ArgumentList *loadLaunchArgumentLists(Target *target);

// Parses options file into ArgumentList
int loadArgumentList(ArgumentList *options, struct DeviceMapEntry *device, char *filePath);

// Generates 32-bit timestamp from RTC. Will wrap around every 64th year
uint32_t getTimestamp(void);

#endif
