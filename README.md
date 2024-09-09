# Neutrino HDD Launcher Prototype

## What this is
This will be a memory card-based Neutrino launcher that scans internal exFAT-formatted HDD for ISO files,
lists them and boots selected ISO via Neutrino.

It writes to memory card history file before launching the title, triggering per-title memory cards on SD2PSX and MemCard PRO 2.

## What this is not

This not an attempt at making an Open PS2 Loader replacement.  
It __will not__ boot ISOs from anything other than exFAT-formatted internal HDDs.

## TODO
- [x] Load HDD and memory card modules
- [x] Scan for ISO and build list of targets
- [x] Open ISO and get title ID
- [x] Write to MC history file
- [x] Launch selected ISO (currently hardcoded to whatever is contained in the lastTitle file or the first entry in the list) 
- [x] Keep track of last launched title
- [x] Global and title-specific arguments
- [ ] GUI
- [ ] User input
- [ ] RetroGEM Game ID transmission
- [ ] Make it work on a real PS2