#ifndef _CONFIG_NHDDL_H_
#define _CONFIG_NHDDL_H_

#include "config/config.h"
#include "devices/devices.h"
#include <ps2sdkapi.h>

// Options file name relative to config root
extern const char optionsFile[];

// Parses mode string into DeviceType
DeviceType parseDevice(const char *modeStr);

// Parses argv[0] for device postfix
DeviceType parseFilename(const char *path);

// Parses video mode string into enum
VModeType parseVMode(const char *modeStr);

// Attempts to parse argv into config
void parseArgv(int argc, char *argv[]);

// Loads NHDDL options from optionsFile in config root path
// Returns 0 on success, negative on error
int loadOptions(void);
// Saves current config to optionsFile in config root path
// Returns 0 on success, negative on error
int saveOptions(void);

#endif
