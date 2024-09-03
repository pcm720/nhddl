# Neutrino HDD Launcher Prototype

## What this is
This will be a light Neutrino launcher that scans internal exFAT-formatted HDD for ISO files,
lists them and boots selected ISO via Neutrino.

It writes to memory card history file before launching the game, triggering per-game memory cards on SD2PSX and MemCard PRO 2.

## What this is not

This not an attempt at making an Open PS2 Loader replacement.  
It __will not__ boot ISOs from anything other than exFAT-formatted internal HDDs and it will not display cover art.

## TODO
- [x] Load HDD and memory card modules
- [x] Scan for ISO and build list of targets
- [x] Open ISO and get title ID
- [x] Write to MC history file
- [x] Launch selected ISO (currently hardcoded to launch the first ISO in the list) 
- [ ] GUI
- [ ] User input
- [ ] Compatibility flags
- [ ] RetroGEM Game ID transmission
- [ ] Make it work on a real PS2