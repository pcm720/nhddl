#include "config/config.h"
#include "devices/hdd.h"
#include <stdlib.h>
#include <string.h>

Config config = {0};

// Getters
VModeType getVMode(void) { return config.vmode; }
DeviceType getEnabledDevices(void) { return config.enabledDevices; }
const char *getIPAddress(void) { return config.ipAddr; }
int getProbeDelayWithDefaults(void) { return (config.probeDelay) ? config.probeDelay : 10; }
int getProbeDelay(void) { return config.probeDelay; }
const char *getNHDDLRawRoot(void) { return config.rootPath; }
const char *getNHDDLRoot(void) {
  if (!strncmp(config.rootPath, "hdd", 3))
    return getNHDDLHDDRoot();

  return config.rootPath;
}
const char *getNeutrinoPath(void) { return config.neutrinoPath; }
const char *getImage(void) { return config.image; }
int getNoInit(void) { return config.noInit; }
int getFakeDEV9(void) { return config.fakeDEV9; }

// Setters
void setVMode(VModeType v) { config.vmode = v; }
void setEnabledDevices(DeviceType v) { config.enabledDevices = v; }
void setIPAddress(const char *v) {
  if (v)
    strncpy(config.ipAddr, v, sizeof(config.ipAddr) - 1);
  config.ipAddr[sizeof(config.ipAddr) - 1] = '\0';
}
void setProbeDelay(int v) { config.probeDelay = v; }
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
void setImage(const char *v) {
  if (config.image)
    free(config.image);
  config.image = v ? strdup(v) : NULL;
}
void setNoInit(int v) { config.noInit = v; }
void setFakeDEV9(int v) { config.fakeDEV9 = v; }
