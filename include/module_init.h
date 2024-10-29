#ifndef _HDD_H_
#define _HDD_H_

// Initializes Memory Card modules required to load HDD modules and neutrino ELF
int init();

// Inititializes BDM modules located at basePath
int initBDM(char *basePath);

#endif
