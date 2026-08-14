UDPFS_BD    ?= 0
UDPFS_IOMAN ?= 0
UDPFS_IOMAN_PS2IP ?= 0
UDPFS_FHI   ?= 0

IOP_CFLAGS += -mno-check-zero-division -I../smap/include -I../ministack/include

IOP_OBJS_COMMON = main.o imports.o exports.o

ifeq ($(UDPFS_BD),1)
  IOP_CFLAGS += -DUDPRDMA_MAX_SOCKETS=1 -DFEATURE_UDPFS_BD
  IOP_OBJS = $(IOP_OBJS_COMMON) udprdma.o udpfs_core.o udpfs_bd.o
  IOP_OBJS_DIR = obj.udpfs_bd/
  IOP_BIN = udpfs_bd.irx
else ifeq ($(UDPFS_IOMAN_PS2IP),1)
  # Dashboard-only variant.  UDPFS uses the resident PS2IP/lwIP stack so it
  # can share one SMAP interface with ps2ftpd.  Neutrino's in-game FHI build
  # keeps using the zero-copy ministack transport above/below this target.
  IOP_CFLAGS += -DUDPRDMA_MAX_SOCKETS=1 -DFEATURE_UDPFS_IOMAN -DFEATURE_UDPFS_PS2IP
  IOP_OBJS = $(IOP_OBJS_COMMON) udprdma_ps2ip.o udpfs_core.o udpfs_ioman.o
  IOP_OBJS_DIR = obj.udpfs_ioman_ps2ip/
  IOP_BIN = udpfs_ioman_ps2ip.irx
else ifeq ($(UDPFS_IOMAN),1)
  IOP_CFLAGS += -DUDPRDMA_MAX_SOCKETS=1 -DFEATURE_UDPFS_IOMAN
  IOP_OBJS = $(IOP_OBJS_COMMON) udprdma.o udpfs_core.o udpfs_ioman.o
  IOP_OBJS_DIR = obj.udpfs_ioman/
  IOP_BIN = udpfs_ioman.irx
else ifeq ($(UDPFS_FHI),1)
  IOP_CFLAGS += -DUDPRDMA_MAX_SOCKETS=1 -DFEATURE_UDPFS_FHI
  IOP_OBJS = $(IOP_OBJS_COMMON) udprdma.o udpfs_core.o udpfs_fhi.o
  IOP_OBJS_DIR = obj.udpfs_fhi/
  IOP_BIN = udpfs_fhi.irx
endif

include ../Rules.make
