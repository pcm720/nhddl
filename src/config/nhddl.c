#include "config/nhddl.h"
#include "backends/backends.h"
#include "config/arguments.h"
#include "config/config.h"
#include "dprintf.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char optionsFile[] = "nhddl.cnf";

// Supported options
#define OPTION_DEVICE "device"
#define OPTION_IP_ADDRESS "ip_addr"
#define OPTION_PROBE_DELAY "probe_delay"
#define OPTION_NEUTRINO "neutrino"
// Forwarder mode-exclusive flags
#define OPTION_IMAGE "dvd"
#define OPTION_NO_INIT "noinit"
#define OPTION_FAKEDEV9 "dev9f"
// UI flags
#define OPTION_VMODE "video"
#define OPTION_WIDESCREEN "widescreen"
#define OPTION_AUTOLAUNCH "autolaunch"

// Config file line formats
#define FMT_OPTION_STR "-%s=%s\n"
#define FMT_OPTION_INT "-%s=%d\n"
#define FMT_OPTION_FLAG "-%s\n"

// Parses device string into DeviceType
DeviceType parseDevice(const char *val) {
  if (!strncmp(val, "ata", 3))
    return Device_ATA;
  if (!strncmp(val, "hdl", 3))
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

// Returns string for video mode for config file output
static const char *vmodeToStr(VModeType v) {
  switch (v) {
  case VMode_NTSC:
    return "ntsc";
  case VMode_PAL:
    return "pal";
  case VMode_480p:
    return "480p";
  case VMode_720p:
    return "720p";
  default:
    return "ntsc";
  }
}

static const char *deviceToStr(DeviceType type) {
  switch (type) {
  case Device_ATA:
    return "ata";
  case Device_MMCE:
    return "mmce";
  case Device_HDD:
    return "hdl";
  case Device_MX4SIO:
    return "mx4sio";
  case Device_UDPFS:
    return "udpfs";
  case Device_USB:
    return "usb";
  case Device_iLink:
    return "ilink";
  }
  return NULL;
}

// Writes one -device=<name> line for each bit set in mask. Returns 0 on success.
static int writeDeviceOptions(FILE *f, DeviceType mask) {
  static const DeviceType bits[] = {
      Device_ATA, Device_HDD, Device_MX4SIO, Device_UDPFS, Device_USB, Device_iLink, Device_MMCE,
  };
  for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
    if (mask & bits[i]) {
      const char *name = deviceToStr(bits[i]);
      if (name && fprintf(f, FMT_OPTION_STR, OPTION_DEVICE, name) < 0)
        return -EIO;
    }
  }
  return 0;
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
      DPRINTF("config/nhddl: using video mode %s\n", val);
      setVMode(parseVMode(val));
    } else if (val && !strcmp(OPTION_DEVICE, arg)) {
      DPRINTF("config/nhddl: enabling device %s\n", val);
      setEnabledDevices(getEnabledDevices() | parseDevice(val));
    } else if (val && !strcmp(OPTION_IP_ADDRESS, arg)) {
      DPRINTF("config/nhddl: using IP %s\n", val);
      setIPAddress(val);
    } else if (val && !strcmp(OPTION_IMAGE, arg)) {
      DPRINTF("config/nhddl: using image %s\n", val);
      setImage(val);
    } else if (!strcmp(OPTION_NO_INIT, arg)) {
      DPRINTF("config/nhddl: skipping IOP init\n");
      setNoInit(1);
    } else if (!strcmp(OPTION_FAKEDEV9, arg)) {
      DPRINTF("config/nhddl: will fake DEV9\n");
      setFakeDEV9(1);
    } else if (!strcmp(OPTION_PROBE_DELAY, arg)) {
      DPRINTF("config/nhddl: using probe delay %s\n", val);
      setProbeDelay(val ? atoi(val) : 0);
    } else if (!strcmp(OPTION_NEUTRINO, arg)) {
      DPRINTF("config/nhddl: using custom Neutrino path: %s\n", val);
      setNeutrinoPath(val);
    } else if (!strcmp(OPTION_WIDESCREEN, arg)) {
      DPRINTF("config/nhddl: using widescreen\n", val);
      setWidescreen(1);
    } else if (!strcmp(OPTION_AUTOLAUNCH, arg)) {
      DPRINTF("config/nhddl: using autolaunch timeout %s\n", val);
      setAutolaunchTimeout(val ? atoi(val) : 0);
    }
  }
}

// Loads NHDDL options from optionsFile in config root path
int loadOptions(void) {
  const char *root = getNHDDLRoot();
  if (!root || root[0] == '\0')
    return -ENOENT;
  char lineBuffer[PATH_MAX];
  snprintf(lineBuffer, sizeof(lineBuffer), "%s%s", root, optionsFile);

  DPRINTF("config/nhddl: loading options file from %s\n", lineBuffer);
  ArgumentList *options = calloc(1, sizeof(ArgumentList));
  if (loadArgumentList(options, NULL, lineBuffer)) {
    DPRINTF("config/nhddl: can't load options file, will use defaults\n");
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
        setNeutrinoPath(arg->value);
      } else if (!strcmp(OPTION_WIDESCREEN, arg->arg)) {
        setWidescreen(1);
      } else if (!strcmp(OPTION_AUTOLAUNCH, arg->arg)) {
        setAutolaunchTimeout(arg->value ? atoi(arg->value) : 0);
      }
    }
    arg = arg->next;
  }
  freeArgumentList(options);

  return 0;
}

// Saves current config to optionsFile in config root path
int saveOptions(void) {
  const char *root = getNHDDLRoot();
  if (!root || root[0] == '\0')
    return -ENOENT;
  char path[PATH_MAX];
  snprintf(path, sizeof(path), "%s%s", root, optionsFile);

  DPRINTF("config/nhddl: saving options to %s\n", path);
  FILE *f = fopen(path, "w");
  if (!f) {
    DPRINTF("config/nhddl: failed to open %s\n", path);
    return -EIO;
  }

  int err = 0;
  VModeType vmode = getVMode();
  if (vmode != VMode_NONE) {
    if (fprintf(f, FMT_OPTION_STR, OPTION_VMODE, vmodeToStr(vmode)) < 0)
      err = -EIO;
  }
  if (writeDeviceOptions(f, getEnabledDevices()))
    err = -EIO;
  if (getIPAddress()[0] != '\0') {
    if (fprintf(f, FMT_OPTION_STR, OPTION_IP_ADDRESS, getIPAddress()) < 0)
      err = -EIO;
  }
  if (getProbeDelay() > 0) {
    if (fprintf(f, FMT_OPTION_INT, OPTION_PROBE_DELAY, getProbeDelay()) < 0)
      err = -EIO;
  }
  if (getNeutrinoPath()[0] != '\0') {
    if (fprintf(f, FMT_OPTION_STR, OPTION_NEUTRINO, getNeutrinoPath()) < 0)
      err = -EIO;
  }
  if (getNoInit()) {
    if (fprintf(f, FMT_OPTION_FLAG, OPTION_NO_INIT) < 0)
      err = -EIO;
  }
  if (getWidescreen()) {
    if (fprintf(f, FMT_OPTION_FLAG, OPTION_WIDESCREEN) < 0)
      err = -EIO;
  }
  if (getAutolaunchTimeout() > 0) {
    if (fprintf(f, FMT_OPTION_INT, OPTION_AUTOLAUNCH, getAutolaunchTimeout()) < 0)
      err = -EIO;
  }
  if (fclose(f))
    err = -EIO;
  return err;
}
