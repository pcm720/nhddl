#ifndef _HDD_H_
#define _HDD_H_

// Macros for loading embedded IOP modules
#define IRX_DEFINE(mod)                                                                                                                              \
  extern unsigned char mod##_irx[] __attribute__((aligned(16)));                                                                                     \
  extern unsigned int size_##mod##_irx

#define IRX_LOAD(mod)                                                                                                                                \
  logString("\tloading " #mod "\n");                                                                                                                 \
  if (SifExecModuleBuffer(mod##_irx, size_##mod##_irx, 0, NULL, &iopret) < 0)                                                                        \
    return ret;                                                                                                                                      \
  if (iopret) {                                                                                                                                      \
    return iopret;                                                                                                                                   \
  }

// Initializes Memory Card modules required to load HDD modules and neutrino ELF
int init();
// Inititializes HDD modules located at basePath
int initHDD(char *basePath);

#endif