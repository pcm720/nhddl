#ifndef _NEUTRINO_NEUTRINO_ARG_DEFS_H_
#define _NEUTRINO_NEUTRINO_ARG_DEFS_H_

#include <stdint.h>

// Table-driven Neutrino argument metadata (known -gc/-gsm/path names and UI labels).

typedef struct NeutGcOption {
  uint32_t bit;
  const char *digit;
  const char *label;
} NeutGcOption;

typedef struct NeutGsmOption {
  uint32_t bit;
  const char *value;
  const char *label;
} NeutGsmOption;

#define NEUT_GC_ARG "gc"
#define NEUT_GSM_ARG "gsm"
#define NEUT_LOGO_ARG "logo"
#define NEUT_DBC_ARG "dbc"

#define NEUT_PATH_MC0 "mc0"
#define NEUT_PATH_MC1 "mc1"
#define NEUT_PATH_ATA0 "ata0"
#define NEUT_PATH_ATA0ID "ata0id"
#define NEUT_PATH_ATA1 "ata1"

#define NEUT_NUM_PATH_ARGS 5

static inline const char *neutPathArgName(int idx) {
  static const char *names[NEUT_NUM_PATH_ARGS] = {
      NEUT_PATH_MC0,
      NEUT_PATH_MC1,
      NEUT_PATH_ATA0,
      NEUT_PATH_ATA0ID,
      NEUT_PATH_ATA1,
  };
  return (idx >= 0 && idx < NEUT_NUM_PATH_ARGS) ? names[idx] : "";
}

#define NEUT_GC_OPTION_COUNT 6
static const NeutGcOption NEUT_GC_OPTIONS[NEUT_GC_OPTION_COUNT] = {
    {(1u << 0), "0", "IOP: Fast reads"},
    {(1u << 1), "1", "IOP: Dummy"},
    {(1u << 2), "2", "IOP: Sync reads"},
    {(1u << 3), "3", "EE: Unhook syscalls"},
    {(1u << 5), "5", "IOP: Emulate DVD-DL"},
    {(1u << 7), "7", "IOP: Fix buffer overrun"},
};

#define NEUT_GSM_OPTION_COUNT 8
static const NeutGsmOption NEUT_GSM_OPTIONS[NEUT_GSM_OPTION_COUNT] = {
    {(1u << 0), "fp1", "GSM: 240p/288p"},
    {(1u << 1), "fp2", "GSM: 480p/576p"},
    {(1u << 2), "1080ix1", "GSM: 1080i x1"},
    {(1u << 3), "1080ix2", "GSM: 1080i x2"},
    {(1u << 4), "1080ix3", "GSM: 1080i x3"},
    {(1u << 5), ":1", "GSM: Field flip :1"},
    {(1u << 6), ":2", "GSM: Field flip :2"},
    {(1u << 7), ":3", "GSM: Field flip :3"},
};

#endif
