# NHDDL — a PS2 launcher for Neutrino

<p align="center">
  <img src="img/logo/logo.png">
</p>

NHDDL is a Neutrino launcher that scans MMCE, APA or _FAT/exFAT-formatted_ BDM devices for ISO files,
lists them and boots selected ISO via Neutrino.  

It displays visual Game ID to trigger per-title settings on the Pixel FX line of products and triggers per-title memory cards on SD2PSX and MemCard PRO2.

Note that this not an attempt at making a Neutrino-based Open PS2 Loader replacement.  
Since NHDDL only launches Neutrino, PADEMU, IGR, IGS, cheats and other features supported by OPL are _out-of-scope_ unless they are implemented in Neutrino.

## Usage

### Controls

Global button conventions:
 - **Cross**/**Circle** — select/confirm/activate
 - **Triangle** — back/cancel

Title list:
 - **Up**/**Down** — previous/next title (long names scroll horizontally)
 - **L1**/**R1** — previous/next page (or start/end of the list)
 - **Cross**/**Circle** — launch the selected title
 - **Triangle** — title options (per-title or global Neutrino args)
 - **Select** — view options for title list view (device filter, favorites only/first, sort)
 - **Start** — NHDDL menu (enabled devices, NHDDL options, reload, exit)

Folder view:
 - **Square** — title options
 - **Triangle** — up a level
 - **Start** — NHDDL menu
 - **Cross**/**Circle** — enter folder/device or launch title

Title options:
- **Triangle** — save and back
- **Start** — discard
- **Square**  — test
- **Cross**/**Circle** — toggle/open.

Path settings: 
- **Triangle**  — back
- **Square**  — clear
- **Cross**/**Circle** — open a file picker

**Save and apply options** writes `nhddl.cnf` and applies language, video mode/widescreen, and folder view immediately.

### Important notes

NHDDL requires a full [Neutrino](https://github.com/rickgaiser/neutrino) installation to be present at one of the following paths:
- `<NHDDL launch directory>/neutrino.elf` (__might be case-sensitive__ depending on device)
- `<>:/neutrino/neutrino.elf` (NHDDL device and storage devices, __might be case-sensitive__ depending on device)
- `hdd0:/<OPL partition>/neutrino/neutrino.elf` (APA device, if HDL mode is enabled)  
  `OPL partition` is read from `hdd0:__common/OPL/conf_hdd.cfg`, with `+OPL` or `__common/OPL` used as a fallback

By default, NHDDL tries to initialize all supported devices. You can override this behavior and reduce initialization times by setting specific mode in launcher configuration file.  
See [this](#launcher-configuration-file) section for details on `nhddl.cnf`.

Note that if your ELF loader resets IOP (e.g. `PS2BBL` and recent versions of `wLE_ISR`), NHDDL will try loading `nhddl.cnf` from memory cards and MMCE devices first to avoid
loading modules for all devices.
If `nhddl.cnf` is not present on any of the memory cards or MMCE devices, NHDDL will initialize all modules first and then will attempt to search for `nhddl.cnf` again.

**Do not plug in any BDM storage devices while running NHDDL!**  
Doing so might crash NHDDL and/or possibly corrupt the files on your target device due to how BDM drivers work.

#### Manual installation

To use NHDDL:
- Get the [latest `nhddl.elf`](https://github.com/pcm720/nhddl/releases)
- Copy `nhddl.elf` to your memory card or storage device wherever you want.
-   _Additional step if you need only some of the available modes or MX4SIO support_:  
  1. Modify `nhddl.cnf` [accordingly](#launcher-configuration-file) and copy it next to `nhddl.elf`.  
  See notes on [the configuration file](#launcher-configuration-file) for more information
  2. When loading NHDDL from APA or BDM, there is no reliable way to get modes from `nhddl.cnf`.  
  You can [rename the ELF](#forcing-a-specific-mode-via-the-nhddl-elf-file-name) to force a specific mode
- Get the [latest Neutrino release](https://github.com/rickgaiser/neutrino/releases)
- Copy Neutrino folder to the root of your PS2 memory card or your storage device. 

#### Save Application System PSU

You can also get NHDDL as an easy-to-use PSU package [here](https://pcm720.github.io/nhddl-psu/).  
To install it:
- Copy generated `nhddl.psu` to your USB drive
- Open wLaunchELF on your PS2
- Choose your USB device and copy `nhddl.psu`
- Go back and open your memory card (`mc0` or `mc1`)
- Open file menu and select `psuPaste`
- Get the [latest Neutrino release](https://github.com/rickgaiser/neutrino/releases)
- Copy Neutrino folder to the root of your PS2 memory card or your storage device. 

This will install NHDDL to your memory card along with the PS2 Browser icon.
 
Updating `nhddl.elf` is as simple as replacing `nhddl.elf` with the latest version.

### Supported modes

#### ATA (MBR/GPT-formatted HDD with exFAT partition)

To skip all other devices, `-device=ata` must be present in `nhddl.cnf`.

#### MX4SIO

MX4SIO support requires explicit configuration due to conflicts with memory cards and MMCE devices.  
`-device=mx4sio` must be present in `nhddl.cnf` on __the memory card__ for MX4SIO to work.  

Note that __MMCE devices will not be available__ when this mode is enabled.

#### USB

Using more than one USB mass storage device at the same time is not recommended.
To skip all other devices, `-device=usb` must be present in `nhddl.cnf`.

#### UDPFS

To skip all other devices, `-device=udpfs` must be present in `nhddl.cnf`.

UDPFS module requires PS2 IP address to work.  
NHDDL attempts to retrieve PS2 IP address from the following sources:
- `-ip=` in `nhddl.cnf` (or via argv)
- `SYS-CONF/IPCONFIG.DAT` on the memory card (usually created by w/uLaunchELF via `MISC/Configure/Network Settings...`)

`-ip=` takes priority over `IPCONFIG.DAT`.

Make sure to set the IP address in Neutrino config files (as of Neutrino 1.8.0, `config/bsd-udpfs.toml`).  
Consult Neutrino documentation for more details.

Recommended UDPFS server implementations:
- [udbfs_server](https://github.com/rickgaiser/neutrino/tree/master/pc) by Maximus32
  - Reference Python implementation

#### iLink

To skip all other devices, `-device=ilink` must be present in `nhddl.cnf`.

#### MMCE (SD2PSX, MemCard PRO2)

To skip all other devices, `-device=mmce` must be present in `nhddl.cnf`.

#### HD Loader (APA-formatted HDD with HDL partitions)

Note that HDL backend **does not support** VMCs and virtual HDDs.  
Cover art, `nhddl.cnf` title options will be loaded from the OPL partition set in
`hdd0:__common/OPL/conf_hdd.cfg`, with `+OPL` or `__common/OPL` used as a fallback.

To skip all other devices, `-device=hdl` must be present in `nhddl.cnf`.
However, due to how device modules are initialized, this will not improve the initialization times.

### Storing ISO (MMCE, BDM backends)

ISOs can be stored almost anywhere on the storage device, but no more than 5 directories deep.  
For example, ISOs stored in `DVD/A/B/C/D` will be scanned and added to the list, but ISOs stored in `DVD/A/B/C/D/E` will be ignored.  

Furthermore, directories that start with `.`, `$` and the following directories are ignored to speed up the scanning process:
 - `nhddl`
 - `APPS`
 - `ART`
 - `CFG`
 - `CHT`
 - `LNG`
 - `THM`
 - `VMC`
 - `XEBPLUS`
 - `MemoryCards`

### Displaying cover art

NHDDL uses the same file naming convention and file format used by OPL.  
Just put **140x200 PNG** files named `<title ID>_COV.png` (e.g. `SLUS_200.02_COV.png`) into the `ART` directory on the root of your device.  
If unsure where to get your cover art from, check out the latest version of [OPL Manager](https://oplmanager.com).

### Passing arguments

Similar to Neutrino, NHDDL supports receiving launcher options from `argv` in the `-<arg>=<value>` format.  
Be aware that passing any argument will cause NHDDL to completely skip loading launcher configuration files from any device.  

For example, to initialize NHDDL with UDPFS mode, you can run `nhddl.elf` with `-device=udpfs` and `-ip=192.168.1.6`.  

If NHDDL receives `-dvd=<path to the image file>`, it will skip UI initialization and directly launch Neutrino while respecting all arguments specified in argument files.  
Add `-noinit` argument to skip IOP initialization (make sure all required modules are already loaded).  

See [this file](examples/nhddl.cnf) for a list of all supported arguments and their possible values.

### Forcing a specific mode via the NHDDL ELF file name

When loading NHDDL from APA or BDM, there is no reliable way to get modes from `nhddl.cnf` other than passing arguments. However, not a lot of launchers support this.  
To work around this, you can add a postfix to `nhddl.elf` to force a specific mode:
- `nhddl-ata.elf` — force ATA mode
- `nhddl-mmce.elf` — force MMCE mode
- `nhddl-hdl.elf` — force HDL mode
- `nhddl-udpbd.elf` — force UDPBD mode
- `nhddl-usb.elf` — force USB mode
- `nhddl-ilink.elf` — force iLink mode
- `nhddl-m4s.elf` — force MX4SIO mode

NHDDL will only initialize the mode specified in the file name and respect all other options from `nhddl.cnf` on the storage device.
When forcing UDPBD mode, make sure you've configured `SYS-CONF/IPCONFIG.DAT` on your memory card.

## Configuration files

NHDDL uses CNF-style config files: one argument per line in the form `-name=value` or `-name`. Lines starting with `#` are comments; `#-name=value` or `# -name=value` (whitespace between `#` and `-` allowed) marks a disabled entry. Only lines starting with `-` or `#` are valid; all others are ignored.

### Launcher configuration file

Launcher configuration is read from the `nhddl.cnf` file.

Configuration file is loaded from one of the following paths:
- `<NHDDL launch directory>/nhddl.cnf` (__might be case-sensitive__ depending on device)
- `massX:/nhddl/nhddl.cnf` (BDM devices, if any of BDM modes are enabled)
- `hdd0:/<OPL partition>/nhddl/nhddl.cnf` (APA device, if HDL mode is enabled)  
  `OPL partition` is read from `hdd0:__common/OPL/conf_hdd.cfg`, with `+OPL` or `__common/OPL` used as a fallback
- `mmceX:/nhddl/nhddl.cnf` (MMCE devices, will work even if MMCE mode is _not_ enabled unless MX4SIO mode is set)
- `mcX:/APP_NHDDL/nhddl.cnf` (memory cards, __case-sensitive__)

This file is _completely optional_. Use it to force UI video mode/widescreen, folder view (`-folder_view`), set devices, probe delay, autolaunch timeout, UI language (`-lang=`), UDPFS IP (`-ip=`), or Neutrino ELF path.  
By default, system video mode is used, UI language is English (`en`), title list view is enabled, and no devices are used to look for ISO files.

`-lang=` takes a short language code baked into the ELF (e.g. `-lang=en`), unknown codes are ignored. The same option is available under **NHDDL options → Language**.

Optional UI font override (not part of `nhddl.cnf`): place `theme/font.ttf` under the NHDDL root to replace the embedded font. There is no theme config file yet.

To disable a flag, comment out the line with `#` (e.g. `#-device=ata`).

See [this file](examples/nhddl.cnf) for an example of a valid `nhddl.cnf` file and a list of all supported arguments and possible values.

#### Adding a UI language

UI strings are compiled into the ELF. Each language is represented by one C header file.

To add a new language:

1. Fork the repository
2. Copy `include/ui/lang/en.h` to `include/ui/lang/<code>.h` (e.g. `ja.h`).
3. Rename the macros/tables (`UI_LANG_*_CODE`/`NAME`, `ui_str_*`, `neutrino_str_*`) and translate the string literals.
   Keep array order identical to the `UiStrId`/`NeutrinoStrId` enums in `include/ui/strings.h`.
4. `#include "ui/lang/<code>.h"` from `src/ui/strings.c` and append an entry to `s_langs`.
5. Create a PR with the changes

When adding new strings, make sure to register the string IDs in `include/ui/strings.h` and add the values to each language header file.

### Additional configuration files on storage device

NHDDL stores and looks for ISO-related config files in `nhddl` directory in the root of your BDM drive.  

#### `lastTitle.bin`

This file stores the full path of the last launched title and is used to automatically navigate to it each time NHDDL starts up.  
This file is created automatically.

#### `cache.bin`

Contains title ID cache for all ISOs located during the previous launch, making building ISO list way faster.  
This file is also created automatically.

#### Argument files

These files store arbitrary arguments that are passed to Neutrino on title launch, in the same CNF format as the launcher config: one argument per line as `-name=value` or `-name`; disabled entries as `#-name=value` or `#-name`.  
Most arguments are passed to `neutrino.elf` as written.

For **known path arguments** (`-mc0`, `-mc1`, `-ata0`, `-ata0id`, `-ata1`), CNF should store device-relative values (e.g. `/memcard0.bin` or `CD/foo.iso`). NHDDL prepends the title device mountpoint when showing those paths in the UI and when launching Neutrino. Other arguments with `/`-prefixed values are not rewritten.

Empty known path arguments (enabled `-mc0` with no value) are kept in CNF for per-title overrides but are not passed on launch.

Ensure that paths are as short as possible as the combined length of all arguments cannot exceed approximately 255 characters. If this limit is exceeded, arguments will be truncated when passed to Neutrino, potentially causing unpredictable results such as returning to the PS2 menu or ignoring some arguments.  
Note that NHDDL always adds `-bsd=<>`, `-dvd=<full path to ISO>`, and `-qb` arguments to load the title, which reduces the actual allowable length below 255 characters.

_For a list of valid arguments, see Neutrino README._

Example of a valid argument file:
```
-gc=2
-mc0=/memcard0.bin # known path args: device-relative; mountpoint added at launch/UI
# -mc1=/VMC/memcard1.bin # this argument is disabled
-dbc
-logo
```

Only enabled (non-commented) arguments get passed to Neutrino (except empty known path args, see above).

NHDDL supports two kinds of argument files:

#### global.cnf

Arguments stored in `nhddl/global.cnf` are applied to every ISO by default.

#### ISO-specific files

Arguments stored in `nhddl/<ISO name>.cnf` are applied to every ISO that starts with `<ISO name>`.  

NHDDL can create this file automatically when options are saved from the UI.  
Known path arguments are stored relative to device mountpoint (e.g. `/VMC/vmc1.bin`, not `usb:/VMC/vmc1.bin`).

#### Example of directory structure on BDM device

```
ART/ # cover art, optional
  |
  - SLUS_200.02_COV.png
nhddl/
  |
   - lastTitle.bin # created automatically
   - cache.bin # created automatically
   - global.cnf # optional argument file, applies to all ISOs
   - nhddl.cnf # NHDDL options, applied after initialization is complete
   - Silent Hill 2.cnf # optional argument file, applies only to ISOs that start with "Silent Hill 2"
CD/
  |
   — Ridge Racer V.iso
DVD/
  |
   - Silent Hill 2.iso
   - TimeSplitters.iso
```

## UI screenshots

<details>
    <summary>Title list</summary>
    <img src="img/titles.png">
</details>
<details>
    <summary>Title options</summary>
    <img src="img/options1.png">
    <img src="img/options2.png">
</details>
