#ifndef _MODULE_INIT_H_
#define _MODULE_INIT_H_

typedef enum {
  // Initialize only base modules
  INIT_TYPE_BASIC,
  // Initialize all modules excluding ones already loaded
  INIT_TYPE_FULL
} ModuleInitType;

// Initializes IOP modules
int initModules(ModuleInitType initType);

#endif
