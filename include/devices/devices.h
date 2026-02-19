#ifndef _DEVICES_DEVICES_H_
#define _DEVICES_DEVICES_H_

// Supported device types
typedef enum {
  Device_None = 0,
  Device_Basic = (1 << 0),
  Device_HDD = (1 << 1),
  Device_MMCE = (1 << 2),
  Device_MX4SIO = (1 << 3),
  Device_UDPFS = (1 << 4),
  Device_USB = (1 << 5),
  Device_iLink = (1 << 6),
  Device_BDM = (1 << 7) // Internal mode for guessed device types. Not used for module init
} DeviceType;

// Defined initialized device
typedef struct Device {
  char *mountpoint; // Device mountpoint
  DeviceType type;  // Device type
  uint8_t index;    // Device index
} Device;

// Linked list of devices
typedef struct {
  Device *current;  // Current device
  DeviceList *next; // Next device in chain
} DeviceListEntry;

// Loads device modules
int loadDeviceModules(DeviceType dtype);

// Reboots IOP and initializes basic devices
int rebootIOP();

// Implemented in devices/list.c

// Returns all known devices
DeviceListEntry *getDevices();

// Frees device list. Frees devices if freeDevices is not 0
void freeDeviceList(DeviceListEntry *list, int freeDevices);

#endif
