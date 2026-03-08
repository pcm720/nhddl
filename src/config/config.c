#include "config/config.h"
#include <stdlib.h>
#include <string.h>

Config config = {0};

// Getters
VModeType getVMode(void) { return config.vmode; }

DeviceType getEnabledDevices(void) { return config.enabledDevices; }

const char *getIPAddress(void) { return config.ipAddr; }

const char *getImage(void) { return config.image; }

int getNoInit(void) { return config.noInit; }

int getProbeDelay(void) { return config.probeDelay; }

const Device *getBootDevice(void) { return &config.bootDevice; }

const char *getNHDDLRoot(void) { return config.rootPath; }

const char *getNeutrinoPath(void) { return config.neutrinoPath; }

// Setters
void setVMode(VModeType v) { config.vmode = v; }

void setEnabledDevices(DeviceType v) { config.enabledDevices = v; }

void setIPAddress(const char *v) {
  if (v)
    strncpy(config.ipAddr, v, sizeof(config.ipAddr) - 1);
  config.ipAddr[sizeof(config.ipAddr) - 1] = '\0';
}

void setImage(const char *v) {
  if (config.image)
    free(config.image);
  config.image = v ? strdup(v) : NULL;
}

void setNoInit(int v) { config.noInit = v; }

void setProbeDelay(int v) { config.probeDelay = v; }

void setBootDevice(const Device *v) {
  if (v) {
    if (config.bootDevice.mountpoint)
      free(config.bootDevice.mountpoint);
    config.bootDevice.mountpoint = v->mountpoint ? strdup(v->mountpoint) : NULL;
    config.bootDevice.type = v->type;
    config.bootDevice.index = v->index;
  }
}

void setNHDDLRoot(const char *v) {
  if (v)
    strncpy(config.rootPath, v, PATH_MAX);
  config.rootPath[PATH_MAX] = '\0';
}

void setNeutrinoPath(const char *v) {
  if (v)
    strncpy(config.neutrinoPath, v, PATH_MAX);
  config.neutrinoPath[PATH_MAX] = '\0';
}
