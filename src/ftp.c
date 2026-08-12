#include "ftp.h"
#include "common.h"
#include "dprintf.h"
#include <ctype.h>
#include <fcntl.h>
#include <iopcontrol.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h>

// Embedded IOP modules (see irx.cmake)
#define IRX_DEFINE(mod)                                                                                                                              \
  extern unsigned char mod##_irx[] __attribute__((aligned(16)));                                                                                     \
  extern uint32_t size_##mod##_irx

IRX_DEFINE(iomanX);
IRX_DEFINE(fileXio);
IRX_DEFINE(sio2man);
IRX_DEFINE(mcman);
IRX_DEFINE(mcserv);
IRX_DEFINE(freepad);
IRX_DEFINE(ps2dev9);
IRX_DEFINE(ps2ip);
IRX_DEFINE(smap_ps2ip);
IRX_DEFINE(ps2ftpd);

static int execModule(const char *name, unsigned char *irx, uint32_t size, uint32_t argLen, const char *args) {
  int iopret = 0;
  DPRINTF("FTP: loading %s\n", name);
  int ret = SifExecModuleBuffer(irx, size, argLen, (char *)args, &iopret);
  if (ret < 0)
    return ret;
  if (iopret == 1)
    return -1;
  return 0;
}

// Reads IP, netmask and gateway from SYS-CONF/IPCONFIG.DAT on the memory
// card. Fields not found keep their passed-in defaults.
static void readIPConfig(char *ip, char *mask, char *gw) {
  static char ipconfigPath[] = "mcX:/SYS-CONF/IPCONFIG.DAT";
  char buf[64];
  int fd = -1, count = 0;

  for (char i = '0'; i < '2'; i++) {
    ipconfigPath[2] = i;
    fd = open(ipconfigPath, O_RDONLY);
    if (fd >= 0) {
      count = read(fd, buf, sizeof(buf) - 1);
      close(fd);
      break;
    }
  }
  if ((fd < 0) || (count <= 0))
    return;
  buf[count] = '\0';

  // Three whitespace-separated tokens: IP, netmask, gateway
  char *fields[3] = {ip, mask, gw};
  int fieldIdx = 0, pos = 0;
  for (int i = 0; (i <= count) && (fieldIdx < 3); i++) {
    if ((buf[i] != '\0') && !isspace((unsigned char)buf[i])) {
      if (pos < 15)
        fields[fieldIdx][pos++] = buf[i];
    } else if (pos > 0) {
      fields[fieldIdx][pos] = '\0';
      fieldIdx++;
      pos = 0;
    }
  }
}

int ftpStartServer(char *ipOut, int ipOutLen) {
  char ip[16] = "";
  char mask[16] = "255.255.255.0";
  char gw[16] = "";

  // Gather network config while the memory card is still accessible
  readIPConfig(ip, mask, gw);
  if (ip[0] == '\0')
    strlcpy(ip, LAUNCHER_OPTIONS.udpfsIp, sizeof(ip));
  if (ip[0] == '\0') {
    DPRINTF("FTP: no IP address available\n");
    return -1; // Refuse before touching the IOP: the dashboard stays usable
  }
  if (gw[0] == '\0') {
    // Derive a gateway from the IP (x.y.z.1)
    strlcpy(gw, ip, sizeof(gw));
    char *lastDot = strrchr(gw, '.');
    if (lastDot != NULL)
      strcpy(lastDot, ".1");
  }
  DPRINTF("FTP: ip=%s mask=%s gw=%s\n", ip, mask, gw);

  // Reboot the IOP fresh: the UDPFS stack (ministack/smap) and the FTP
  // stack (ps2ip/smap) cannot drive the network adapter at the same time
  fileXioExit();
  while (!SifIopReset("", 0)) {
  }
  while (!SifIopSync()) {
  }
  sceSifInitRpc(0);
  sbv_patch_enable_lmb();
  sbv_patch_disable_prefix_check();

  // ps2ip takes IP configuration as positional arguments
  char ipArgs[48];
  uint32_t ipArgsLen = 0;
  ipArgsLen += snprintf(&ipArgs[ipArgsLen], sizeof(ipArgs) - ipArgsLen, "%s", ip) + 1;
  ipArgsLen += snprintf(&ipArgs[ipArgsLen], sizeof(ipArgs) - ipArgsLen, "%s", mask) + 1;
  ipArgsLen += snprintf(&ipArgs[ipArgsLen], sizeof(ipArgs) - ipArgsLen, "%s", gw) + 1;

  const struct {
    const char *name;
    unsigned char *irx;
    uint32_t *size;
    uint32_t argLen;
    const char *args;
  } mods[] = {
      {"iomanX", iomanX_irx, &size_iomanX_irx, 0, NULL},
      {"fileXio", fileXio_irx, &size_fileXio_irx, 0, NULL},
      {"sio2man", sio2man_irx, &size_sio2man_irx, 0, NULL},
      {"mcman", mcman_irx, &size_mcman_irx, 0, NULL},
      {"mcserv", mcserv_irx, &size_mcserv_irx, 0, NULL},
      {"freepad", freepad_irx, &size_freepad_irx, 0, NULL},
      {"ps2dev9", ps2dev9_irx, &size_ps2dev9_irx, 0, NULL},
      {"ps2ip", ps2ip_irx, &size_ps2ip_irx, 0, NULL},
      {"smap-ps2ip", smap_ps2ip_irx, &size_smap_ps2ip_irx, 0, NULL},
      {"ps2ftpd", ps2ftpd_irx, &size_ps2ftpd_irx, 0, NULL},
  };

  for (int i = 0; i < (int)(sizeof(mods) / sizeof(mods[0])); i++) {
    uint32_t argLen = mods[i].argLen;
    const char *args = mods[i].args;
    if (!strcmp(mods[i].name, "ps2ip")) {
      argLen = ipArgsLen;
      args = ipArgs;
    }
    int ret = execModule(mods[i].name, mods[i].irx, *mods[i].size, argLen, args);
    if (ret != 0) {
      DPRINTF("FTP: %s failed: %d\n", mods[i].name, ret);
      return ret;
    }
    if (!strcmp(mods[i].name, "fileXio"))
      fileXioInit();
  }

  strlcpy(ipOut, ip, ipOutLen);
  return 0;
}
