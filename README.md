# NHDDL — a PS2 exFAT HDD launcher for Neutrino

## What this is
This will be a memory card-based launcher that scans internal exFAT-formatted HDD for ISO files,
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
- [x] Launch selected ISO
- [x] Keep track of last launched title
- [x] Global and title-specific arguments
- [x] GUI
- [x] User input
- [X] RetroGEM Game ID transmission
- [ ] Actually apply arguments
- [ ] Make it work on a real PS2 if it somehow doesn't already
- [ ] Write a comprehensive README