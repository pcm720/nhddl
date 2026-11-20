#include "config/nhddl.h"
#include "backends/backends.h"
#include "config/arguments.h"
#include "config/common.h"
#include "config/config.h"
#include "dprintf.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

const char optionsFile[] = "nhddl.cnf";

// Supported options
#define OPTION_DEVICE "device"
#define OPTION_IP_ADDRESS "ip"
#define OPTION_PROBE_DELAY "probe_delay"
#define OPTION_NEUTRINO "neutrino"
// Forwarder mode-exclusive flags
#define OPTION_IMAGE "dvd"
#define OPTION_NO_INIT "noinit"
#define OPTION_FAKEDEV9 "dev9f"
// UI flags
#define OPTION_VMODE "video"
#define OPTION_WIDESCREEN "widescreen"
#define OPTION_FOLDER_VIEW "folder_view"
#define OPTION_AUTOLAUNCH "autolaunch"
#define OPTION_LANG "lang"

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
  if (!strncmp(modeStr, "480i", 4))
    return VMode_480i;
  if (!strncmp(modeStr, "576i", 4))
    return VMode_576i;
  if (!strncmp(modeStr, "480p", 4))
    return VMode_480p;
  if (!strncmp(modeStr, "576p", 4))
    return VMode_576p;
  if (!strncmp(modeStr, "720p", 4))
    return VMode_720p;
  return VMode_NONE;
}

// Returns string for video mode for config file output
static const char *vmodeToStr(VModeType v) {
  switch (v) {
  case VMode_480i:
    return "480i";
  case VMode_576i:
    return "576i";
  case VMode_480p:
    return "480p";
  case VMode_576p:
    return "576p";
  case VMode_720p:
    return "720p";
  default:
    return "480i";
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

// Writes one line to fd. Returns 0 on success, -EIO on write failure.
static int writeOptionLine(int fd, const char *line) {
  int len = (int)strlen(line);
  if (len <= 0)
    return 0;
  if (write(fd, line, len) != len)
    return -EIO;
  return 0;
}

// Writes one -device=<name> line for each bit set in mask. Returns 0 on success.
static int writeDeviceOptions(int fd, DeviceType mask) {
  static const DeviceType bits[] = {
      Device_ATA, Device_HDD, Device_MX4SIO, Device_UDPFS, Device_USB, Device_iLink, Device_MMCE,
  };
  char line[64];
  for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); i++) {
    if (mask & bits[i]) {
      const char *name = deviceToStr(bits[i]);
      if (!name)
        continue;
      snprintf(line, sizeof(line), FMT_OPTION_STR, OPTION_DEVICE, name);
      if (writeOptionLine(fd, line))
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
    } else if (!strcmp(OPTION_FOLDER_VIEW, arg)) {
      DPRINTF("config/nhddl: using folder view\n");
      setFolderView(1);
    } else if (!strcmp(OPTION_AUTOLAUNCH, arg)) {
      DPRINTF("config/nhddl: using autolaunch timeout %s\n", val);
      setAutolaunchTimeout(val ? atoi(val) : 0);
    } else if (val && !strcmp(OPTION_LANG, arg)) {
      DPRINTF("config/nhddl: using language %s\n", val);
      setLang(val);
    }
  }
}

// Loads NHDDL options from optionsFile in config root path
int loadOptions(void) {
  const char *root = getNHDDLRoot();
  if (!root || root[0] == '\0')
    return -ENOENT;
  char lineBuffer[PATH_MAX];
  joinDevicePath(lineBuffer, sizeof(lineBuffer), root, optionsFile);

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
      } else if (!strcmp(OPTION_FOLDER_VIEW, arg->arg)) {
        setFolderView(1);
      } else if (!strcmp(OPTION_AUTOLAUNCH, arg->arg)) {
        setAutolaunchTimeout(arg->value ? atoi(arg->value) : 0);
      } else if (!strcmp(OPTION_LANG, arg->arg) && arg->value && arg->value[0]) {
        setLang(arg->value);
      }
    }
    arg = arg->next;
  }
  freeArgumentList(options);

  return 0;
}

// Saves current config to optionsFile in config root path.
// Returns 0 on success, -ENOENT if no root, -EBADF on open failure, -EIO on write/close failure.
int saveOptions(void) {
  const char *root = getNHDDLRoot();
  if (!root || root[0] == '\0')
    return -ENOENT;
  char path[PATH_MAX];
  joinDevicePath(path, sizeof(path), root, optionsFile);

  DPRINTF("config/nhddl: saving options to %s\n", path);
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
  if (fd < 0) {
    DPRINTF("config/nhddl: failed to open %s\n", path);
    return -EBADF;
  }

  char line[PATH_MAX];
  int err = 0;
  VModeType vmode = getVMode();
  if (vmode != VMode_NONE) {
    snprintf(line, sizeof(line), FMT_OPTION_STR, OPTION_VMODE, vmodeToStr(vmode));
    if (writeOptionLine(fd, line))
      err = -EIO;
  }
  if (!err && writeDeviceOptions(fd, getEnabledDevices()))
    err = -EIO;
  if (!err && getIPAddress()[0] != '\0') {
    snprintf(line, sizeof(line), FMT_OPTION_STR, OPTION_IP_ADDRESS, getIPAddress());
    if (writeOptionLine(fd, line))
      err = -EIO;
  }
  if (!err && getProbeDelay() > 0) {
    snprintf(line, sizeof(line), FMT_OPTION_INT, OPTION_PROBE_DELAY, getProbeDelay());
    if (writeOptionLine(fd, line))
      err = -EIO;
  }
  if (!err && getNeutrinoPath()[0] != '\0') {
    snprintf(line, sizeof(line), FMT_OPTION_STR, OPTION_NEUTRINO, getNeutrinoPath());
    if (writeOptionLine(fd, line))
      err = -EIO;
  }
  if (!err && getNoInit()) {
    snprintf(line, sizeof(line), FMT_OPTION_FLAG, OPTION_NO_INIT);
    if (writeOptionLine(fd, line))
      err = -EIO;
  }
  if (!err && getWidescreen()) {
    snprintf(line, sizeof(line), FMT_OPTION_FLAG, OPTION_WIDESCREEN);
    if (writeOptionLine(fd, line))
      err = -EIO;
  }
  if (!err && getFolderView()) {
    snprintf(line, sizeof(line), FMT_OPTION_FLAG, OPTION_FOLDER_VIEW);
    if (writeOptionLine(fd, line))
      err = -EIO;
  }
  if (!err && getAutolaunchTimeout() > 0) {
    snprintf(line, sizeof(line), FMT_OPTION_INT, OPTION_AUTOLAUNCH, getAutolaunchTimeout());
    if (writeOptionLine(fd, line))
      err = -EIO;
  }
  if (!err && strcmp(getLang(), "en") != 0) {
    snprintf(line, sizeof(line), FMT_OPTION_STR, OPTION_LANG, getLang());
    if (writeOptionLine(fd, line))
      err = -EIO;
  }
  if (close(fd))
    err = -EIO;
  return err;
}
