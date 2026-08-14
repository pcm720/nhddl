#ifndef _FTP_H_
#define _FTP_H_

#include <stdint.h>

// Dashboard FTP support. The normal path loads PS2IP, socket-backed UDPFS and
// ps2ftpd together, so FTP stays available while the title list is active.
// The older IOP-replacement entry point remains below as a diagnostic/fallback
// implementation, but the UI no longer invokes it.
//
// The live shared server uses the launcher's UDPFS address plus the mask and
// gateway from SYS-CONF/IPCONFIG.DAT. The ftp.cfg helpers below belong to the
// older replacement-stack diagnostic path and are not used by the title-list
// UI; they remain available for troubleshooting without changing the proven
// resident PS2IP/SMAP path.

typedef enum {
  FTP_NET_STATIC = 0,
  FTP_NET_DHCP = 1,
} FtpNetMode;

typedef struct {
  FtpNetMode mode;
  char ip[16];
  char mask[16];
  char gw[16];
} FtpConfig;

// Loads the persisted FTP config into cfg. If nhddl/ftp.cfg doesn't exist,
// seeds defaults from IPCONFIG.DAT (static mode) and returns 0 without
// writing a file. Always leaves cfg populated with usable values.
void ftpLoadConfig(FtpConfig *cfg);

// Persists cfg to nhddl/ftp.cfg on the first writable device.
void ftpSaveConfig(const FtpConfig *cfg);

// Starts the FTP server using cfg. On success returns 0 and writes the IP the
// server is reachable at into ipOut (the DHCP-assigned address in DHCP mode).
// On failure the IOP may already have been rebooted: the caller must relaunch
// the dashboard either way.
int ftpStartServer(const FtpConfig *cfg, char *ipOut, int ipOutLen);

// Diagnostic state survives the IOP swap in EE memory. The UI displays it
// verbatim on failure and also writes it to the memory card for later review.
const char *ftpGetLastDiagnostic(void);
void ftpWriteDiagnostic(const FtpConfig *cfg, int result);

// True once the replacement IOP has successfully loaded freepad.irx.
int ftpPadAvailable(void);

// Cleanly releases the currently loaded network stack's DEV9/SMAP hardware
// before an IOP reset or an ELF handoff. This must be called while fileXio is
// still alive, because DDIOC_OFF is exposed through the dev9x: device.
int ftpShutdownNetwork(void);

// Shared-stack dashboard mode. The SMAP argument builder is used before the
// PS2IP driver loads; attach verifies/applies that same configuration through
// ps2ips before UDPFS and ps2ftpd start.
char *ftpBuildSmapArguments(uint32_t *argLength);
int ftpAttachSharedNetwork(char *ipOut, int ipOutLen);

// Background ps2ftpd status exposed to the game-list UI.
void ftpSetBackgroundStatus(int result);
int ftpIsBackgroundRunning(void);
int ftpGetBackgroundError(void);
const char *ftpGetBackgroundIP(void);

#endif
