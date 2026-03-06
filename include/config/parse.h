#ifndef _CONFIG_PARSE_H_
#define _CONFIG_PARSE_H_

#include "config/config.h"
#include "devices/devices.h"
#include <ps2sdkapi.h>

// Options file name relative to CWD
extern const char optionsFile[];

// Parses mode string into DeviceType
DeviceType parseDevice(const char *modeStr);

// Parses argv[0] for mode postfix
DeviceType parseFilename(const char *path);

// Parses video mode string into enum
VModeType parseVMode(const char *modeStr);

// Attempts to parse argv into config
void parseArgv(int argc, char *argv[]);

// Loads NHDDL options from optionsFile in cwdPath
int loadOptions(char *cwdPath);

#endif
