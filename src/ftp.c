#include <ps2ips.h>

#include "ftp.h"
#include "common.h"
#include "devices/devices.h"
#include "dprintf.h"
#include "options.h"
#include "ui/ui.h"
#include <ctype.h>
#include <delaythread.h>
#include <errno.h>
#include <fcntl.h>
#include <hdd-ioctl.h>
#include <iopcontrol.h>
#include <iopheap.h>
#include <kernel.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <sifrpc.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
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
IRX_DEFINE(poweroff);
IRX_DEFINE(ps2dev9);
IRX_DEFINE(ps2ip);
IRX_DEFINE(ps2ips);
IRX_DEFINE(smap_ps2ip);
IRX_DEFINE(ps2ftpd);

static const char ftpConfigFile[] = "/ftp.cfg";
static int ps2ipClientReady = 0;
static int replacementPadReady = 0;
static char ftpLastDiagnostic[256] = "stage=idle";
static int backgroundFtpRunning = 0;
static int backgroundFtpError = 0;
static char backgroundFtpIP[16] = "";

static int applyNetworkConfig(const FtpConfig *cfg, const char *ip,
                              const char *mask, const char *gw,
                              char *ipOut, int ipOutLen);

static void setFtpDiagnostic(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(ftpLastDiagnostic, sizeof(ftpLastDiagnostic), format, args);
  va_end(args);
  ftpLastDiagnostic[sizeof(ftpLastDiagnostic) - 1] = '\0';
  DPRINTF("FTP diagnostic: %s\n", ftpLastDiagnostic);
}

const char *ftpGetLastDiagnostic(void) {
  return ftpLastDiagnostic;
}

int ftpPadAvailable(void) {
  return replacementPadReady;
}

void ftpWriteDiagnostic(const FtpConfig *cfg, int result) {
  static const char *paths[] = {
      "mc0:/SYS-CONF/NHDDL-FTP.LOG",
      "mc1:/SYS-CONF/NHDDL-FTP.LOG",
  };

  for (int i = 0; i < (int)(sizeof(paths) / sizeof(paths[0])); i++) {
    FILE *file = fopen(paths[i], "wb");
    if (file == NULL)
      continue;

    fprintf(file, "result=%d\n", result);
    fprintf(file, "mode=%s\n", (cfg->mode == FTP_NET_DHCP) ? "dhcp" : "static");
    fprintf(file, "requested_ip=%s\n", cfg->ip);
    fprintf(file, "requested_mask=%s\n", cfg->mask);
    fprintf(file, "requested_gateway=%s\n", cfg->gw);
    fprintf(file, "%s\n", ftpLastDiagnostic);
    fclose(file);
    DPRINTF("FTP: wrote diagnostic to %s\n", paths[i]);
    return;
  }

  DPRINTF("FTP: could not persist diagnostic to either memory card\n");
}

/*
 * Do not use inet_addr()/inet_ntoa() here.  Those EE libcglue entry points
 * require an installed _libcglue_fdman_inet_ops implementation.  ps2ips is
 * only the socket/config RPC bridge and deliberately installs no inet ops,
 * so inet_addr() returns 0 and inet_ntoa() returns NULL in this mode.  That
 * previously changed the correctly initialized SMAP address to 0.0.0.0.
 *
 * lwIP stores IPv4 addresses in network byte order. On the little-endian EE
 * that means the first dotted octet occupies the low byte of s_addr.
 */
static int parseIPv4(const char *text, u32 *address) {
  u32 result = 0;

  if ((text == NULL) || (address == NULL))
    return 0;

  for (int part = 0; part < 4; part++) {
    unsigned int value = 0;
    int digits = 0;

    while ((*text >= '0') && (*text <= '9')) {
      value = (value * 10) + (unsigned int)(*text - '0');
      if (value > 255)
        return 0;
      text++;
      digits++;
    }
    if (digits == 0)
      return 0;

    result |= (u32)value << (part * 8);
    if (part < 3) {
      if (*text != '.')
        return 0;
      text++;
    } else if (*text != '\0') {
      return 0;
    }
  }

  *address = result;
  return 1;
}

static int formatIPv4(u32 address, char *output, int outputLen) {
  if ((output == NULL) || (outputLen <= 0))
    return 0;

  int written = snprintf(output, outputLen, "%u.%u.%u.%u",
                         (unsigned int)(address & 0xff),
                         (unsigned int)((address >> 8) & 0xff),
                         (unsigned int)((address >> 16) & 0xff),
                         (unsigned int)((address >> 24) & 0xff));
  return (written > 0) && (written < outputLen);
}

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

// Network settings needed before UDPFS itself is mounted.  The explicit
// udpfs_ip option remains authoritative; IPCONFIG.DAT supplies the mask and
// gateway (and the address when no option was provided).
static int loadStartupNetworkConfig(FtpConfig *cfg) {
  cfg->mode = FTP_NET_STATIC;
  cfg->ip[0] = '\0';
  strlcpy(cfg->mask, "255.255.255.0", sizeof(cfg->mask));
  cfg->gw[0] = '\0';
  readIPConfig(cfg->ip, cfg->mask, cfg->gw);

  if (LAUNCHER_OPTIONS.udpfsIp[0] != '\0')
    strlcpy(cfg->ip, LAUNCHER_OPTIONS.udpfsIp, sizeof(cfg->ip));
  else if (cfg->ip[0] != '\0')
    strlcpy(LAUNCHER_OPTIONS.udpfsIp, cfg->ip,
            sizeof(LAUNCHER_OPTIONS.udpfsIp));

  if (cfg->ip[0] == '\0')
    return -ENOENT;
  if (cfg->gw[0] == '\0') {
    strlcpy(cfg->gw, cfg->ip, sizeof(cfg->gw));
    char *lastDot = strrchr(cfg->gw, '.');
    if (lastDot != NULL)
      strcpy(lastDot, ".1");
  }
  return 0;
}

char *ftpBuildSmapArguments(uint32_t *argLength) {
  FtpConfig cfg;
  if ((argLength == NULL) || (loadStartupNetworkConfig(&cfg) < 0))
    return NULL;

  const uint32_t size = strlen(cfg.ip) + 1 + strlen(cfg.mask) + 1 +
                        strlen(cfg.gw) + 1;
  char *args = malloc(size);
  if (args == NULL)
    return NULL;

  uint32_t offset = 0;
  memcpy(args + offset, cfg.ip, strlen(cfg.ip) + 1);
  offset += strlen(cfg.ip) + 1;
  memcpy(args + offset, cfg.mask, strlen(cfg.mask) + 1);
  offset += strlen(cfg.mask) + 1;
  memcpy(args + offset, cfg.gw, strlen(cfg.gw) + 1);

  *argLength = size;
  DPRINTF("Shared network args: %s / %s / %s\n", cfg.ip, cfg.mask, cfg.gw);
  return args;
}

// Returns the first writable device (BDM/MMCE/UDPFS), preferring its metadev
static struct DeviceMapEntry *firstConfigDevice() {
  for (int i = 0; i < MAX_DEVICES; i++) {
    if ((deviceModeMap[i].mode == MODE_NONE) || (deviceModeMap[i].mode == MODE_ALL) || (deviceModeMap[i].mountpoint == NULL))
      continue;
    if (deviceModeMap[i].metadev)
      return deviceModeMap[i].metadev;
    return &deviceModeMap[i];
  }
  return NULL;
}

void ftpLoadConfig(FtpConfig *cfg) {
  // Defaults
  cfg->mode = FTP_NET_STATIC;
  cfg->ip[0] = '\0';
  strlcpy(cfg->mask, "255.255.255.0", sizeof(cfg->mask));
  cfg->gw[0] = '\0';

  struct DeviceMapEntry *device = firstConfigDevice();
  char path[PATH_MAX];
  int haveFile = 0;

  if (device != NULL) {
    buildConfigFilePath(path, device->mountpoint, ftpConfigFile);
    FILE *file = fopen(path, "rb");
    if (file != NULL) {
      haveFile = 1;
      char line[64];
      while (fgets(line, sizeof(line), file) != NULL) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (eq == NULL)
          continue;
        *eq = '\0';
        const char *val = eq + 1;
        if (!strcmp(line, "mode"))
          cfg->mode = (!strcmp(val, "dhcp")) ? FTP_NET_DHCP : FTP_NET_STATIC;
        else if (!strcmp(line, "ip"))
          strlcpy(cfg->ip, val, sizeof(cfg->ip));
        else if (!strcmp(line, "mask"))
          strlcpy(cfg->mask, val, sizeof(cfg->mask));
        else if (!strcmp(line, "gw"))
          strlcpy(cfg->gw, val, sizeof(cfg->gw));
      }
      fclose(file);
    }
  }

  // First run only: seed static defaults from IPCONFIG.DAT / udpfs_ip so the
  // initial configuration matches the known-good address. Once ftp.cfg
  // exists, the user's saved choice is authoritative.
  if (!haveFile) {
    readIPConfig(cfg->ip, cfg->mask, cfg->gw);
    if (cfg->ip[0] == '\0')
      strlcpy(cfg->ip, LAUNCHER_OPTIONS.udpfsIp, sizeof(cfg->ip));
  }
  if (cfg->gw[0] == '\0' && cfg->ip[0] != '\0') {
    strlcpy(cfg->gw, cfg->ip, sizeof(cfg->gw));
    char *lastDot = strrchr(cfg->gw, '.');
    if (lastDot != NULL)
      strcpy(lastDot, ".1");
  }
}

void ftpSaveConfig(const FtpConfig *cfg) {
  struct DeviceMapEntry *device = firstConfigDevice();
  if (device == NULL)
    return;

  char path[PATH_MAX];
  char dirPath[PATH_MAX];
  buildConfigFilePath(dirPath, device->mountpoint, NULL);
  buildConfigFilePath(path, device->mountpoint, ftpConfigFile);

  struct stat st;
  if (stat(dirPath, &st) == -1)
    mkdir(dirPath, 0777);

  FILE *file = fopen(path, "wb");
  if (file == NULL) {
    DPRINTF("FTP: failed to write %s\n", path);
    return;
  }
  fprintf(file, "mode=%s\n", (cfg->mode == FTP_NET_DHCP) ? "dhcp" : "static");
  fprintf(file, "ip=%s\n", cfg->ip);
  fprintf(file, "mask=%s\n", cfg->mask);
  fprintf(file, "gw=%s\n", cfg->gw);
  fclose(file);
}

int ftpShutdownNetwork(void) {
  backgroundFtpRunning = 0;
  if (ps2ipClientReady) {
    ps2ip_deinit();
    ps2ipClientReady = 0;
  }

  // DEV9 retains power and hardware state across an IOP reset. Ask the
  // currently loaded driver to stop SMAP and power the adapter down before
  // replacing it. Both OPL and wLaunchELF do this through dev9x:. The driver
  // can transiently report busy while its RX/TX thread drains; OPL retries
  // until it accepts DDIOC_OFF. Keep our retry bounded so a broken RPC server
  // cannot freeze the settings screen forever.
  int ret = -ENODEV;
  int attempt;
  for (attempt = 0; attempt < 100; attempt++) {
    ret = fileXioDevctl("dev9x:", DDIOC_OFF, NULL, 0, NULL, 0);
    if (ret >= 0)
      break;
    DelayThread(10000);
  }
  DPRINTF("FTP: DEV9 shutdown returned %d after %d attempt(s)\n", ret, (attempt < 100) ? (attempt + 1) : 100);
  return ret;
}

static int applyNetworkConfig(const FtpConfig *cfg, const char *ip, const char *mask, const char *gw, char *ipOut, int ipOutLen) {
  t_ip_info info;
  memset(&info, 0, sizeof(info));
  char netifName[4] = "sm0";

  int ret = ps2ip_init();
  if (ret < 0) {
    DPRINTF("FTP: ps2ip_init failed: %d\n", ret);
    setFtpDiagnostic("stage=ps2ip-init ret=%d", ret);
    return ret;
  }
  ps2ipClientReady = 1;

  // ps2ips is the EE RPC bridge to the IOP TCP/IP stack used by ps2ftpd.
  // Its EE getconfig wrapper returns 1 whenever the RPC completed, even when
  // the IOP's ps2ip_getconfig() could not find the requested interface (the
  // IOP handler discards that return value and sends back an all-zero struct).
  // Therefore the returned netif_name, not the EE return code, is the proof
  // that SMAP registered. Probe a few indices too: normally this is sm0, but
  // another lwIP netif can consume index zero in some stack configurations.
  int netifFound = 0;
  for (int attempt = 0; attempt < 20; attempt++) {
    for (int index = 0; index < 4; index++) {
      snprintf(netifName, sizeof(netifName), "sm%d", index);
      memset(&info, 0, sizeof(info));
      ret = ps2ip_getconfig(netifName, &info);
      if ((ret > 0) && !strncmp(info.netif_name, netifName, sizeof(info.netif_name))) {
        netifFound = 1;
        break;
      }
    }
    if (netifFound)
      break;
    DelayThread(100000);
  }
  if (!netifFound) {
    DPRINTF("FTP: no SMAP netif was registered (RPC ret=%d)\n", ret);
    setFtpDiagnostic("stage=smap-netif-missing probed=sm0..sm3 rpc=%d", ret);
    return -ENODEV;
  }

  const u32 initialAddress = info.ipaddr.s_addr;
  const u32 initialMask = info.netmask.s_addr;
  const u32 initialGateway = info.gw.s_addr;
  const u32 initialDhcpEnabled = info.dhcp_enabled;
  formatIPv4(initialAddress, ipOut, ipOutLen);
  DPRINTF("FTP: initial %s ip=%s raw=%08lx mask=%08lx gw=%08lx dhcp=%lu/%lu\n",
          netifName, (ipOut != NULL && ipOut[0] != '\0') ? ipOut : "?", (unsigned long)info.ipaddr.s_addr,
          (unsigned long)info.netmask.s_addr, (unsigned long)info.gw.s_addr,
          (unsigned long)info.dhcp_enabled, (unsigned long)info.dhcp_status);
  setFtpDiagnostic("stage=smap-initial netif=%s ip=%s raw=%08lx mask=%08lx gw=%08lx dhcp=%lu/%lu",
                   netifName, (ipOut != NULL && ipOut[0] != '\0') ? ipOut : "?", (unsigned long)info.ipaddr.s_addr,
                   (unsigned long)info.netmask.s_addr, (unsigned long)info.gw.s_addr,
                   (unsigned long)info.dhcp_enabled, (unsigned long)info.dhcp_status);

  u32 requestedAddress = info.ipaddr.s_addr;
  u32 requestedMask = info.netmask.s_addr;
  u32 requestedGateway = info.gw.s_addr;
  if (cfg->mode == FTP_NET_DHCP) {
    info.dhcp_enabled = 1;
  } else {
    if (!parseIPv4(ip, &requestedAddress) ||
        !parseIPv4(mask, &requestedMask) ||
        !parseIPv4(gw, &requestedGateway)) {
      DPRINTF("FTP: invalid static IPv4 configuration\n");
      setFtpDiagnostic("stage=parse-static ip=%s mask=%s gw=%s", ip, mask, gw);
      return -EINVAL;
    }
    info.ipaddr.s_addr = requestedAddress;
    info.netmask.s_addr = requestedMask;
    info.gw.s_addr = requestedGateway;
    info.dhcp_enabled = 0;

    // smap-ps2ip receives this same static configuration in its module
    // arguments. If GETCONFIG proves it was already applied, do not rewrite a
    // live netif through the old tcpips SETCONFIG RPC path. This is the normal
    // and most reliable static-IP path used by wLaunchELF as well.
    if ((initialAddress == requestedAddress) &&
        (initialMask == requestedMask) &&
        (initialGateway == requestedGateway) && !initialDhcpEnabled) {
      formatIPv4(initialAddress, ipOut, ipOutLen);
      DPRINTF("FTP: sm0 already has the requested static configuration\n");
      setFtpDiagnostic("stage=ready-static-module-args ip=%s raw=%08lx mask=%08lx gw=%08lx",
                       ipOut, (unsigned long)initialAddress, (unsigned long)initialMask, (unsigned long)initialGateway);
      return 0;
    }
  }

  /*
   * The tcpips RPC bridge does not provide a trustworthy return value for
   * SETCONFIG: its IOP handler calls ps2ip_setconfig(), but returns the
   * request buffer rather than the function's result.  Treat the call as a
   * request and decide success from a fresh GETCONFIG below.  This also
   * handles older ps2ips builds that report 0 even though the address was
   * applied successfully.
   */
  // Keep the exact discovered name in the SETCONFIG payload. An earlier
  // version broke out of the initial wait on the EE wrapper's unconditional
  // `1`, leaving this field empty and guaranteeing that SETCONFIG targeted no
  // interface at all.
  strlcpy(info.netif_name, netifName, sizeof(info.netif_name));
  ret = ps2ip_setconfig(&info);
  DPRINTF("FTP: ps2ip_setconfig returned %d; verifying %s state\n", ret, netifName);

  // Static configuration is immediate. DHCP needs time for a lease; poll the
  // IOP's real interface state so the UI reports the address that is actually
  // bound instead of displaying 0.0.0.0 or telling the user to guess.
  const int attempts = (cfg->mode == FTP_NET_DHCP) ? 60 : 8;
  for (int attempt = 0; attempt < attempts; attempt++) {
    if (attempt != 0)
      DelayThread(250000);
    ret = ps2ip_getconfig(netifName, &info);
    if (ret > 0) {
      formatIPv4(info.ipaddr.s_addr, ipOut, ipOutLen);
      const int addressReady = (cfg->mode == FTP_NET_STATIC)
                                   ? (info.ipaddr.s_addr == requestedAddress)
                                   : ((info.ipaddr.s_addr != INADDR_ANY) && (info.ipaddr.s_addr != initialAddress));
      if (addressReady) {
        DPRINTF("FTP: sm0 bound to %s\n", ipOut);
        setFtpDiagnostic("stage=ready-after-set ip=%s raw=%08lx mask=%08lx gw=%08lx dhcp=%lu/%lu",
                         ipOut, (unsigned long)info.ipaddr.s_addr, (unsigned long)info.netmask.s_addr,
                         (unsigned long)info.gw.s_addr, (unsigned long)info.dhcp_enabled,
                         (unsigned long)info.dhcp_status);
        return 0;
      }
    }
  }

  // A DHCP-capable build can still fail to obtain a lease on networks that
  // filter or delay broadcasts. The settings retain the last known static
  // address, so use it as a deterministic recovery path instead of returning
  // ETIMEDOUT with an otherwise healthy link and FTP stack.
  if (cfg->mode == FTP_NET_DHCP) {
    u32 fallbackAddress, fallbackMask, fallbackGateway;
    if (parseIPv4(cfg->ip, &fallbackAddress) &&
        parseIPv4(cfg->mask, &fallbackMask) &&
        parseIPv4(cfg->gw, &fallbackGateway)) {
      strlcpy(info.netif_name, netifName, sizeof(info.netif_name));
      info.ipaddr.s_addr = fallbackAddress;
      info.netmask.s_addr = fallbackMask;
      info.gw.s_addr = fallbackGateway;
      info.dhcp_enabled = 0;
      ret = ps2ip_setconfig(&info);
      DPRINTF("FTP: DHCP timed out; applying saved static fallback (set=%d)\n", ret);

      for (int attempt = 0; attempt < 8; attempt++) {
        if (attempt != 0)
          DelayThread(250000);
        ret = ps2ip_getconfig(netifName, &info);
        if ((ret > 0) && (info.ipaddr.s_addr == fallbackAddress)) {
          formatIPv4(info.ipaddr.s_addr, ipOut, ipOutLen);
          setFtpDiagnostic("stage=ready-dhcp-fallback ip=%s raw=%08lx mask=%08lx gw=%08lx",
                           ipOut, (unsigned long)info.ipaddr.s_addr, (unsigned long)info.netmask.s_addr,
                           (unsigned long)info.gw.s_addr);
          return 0;
        }
      }
    }
  }

  DPRINTF("FTP: timed out waiting for sm0; last ip=%s raw=%08lx dhcp=%lu/%lu\n",
          (ipOut != NULL && ipOut[0] != '\0') ? ipOut : "?", (unsigned long)info.ipaddr.s_addr,
          (unsigned long)info.dhcp_enabled, (unsigned long)info.dhcp_status);
  setFtpDiagnostic("stage=smap-timeout netif=%s mode=%s ip=%s raw=%08lx mask=%08lx gw=%08lx dhcp=%lu/%lu rpc=%d",
                   netifName, (cfg->mode == FTP_NET_DHCP) ? "dhcp" : "static",
                   (ipOut != NULL && ipOut[0] != '\0') ? ipOut : "?", (unsigned long)info.ipaddr.s_addr,
                   (unsigned long)info.netmask.s_addr, (unsigned long)info.gw.s_addr,
                   (unsigned long)info.dhcp_enabled, (unsigned long)info.dhcp_status, ret);
  return -ETIMEDOUT;
}

int ftpAttachSharedNetwork(char *ipOut, int ipOutLen) {
  FtpConfig cfg;
  int result = loadStartupNetworkConfig(&cfg);
  if (result < 0) {
    setFtpDiagnostic("stage=shared-config ret=%d", result);
    backgroundFtpError = result;
    return result;
  }

  result = applyNetworkConfig(&cfg, cfg.ip, cfg.mask, cfg.gw,
                              ipOut, ipOutLen);
  if (result < 0) {
    backgroundFtpError = result;
    return result;
  }

  if (ipOut != NULL && ipOutLen > 0)
    strlcpy(backgroundFtpIP, ipOut, sizeof(backgroundFtpIP));
  else
    strlcpy(backgroundFtpIP, cfg.ip, sizeof(backgroundFtpIP));
  backgroundFtpError = 0;
  setFtpDiagnostic("stage=shared-network-ready ip=%s", backgroundFtpIP);
  return 0;
}

void ftpSetBackgroundStatus(int result) {
  backgroundFtpRunning = (result == 0);
  backgroundFtpError = result;
  if (result == 0)
    setFtpDiagnostic("stage=background-ready ip=%s", backgroundFtpIP);
  else
    setFtpDiagnostic("stage=background-ftp ret=%d", result);
}

int ftpIsBackgroundRunning(void) {
  return backgroundFtpRunning;
}

int ftpGetBackgroundError(void) {
  return backgroundFtpError;
}

const char *ftpGetBackgroundIP(void) {
  return backgroundFtpIP;
}

int ftpStartServer(const FtpConfig *cfg, char *ipOut, int ipOutLen) {
  char ip[16], mask[16], gw[16];

  // The dashboard's current freepad server is usable until the IOP reset.
  replacementPadReady = 1;
  if ((ipOut != NULL) && (ipOutLen > 0))
    ipOut[0] = '\0';
  setFtpDiagnostic("stage=begin mode=%s requested=%s/%s gw=%s",
                   (cfg->mode == FTP_NET_DHCP) ? "dhcp" : "static", cfg->ip, cfg->mask, cfg->gw);

  if (cfg->mode == FTP_NET_DHCP) {
    // SMAP needs an initial netif address. DHCP is explicitly started later
    // through ps2ips/ps2ip_setconfig, as required by the IOP PS2IP stack.
    strlcpy(ip, "169.254.0.1", sizeof(ip));
    strlcpy(mask, "255.255.0.0", sizeof(mask));
    strlcpy(gw, "0.0.0.0", sizeof(gw));
  } else {
    strlcpy(ip, cfg->ip, sizeof(ip));
    strlcpy(mask, (cfg->mask[0] != '\0') ? cfg->mask : "255.255.255.0", sizeof(mask));
    strlcpy(gw, cfg->gw, sizeof(gw));
    if (ip[0] == '\0') {
      DPRINTF("FTP: no static IP configured\n");
      setFtpDiagnostic("stage=validate-static ret=%d reason=no-ip", -EINVAL);
      return -1; // Refuse before touching the IOP: the dashboard stays usable
    }
    if (gw[0] == '\0') {
      strlcpy(gw, ip, sizeof(gw));
      char *lastDot = strrchr(gw, '.');
      if (lastDot != NULL)
        strcpy(lastDot, ".1");
    }
  }
  DPRINTF("FTP: mode=%d ip=%s mask=%s gw=%s\n", cfg->mode, ip, mask, gw);

  // An IOP reset does not power-cycle DEV9. The UDPFS SMAP driver must release
  // the hardware while its dev9x: service is still reachable, otherwise the
  // replacement ps2ip SMAP driver inherits a half-initialized adapter.
  int ret = ftpShutdownNetwork();
  if (ret < 0) {
    setFtpDiagnostic("stage=dev9-off ret=%d", ret);
    return ret;
  }
  setFtpDiagnostic("stage=dev9-off ret=%d", ret);

  // Tear down EE-side clients before invalidating their IOP RPC servers.
  replacementPadReady = 0;
  fileXioExit();
  SifExitIopHeap();
  SifLoadFileExit();
  SifExitRpc();

  while (!SifIopReset("", 0)) {
  }
  while (!SifIopSync()) {
  }
  SifInitRpc(0);
  SifLoadFileInit();
  sbv_patch_enable_lmb();
  sbv_patch_disable_prefix_check();

  // The SMAP ethernet driver (not ps2ip) takes the interface configuration
  // as three null-separated positional arguments: ip\0mask\0gw\0.
  // (Convention verified against wLaunchELF's load_ps2ip.)
  char ifConf[48];
  uint32_t ifConfLen = 0;
  ifConfLen += snprintf(&ifConf[ifConfLen], sizeof(ifConf) - ifConfLen, "%s", ip) + 1;
  ifConfLen += snprintf(&ifConf[ifConfLen], sizeof(ifConf) - ifConfLen, "%s", mask) + 1;
  ifConfLen += snprintf(&ifConf[ifConfLen], sizeof(ifConf) - ifConfLen, "%s", gw) + 1;

  const struct {
    const char *name;
    unsigned char *irx;
    uint32_t *size;
    int wantsIfConf;
  } mods[] = {
      {"iomanX", iomanX_irx, &size_iomanX_irx, 0},
      {"fileXio", fileXio_irx, &size_fileXio_irx, 0},
      {"sio2man", sio2man_irx, &size_sio2man_irx, 0},
      {"mcman", mcman_irx, &size_mcman_irx, 0},
      {"mcserv", mcserv_irx, &size_mcserv_irx, 0},
      {"freepad", freepad_irx, &size_freepad_irx, 0},
      {"poweroff", poweroff_irx, &size_poweroff_irx, 0},
      {"ps2dev9", ps2dev9_irx, &size_ps2dev9_irx, 0},
      {"ps2ip", ps2ip_irx, &size_ps2ip_irx, 0},
      {"smap-ps2ip", smap_ps2ip_irx, &size_smap_ps2ip_irx, 1}, // gets the IP args
      {"ps2ips", ps2ips_irx, &size_ps2ips_irx, 0},
  };

  for (int i = 0; i < (int)(sizeof(mods) / sizeof(mods[0])); i++) {
    uint32_t argLen = mods[i].wantsIfConf ? ifConfLen : 0;
    const char *args = mods[i].wantsIfConf ? ifConf : NULL;
    setFtpDiagnostic("stage=load-module name=%s", mods[i].name);
    ret = execModule(mods[i].name, mods[i].irx, *mods[i].size, argLen, args);
    if (ret != 0) {
      DPRINTF("FTP: %s failed: %d\n", mods[i].name, ret);
      setFtpDiagnostic("stage=load-module name=%s ret=%d", mods[i].name, ret);
      return ret;
    }
    if (!strcmp(mods[i].name, "fileXio"))
      fileXioInit();
    else if (!strcmp(mods[i].name, "freepad"))
      replacementPadReady = 1;
    else if (!strcmp(mods[i].name, "poweroff")) {
      int powerResult = uiInitPowerReset();
      if (powerResult < 0)
        DPRINTF("FTP: failed to rebind power reset callback: %d\n", powerResult);
    }
  }

  ret = applyNetworkConfig(cfg, ip, mask, gw, ipOut, ipOutLen);
  if (ret < 0)
    return ret;

  static const char ftpArgs[] = "-anonymous";
  ret = execModule("ps2ftpd", ps2ftpd_irx, size_ps2ftpd_irx, sizeof(ftpArgs), ftpArgs);
  if (ret < 0) {
    DPRINTF("FTP: ps2ftpd failed: %d\n", ret);
    setFtpDiagnostic("stage=load-module name=ps2ftpd ret=%d", ret);
  } else {
    setFtpDiagnostic("stage=ready ip=%s", (ipOut != NULL && ipOut[0] != '\0') ? ipOut : "?");
  }
  return ret;
}
