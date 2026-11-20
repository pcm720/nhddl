#ifndef _UI_LANG_EN_H_
#define _UI_LANG_EN_H_

// English UI strings.
// Included from strings.c only.
// Keep common prompts first and launcher strings after the separator comment.

#define UI_LANG_EN_CODE "en"
#define UI_LANG_EN_NAME "English"

static const char *const ui_str_en[UI_STR_COUNT] = {
    /* common */
    "Back",
    "Select",
    "Toggle",
    "Yes",
    "No",
    "Off",
    "All",
    "Clear",
    "Discard",
    "Test",
    "Toggle/open",
    "Save and back",
    "(not set)",
    "(no name)",

    /* launcher */
    "Title List",
    "Title options",
    "View",
    "Menu",
    "Launch",
    "No titles",
    "No cover",
    "Updating devices...",
    "No title selected",
    "No title",
    "No configuration",
    "Launch failed: %d",
    "Test launch failed: %d",
    "Launching in %d s... (any button to cancel)",

    "Exit?",
    "Main menu",
    "Enabled devices",
    "NHDDL options",
    "Save and apply options",
    "Reload list",
    "Invalidate cache",
    "Exit",
    "Devices",
    "Language",

    "Video mode",
    "Probe delay",
    "Autolaunch",
    "Neutrino path",
    "Widescreen",
    "System default",
    "NTSC (480i)",
    "PAL (576i)",
    "480p",
    "576p",
    "720p",

    "View options",
    "Favorites only",
    "Favorites first",
    "Sort",
    "Ascending",
    "Descending",
    "Folder view",
    "Up",
    "Filter devices",
    "All devices",

    "Select device",
    "Select file",
    "Select Neutrino device",
    "Select Neutrino ELF",
    "Select file for %s",
    "Path open failed",

    "Per-title options",
    "Global Neutrino options",
    "Global Neutrino configuration",
    "Global options",
    "Global Neutrino saved",

    "Neutrino path updated",
    "Rescan busy",
    "Save busy",
    "Reload busy",
    "Invalidate busy",
    "Update busy",
    "Reloading...",
    "Failed to save config",
    "Device init failed",
    "Settings saved",
    "Config save failed (no path)",
    "Config save failed (open)",
    "Config save failed (write)",
    "Cache invalidated",

    "3 s",
    "5 s",
    "10 s",
    "15 s",
    "20 s",
    "30 s",
};

static const char *const neutrino_str_en[UI_N_STR_COUNT] = {
    "Compatibility (-gc)",
    "GSM (-gsm)",
    "Paths (device)",
    "Flags",
    "Neutrino arguments",
    "Additional arguments (raw)",

    "Compatibility (-gc)",
    "GSM (-gsm)",
    "Device emulation (paths)",
    "Additional arguments (raw)",

    "PS2 logo (-logo)",
    "Debug colors (-dbc)",
    "Favorite",
    "Fake DEV9 (ATA-net)",
    "(no additional arguments)",
    "(none)",

    "IOP: Fast reads",
    "IOP: Dummy",
    "IOP: Sync reads",
    "EE: Unhook syscalls",
    "IOP: Emulate DVD-DL",
    "IOP: Fix buffer overrun",

    "GSM: 240p/288p",
    "GSM: 480p/576p",
    "GSM: 1080i x1",
    "GSM: 1080i x2",
    "GSM: 1080i x3",
    "GSM: Field flip :1",
    "GSM: Field flip :2",
    "GSM: Field flip :3",
};

#endif
