#ifndef IOP_IRX_IMPORTS_H
#define IOP_IRX_IMPORTS_H

#include <irx.h>

/* Please keep these in alphabetical order!  */
#include <bdm.h>
#include <ioman.h>
#include <loadcore.h>
#ifdef FEATURE_UDPFS_PS2IP
#include <ps2ip.h>
#else
#include <mstack.h>
#include "smap.h"
#endif
#include <stdio.h>
#include <sysclib.h>
#include <thbase.h>
#include <thevent.h>
#include <thsemap.h>

#endif /* IOP_IRX_IMPORTS_H */
