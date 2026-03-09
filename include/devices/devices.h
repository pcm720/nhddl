#ifndef _DEVICES_H_
#define _DEVICES_H_

#include <stddef.h>
#include <stdint.h>

// Supported device types
typedef enum {
  Device_None = 0,
  Device_Basic = (1 << 0),
  Device_ATA = (1 << 1), // exFAT on internal HDD
  Device_MMCE = (1 << 2),
  Device_UDPFS = (1 << 3),
  Device_HDD = (1 << 4), // APA on internal HDD
  Device_USB = (1 << 5),
  Device_MX4SIO = (1 << 6),
  Device_iLink = (1 << 7),
  Device_BDM = (1 << 8), // Internal mode for guessed device types. Not used for module init
} DeviceType;

// Defined initialized device
typedef struct {
  char *mountpoint; // Device mountpoint
  DeviceType type;  // Device type
  uint8_t index;    // Device index
} Device;

// Loads device modules
int loadDeviceModules(DeviceType dtype);

// Reboots IOP and initializes basic devices
int rebootIOP();

// Returns max device count for the given type and writes base mountpoint (e.g. "ata", "usb") into baseMountpoint buffer. Returns 0 or negative if
// type has no supported device info.
int getDeviceInfo(DeviceType type, char *baseMountpoint, size_t baseMountpointSize);

// Returns bitmask of device types that conflict with the given type (e.g. Device_MX4SIO for Device_MMCE).
DeviceType getConflictingDeviceTypes(DeviceType type);

// Returns non-zero if the given device type has been loaded
int isDeviceLoaded(DeviceType type);

#endif
