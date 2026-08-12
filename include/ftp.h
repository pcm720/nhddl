#ifndef _FTP_H_
#define _FTP_H_

// FTP server mode.
//
// Reboots the IOP with a TCP/IP + FTP server module set (ps2ip, SMAP,
// ps2ftpd — the same daemon uLaunchELF's PS2Net uses), replacing every other
// device backend including UDPFS. Because of that, the only way out of FTP
// mode is a full dashboard relaunch (LoadExecPS2 of the NHDDL ELF).
//
// Network configuration comes from SYS-CONF/IPCONFIG.DAT on the memory card
// (read before the IOP reboot), falling back to the udpfs_ip option.

// Starts the FTP server. On success returns 0 and writes the IP address the
// server is reachable at into ipOut. On failure the IOP may already have
// been rebooted: the caller must relaunch the dashboard either way.
int ftpStartServer(char *ipOut, int ipOutLen);

#endif
