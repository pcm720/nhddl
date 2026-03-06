#include "config/parse.h"
#include "backends/backends.h"
#include "common.h"
#include "config/config.h"
#include "config/title.h"
#include "dprintf.h"
#include "options.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char optionsFile[] = "nhddl.cnf";

// Supported options
#define OPTION_VMODE "video"
#define OPTION_DEVICE "device"
#define OPTION_IP_ADDRESS "ip_addr"
#define OPTION_IMAGE "dvd"
#define OPTION_NO_INIT "noinit"
#define OPTION_PROBE_DELAY "probe_delay"
#define OPTION_NEUTRINO "neutrino"

// Parses mode string into DeviceType
DeviceType parseDevice(const char *val) {
  if (!strncmp(val, "ata", 3))
    return Device_HDD;
  if (!strncmp(val, "mx4sio", 3))
    return Device_MX4SIO;
  if (!strncmp(val, "udpfs", 5))
    return Device_UDPFS;
  if (!strncmp(val, "usb", 3))
    return Device_USB;
  if (!strncmp(val, "ilink", 5))
    return Device_iLink;
  if (!strncmp(val, "mmce", 4))
    return Device_MMCE;
  if (!strncmp(val, "hdl", 3))
    return Device_HDD;
  return Device_None;
}

// Parses argv[0] for device postfix
DeviceType parseFilename(const char *path) {
  char *val = strrchr(path, '-');
  if (!val)
    return Device_None;

  val++;
  return parseDevice(val);
}

// Parses video mode string into enum
VModeType parseVMode(const char *modeStr) {
  if (!strcmp(modeStr, "ntsc"))
    return VMode_NTSC;
  if (!strcmp(modeStr, "pal"))
    return VMode_PAL;
  if (!strcmp(modeStr, "480p"))
    return VMode_480p;
  if (!strcmp(modeStr, "720p"))
    return VMode_720p;
  return VMode_NONE;
}

// Attempts to parse argv into config
void parseArgv(int argc, char *argv[]) {
  setEnabledDevices(Device_None);
  char *arg;
  for (int i = 0; i < argc; i++) {
    arg = argv[i];
    if ((arg == NULL) || (arg[0] != '-'))
      continue;

    char *val = strchr(arg, '=');
    if (val) {
      *val = '\0';
      val++;
    }
    arg++;

    if (val && !strcmp(OPTION_VMODE, arg)) {
      DPRINTF("Using VMode %s\n", val);
      setVMode(parseVMode(val));
    } else if (val && !strcmp(OPTION_DEVICE, arg)) {
      DPRINTF("Enabling device %s\n", val);
      setEnabledDevices(getEnabledDevices() | parseDevice(val));
    } else if (val && !strcmp(OPTION_IP_ADDRESS, arg)) {
      DPRINTF("Using IP %s\n", val);
      setIPAddress(val);
    } else if (val && !strcmp(OPTION_IMAGE, arg)) {
      DPRINTF("Using image %s\n", val);
      setImage(val);
    } else if (!strcmp(OPTION_NO_INIT, arg)) {
      DPRINTF("Skipping IOP init\n");
      setNoInit(1);
    } else if (!strcmp(OPTION_PROBE_DELAY, arg)) {
      DPRINTF("Using probe delay %d\n", val);
      setProbeDelay(val ? atoi(val) : 0);
    } else if (!strcmp(OPTION_NEUTRINO, arg)) {
      DPRINTF("Using custom Neutrino path: %s\n", val);
      setNeutrinoElfPath(val);
    }
  }
}

// Loads NHDDL options from optionsFile in cwdPath
int loadOptions(char *cwdPath) {
  char lineBuffer[PATH_MAX + sizeof(optionsFile) + 1];
  if (cwdPath[0] != '\0') {
    strcpy(lineBuffer, cwdPath);
    strcat(lineBuffer, optionsFile);
    if (!tryFile(lineBuffer))
      goto fileExists;
  }
  DPRINTF("Can't load options file, will use defaults\n");
  return -ENOENT;

fileExists:
  ArgumentList *options = calloc(1, sizeof(ArgumentList));
  if (loadArgumentList(options, NULL, lineBuffer)) {
    DPRINTF("Can't load options file, will use defaults\n");
    freeArgumentList(options);
    return -ENOENT;
  }

  Argument *arg = options->first;
  while (arg != NULL) {
    if (!arg->isDisabled) {
      if (!strcmp(OPTION_VMODE, arg->arg)) {
        setVMode(parseVMode(arg->value));
      } else if (!strcmp(OPTION_DEVICE, arg->arg)) {
        setEnabledDevices(getEnabledDevices() | parseDevice(arg->value));
      } else if (!strcmp(OPTION_IP_ADDRESS, arg->arg)) {
        setIPAddress(arg->value);
      } else if (!strcmp(OPTION_PROBE_DELAY, arg->arg)) {
        setProbeDelay(arg->value ? atoi(arg->value) : 0);
      } else if (!strcmp(OPTION_NEUTRINO, arg->arg) && arg->value && arg->value[0] != '\0') {
        setNeutrinoElfPath(arg->value);
      }
    }
    arg = arg->next;
  }
  freeArgumentList(options);

  return 0;
}
