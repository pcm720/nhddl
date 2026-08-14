# Integrated FTP release-candidate notes

This branch keeps the UDPFS title list and anonymous FTP server active at the
same time by running both over one PS2IP/SMAP stack. FTP is started during the
normal UDPFS dashboard startup; opening the FTP settings page is not required.

## Hardware-validated behavior

- the title list remains usable while an FTP client is connected;
- memory-card files can be downloaded and uploaded through `/mc/0/`;
- launching the recovery file browser from Settings works without the former
  `LoadExecPS2` black-screen hang;
- video-mode changes persist in the configuration file that was actually
  loaded; and
- the compact title-list layout and controller footer render correctly in the
  standard and 720p UI modes used during testing.

The validated packed dashboard artifact is 435,380 bytes with SHA-256
`9D43B3D10617CC1E6A94FEE14FAA96E161F5D04F02E9E1E3A4770ACFB1B5A5A8`.

## Reproducible FTP dependency

`iop/ps2ftpd.irx` is intentionally tracked even though generated IRX files are
normally ignored. It is the exact module used by the hardware-tested build.
The corresponding AFL-2.0 source and license are retained in
`iop/ps2ftpd-src/`; the module hash is
`1A4C9AF87801EDCE2EBF8B7C4084AABB95AE8A4573344146B185737E8E6B5A69`.

## Known release-candidate limits

- Startup currently requires a usable Ethernet link, a configured static IPv4
  address, and a reachable UDPFS server. Reconnecting Ethernet after a failed
  initial discovery does not rebuild the list in place.
- Editable DHCP/static settings and live reconnect are being preserved on a
  separate feature branch until they receive the same hardware validation.
- In-game reset is supplied by the matching Neutrino build and remains subject
  to per-title compatibility testing; it is not implemented by this resident
  dashboard process.
- The embedded FTP daemon does not reliably rename memory-card files. Automated
  deployment tools must upload and read back a staged file, copy it to the live
  name, verify the live readback, and retain a rollback copy.
