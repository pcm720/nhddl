// This file is a slightly modified copy of OPL OSDHistory.h
#ifndef _HISTORY_H_
#define _HISTORY_H_

// Target history file size
#define MAX_HISTORY_ENTRIES 21
#define HISTORY_FILE_SIZE MAX_HISTORY_ENTRIES * sizeof(struct historyListEntry)

#define OSD_HISTORY_SET_DATE(year, month, date) (((uint16_t)(year)) << 9 | ((uint16_t)(month)&0xF) << 5 | ((date)&0x1F))

struct historyListEntry {
    char titleID[16];
    uint8_t launchCount;
    uint8_t bitmask;
    uint8_t shiftAmount;
    uint8_t padding;
    uint16_t timestamp;
};

// Functions
int updateHistoryFile(const char *titleID);

#endif