# NHDDL — a PS2 exFAT BDM launcher for Neutrino

NHDDL is a memory card-based launcher that scans _FAT/exFAT-formatted_ BDM devices for ISO files,
lists them and boots selected ISO via Neutrino.  

It displays visual Game ID to trigger per-game settings on the Pixel FX line of products and writes to memory card history file before launching the title, triggering per-title memory cards on SD2PSX and MemCard PRO 2.

Note that this not an attempt at making a Neutrino-based Open PS2 Loader replacement.  
It __will not__ boot ISOs from anything other than BDM devices.  
GSM, PADEMU, IGR, IGS and other stuff is out-of-scope of this launcher.

## Usage

- Get the [latest Neutrino release](https://github.com/rickgaiser/neutrino/releases)
- Get the [latest `nhddl.elf`](https://github.com/pcm720/nhddl/releases)
- Unpack Neutrino release
- Copy `nhddl.elf` to Neutrino folder next to `neutrino.elf`
- _Additional step if you need only ATA, USB, MX4SIO, UDPBD or iLink_:  
  Modify `nhddl.yaml` [accordingly](#common-use-cases) and copy it next to `nhddl.elf`
- Copy Neutrino folder to your PS2 memory card.  
  Any folder (e.g. `APPS`) will do, it doesn't have to be in the root of your memory card.

Updating `nhddl.elf` is as simple as replacing `nhddl.elf` with the latest version.

### Supported BDM devices

NHDDL reuses Neutrino modules for BDM support and requires them to be present in Neutrino `modules` directory.
These files should already be present in Neutrino release ZIP.

By default, NHDDL initializes all modules and looks for ISOs on FAT/exFAT-formatted BDM devices.  
See [this](#launcher-configuration-file) section for details on `nhddl.yml`.

**Do not plug in any USB mass storage devices while running NHDDL!**  
Doing so might crash NHDDL and/or possibly corrupt the files on your target device due to how BDM drivers work.

#### ATA
Make sure that Neutrino `modules` directory contains the following IRX files:
- `bdm.irx` 
- `bdmfs_fatfs.irx`
- `dev9_ns.irx`
- `ata_bd.irx`

To skip all other BDM devices, `mode: ata` must be present in `nhddl.yaml`.

#### MX4SIO
The following files are required for MX4SIO:
- `bdm.irx` 
- `bdmfs_fatfs.irx`
- `mx4sio_bd_mini.irx`

To skip all other BDM devices, `mode: mx4sio` must be present in `nhddl.yaml` to initialize only MX4SIO.

#### USB
The following files are required for USB:
- `bdm.irx` 
- `bdmfs_fatfs.irx`
- `usbd_mini.irx`
- `usbmass_bd_mini.irx`

Using more than one USB mass storage device at the same time is not recommended.
To skip all other BDM devices, `mode: usb` must be present in `nhddl.yaml`.

#### UDPBD
The following files are required for UDPBD:
- `bdm.irx` 
- `bdmfs_fatfs.irx`
- `dev9_ns.irx`
- `smap_udpbd.irx`

To skip all other BDM devices, `mode: udpbd` must be present in `nhddl.yaml`.

UDPBD module requires PS2 IP address to work.  
NHDDL attempts to retrieve PS2 IP address from the following sources:
- `udpbd_ip` flag in `nhddl.yml`
- `SYS-CONF/IPCONFIG.DAT` on the memory card (usually created by w/uLaunchELF)

`udpbd_ip` flag takes priority over `IPCONFIG.DAT`.

#### iLink
The following files are required for iLink:
- `bdm.irx` 
- `bdmfs_fatfs.irx`
- `iLinkman.irx`
- `IEEE1394_bd_mini.irx`

To skip all other BDM devices, `mode: ilink` must be present in `nhddl.yaml`.

### Storing ISO

ISOs can be stored almost anywhere on the storage device.  
Only directories that start with `.`, `$` and the following directories are ignored:
 - `nhddl`
 - `APPS`
 - `ART`
 - `CFG`
 - `CHT`
 - `LNG`
 - `THM`
 - `VMC`
 - `XEBPLUS`

### Displaying cover art

NHDDL uses the same file naming convention and file format used by OPL.  
Just put **140x200 PNG** files named `<title ID>_COV.png` (e.g. `SLUS_200.02_COV.png`) into the `ART` directory on the root of your HDD.  
If unsure where to get your cover art from, check out the latest version of [OPL Manager](https://oplmanager.com).

## Configuration files

NHDDL uses YAML-like files to load and store its configuration options.

### Launcher configuration file

Launcher configuration is read from the `nhddl.yaml` file, which must be located in the same directory as `nhddl.elf`.  
This file is _completely optional_ and must be used only to enable 480p in NHDDL UI or switch NHDDL to single device.  
By default, 480p is disabled and the all devices are used to look for ISO files.

To disable a flag, you can just comment it out with `#`.

See [this file](examples/nhddl.yaml) for an example of a valid `nhddl.yaml` file.

### Configuration files on storage device

NHDDL stores and looks for ISO-related config files in `nhddl` directory in the root of your BDM drive.  

#### `lastTitle.bin`

This file stores the full path of the last launched title and is used to automatically navigate to it each time NHDDL starts up.  
This file is created automatically.

#### `cache.bin`

Contains title ID cache for all ISOs located during the previous launch, making building ISO list way faster.  
This file is also created automatically.

#### Argument files

These files store arbitrary arguments that are passed to Neutrino on title launch.  
Arguments stored in those files __are passed to `neutrino.elf` as-is__.

_For a list of valid arguments, see Neutrino README._

Example of a valid argument file:
```yaml
# All flags are passed to neutrino as-is for future-proofing, comments are ignored
gc: 2
mc0: massX:/memcard0.bin # all file paths must always start with massX:. X will be replaced with the actual device number.
$mc1: massX:/memcard1.bin # this argument is disabled
# Arguments that don't have a value
# Empty values are treated as a simple flag
dbc:
logo:
```

To be able to parse those arguments and allow you to dynamically enable or disable them in UI,  
NHDDL uses a dollar sign (`$`) to mark arguments as enabled or disabled by default.  
Only enabled arguments get passed to Neutrino.

NHDDL supports two kinds of argument files:

#### global.yaml

Arguments stored in `nhddl/global.yaml` are applied to every ISO by default.

#### ISO-specific files

Arguments stored in `nhddl/<ISO name>.yaml` are applied to every ISO that starts with `<ISO name>`.  

NHDDL can create this file automatically when title compatibility modes are modified and saved in UI.

#### Example of directory sturcture on BDM device

```
ART/ # cover art, optional
  |
  - SLUS_200.02_COV.png
nhddl/
  |
   - lastTitle.txt # created automatically
   - cache.bin # created automatically
   - global.yaml # optional argument file, applies to all ISOs
   - Silent Hill 2.yaml # optional argument file, applies only to ISOs that start with "Silent Hill 2"
CD/
  |
   — Ridge Racer V.iso
DVD/
  |
   - Silent Hill 2.iso
   - TimeSplitters.iso
```

## Common use cases

### Switching NHDDL to ATA-only mode

To switch NHDDL to ATA-only mode, you'll need to create `nhddl.yaml` with the following contents:
```yaml
mode: ata
```
Copy this file to Neutrino directory next to `nhddl.elf`.

### Switching NHDDL to USB-only mode

To switch NHDDL to USB-only mode, you'll need to create `nhddl.yaml` with the following contents:
```yaml
mode: usb
```
Copy this file to Neutrino directory next to `nhddl.elf`.

### Switching NHDDL to MX4SIO-only mode

To switch NHDDL to MX4SIO-only mode, you'll need to create `nhddl.yaml` with the following contents:
```yaml
mode: mx4sio
```
Copy this file to Neutrino directory next to `nhddl.elf`.

### Switching NHDDL to UDPBD-only mode

To switch NHDDL to UDPBD-only mode, you'll need to create `nhddl.yaml` with the following contents:
```yaml
mode: udpbd
udpbd_ip: <PS2 IP address>
```

If you've previously set up the network via uLaunchELF and your memory card
has `SYS-CONF/IPCONFIG.DAT` file, you don't have to add `udpbd_ip`.

Copy this file to the Neutrino directory next to `nhddl.elf`.

### Switching NHDDL to iLink-only mode

To switch NHDDL to iLink-only mode, you'll need to create `nhddl.yaml` with the following contents:
```yaml
mode: ilink
```
Copy this file to Neutrino directory next to `nhddl.elf`.

## UI screenshots

<details>
    <summary>Title list</summary>
    <img src="img/titles.png">
</details>
<details>
    <summary>Title options</summary>
    <img src="img/options.png">
</details>