#ifndef _BACKENDS_H_
#define _BACKENDS_H_

#include "backends/target.h"
#include "devices/devices.h"

#define MAX_DEVICES 20

// If conflict reinit occurred, initBackend return value has this set so UI can reload (e.g. (return & LIST_INVALIDATED)).
#define LIST_INVALIDATED 0x80000000

// Must scan the device for titles and fill device->titles
typedef int (*titleScanFunc)(struct BackendDevice *device);
// Must sync the device (e.g. flush PFS for HDL)
typedef void (*syncFunc)(struct BackendDevice *device);
// Must release device resources (e.g. unmount PFS for HDL). Called before launch, exit, or IOP reboot.
typedef void (*cleanupFunc)(struct BackendDevice *device);

// Backend device entry
struct BackendDevice {
  char *mountpoint;    // Device mountpoint
  syncFunc sync;       // Must sync the device
  cleanupFunc cleanup; // Must release resources (unmount PFS, etc.). Can be NULL.
  titleScanFunc scan;  // Function used for scanning the device for titles. Can be NULL if device must be ignored during scanning
  struct BackendDevice
      *metadev;             // If set, cover art and options will be loaded from metadata device instead of this device. Set during initialization
  DeviceType type;          // Backend device type (Device_HDD, Device_ATA, etc.)
  uint8_t index;            // BDM internal device driver number, must be used for passing paths to Neutrino
  TargetList *titles;       // Per-device title list (owned by backend; UI read-only)
  int lastLaunchedTitleIdx; // Index into titles of last launched title; -1 if none or not found
  uint32_t lastLaunchedTimestamp; // From lastTitle.bin when loaded; 0 = none/invalid. Used by getLastLaunchedTarget().
};

//
// Getters (UI and other callers should use these)
//
int getBackendDeviceCount(void);
struct BackendDevice *getBackendDeviceAt(int index);
TargetList *getBackendDeviceTitles(struct BackendDevice *device);
int getBackendDeviceCountByType(DeviceType type);
struct BackendDevice *getBackendDeviceOfType(DeviceType type, int index);
// Returns the target that was last launched across all devices (by lastTitle.bin timestamp). NULL if none.
Target *getLastLaunchedTarget(void);

//
// Setters
//
void scanBackendDevice(struct BackendDevice *device);
void updateTargetFlagsAndPersist(struct BackendDevice *device, Target *target, uint32_t flags);

// Initializes backend for the given device type (loads device modules, removes conflicting backends, runs backend init).
// device must be a single type (e.g. Device_MMCE or Device_HDD), not a mask. Call once per type.
// Returns number of devices added, or negative on error.
// If the backend for this type is already initialized, returns 0 without adding duplicate devices.
// If skipDeviceInit is 1, will skip initializing device modules
// If the device list was invalidated (e.g. conflict reinit), the high bit is set so UI can reload views (e.g. return value & LIST_INVALIDATED).
int initBackend(DeviceType device, int skipDeviceInit);
// Initializes device, backend and target for the given image path for forwarder boot and returns target.
// Returns NULL on error.
Target *initializeTargetForImage(const char *image);
// Returns the backend device whose mountpoint is a prefix of path, or NULL if none.
struct BackendDevice *getBackendDeviceForPath(const char *path);
// Copies the mountpoint prefix of path (up to and including the first ':'). Returns 0 on success.
int getMountpointFromPath(const char *path, char *buf, size_t bufSize);
void freeBackendDeviceTitles(struct BackendDevice *device);
void freeAllBackendTitles(void);
// Rescans all backend devices (rebuilds device->titles). Call before rebuilding the title list view.
void rescanAllBackendDevices(void);
// Drops backends whose type is not in getEnabledDevices() (sync/cleanup, free titles and mountpoints).
void removeBackendsDisabledInConfig(void);
// Fills out[] with each DeviceType bit set in config (getEnabledDevices()), up to maxCount. Returns count.
int getEnabledDeviceTypesArray(DeviceType *out, int maxCount);
// Fills out[] with each DeviceType supported, up to maxCount. Returns count.
int getSupportedDeviceTypesArray(DeviceType *out, int maxCount);
// Runs cleanup on all backend devices (unmount PFS, etc.). Call before launch, exit, or IOP reboot.
void cleanupAllBackends(void);

//
// Device-specific scanning (take BackendDevice *, operate on device->titles)
//
int findISO(struct BackendDevice *device);
int findHDLTargets(struct BackendDevice *device);

#endif
