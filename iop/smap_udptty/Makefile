IOP_BIN := smap_udptty.irx

IOP_OBJS_DIR := obj/
IOP_SRC_DIR := ../smap_udpbd/src/
IOP_CFLAGS += -mno-check-zero-division -DNO_BDM
IOP_INCS := -I../smap_udpbd/src/include
IOP_OBJS = main.o smap.o xfer.o ministack.o udptty.o imports.o exports.o

all:: $(IOP_BIN)

clean::
	rm -f -r $(IOP_BIN) $(IOP_OBJS_DIR)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
