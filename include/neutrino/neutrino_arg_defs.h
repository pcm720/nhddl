#ifndef _NEUTRINO_NEUTRINO_ARG_DEFS_H_
#define _NEUTRINO_NEUTRINO_ARG_DEFS_H_

#include <stdint.h>
#include <string.h>

// Table-driven Neutrino argument metadata (known -gc/-gsm/path names and UI labels).

typedef struct NeutrinoGcOption {
  uint32_t bit;
  const char *digit;
} NeutrinoGcOption;

typedef struct NeutrinoGsmOption {
  uint32_t bit;
  const char *value;
} NeutrinoGsmOption;

#define NEUTRINO_GC_ARG "gc"
#define NEUTRINO_GSM_ARG "gsm"
#define NEUTRINO_LOGO_ARG "logo"
#define NEUTRINO_DBC_ARG "dbc"

#define NEUTRINO_PATH_MC0 "mc0"
#define NEUTRINO_PATH_MC1 "mc1"
#define NEUTRINO_PATH_ATA0 "ata0"
#define NEUTRINO_PATH_ATA0ID "ata0id"
#define NEUTRINO_PATH_ATA1 "ata1"

#define NEUTRINO_NUM_PATH_ARGS 5

static inline const char *neutrinoPathArgName(int idx) {
  static const char *names[NEUTRINO_NUM_PATH_ARGS] = {
      NEUTRINO_PATH_MC0,
      NEUTRINO_PATH_MC1,
      NEUTRINO_PATH_ATA0,
      NEUTRINO_PATH_ATA0ID,
      NEUTRINO_PATH_ATA1,
  };
  return (idx >= 0 && idx < NEUTRINO_NUM_PATH_ARGS) ? names[idx] : "";
}

static inline int neutrinoIsPathArg(const char *name) {
  if (!name)
    return 0;
  for (int i = 0; i < NEUTRINO_NUM_PATH_ARGS; i++) {
    if (!strcmp(name, neutrinoPathArgName(i)))
      return 1;
  }
  return 0;
}

#define NEUTRINO_GC_OPTION_COUNT 6
static const NeutrinoGcOption NEUTRINO_GC_OPTIONS[NEUTRINO_GC_OPTION_COUNT] = {
    {(1u << 0), "0"},
    {(1u << 1), "1"},
    {(1u << 2), "2"},
    {(1u << 3), "3"},
    {(1u << 5), "5"},
    {(1u << 7), "7"},
};

#define NEUTRINO_GSM_OPTION_COUNT 8
static const NeutrinoGsmOption NEUTRINO_GSM_OPTIONS[NEUTRINO_GSM_OPTION_COUNT] = {
    {(1u << 0), "fp1"},
    {(1u << 1), "fp2"},
    {(1u << 2), "1080ix1"},
    {(1u << 3), "1080ix2"},
    {(1u << 4), "1080ix3"},
    {(1u << 5), ":1"},
    {(1u << 6), ":2"},
    {(1u << 7), ":3"},
};

#endif
