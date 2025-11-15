IOP_BIN := smap_udpbd.irx

IOP_SRC_DIR := src/
IOP_OBJS_DIR := obj/
IOP_CFLAGS += -mno-check-zero-division
IOP_INCS := -Isrc/include
IOP_OBJS =  main.o smap.o xfer.o ministack_eth.o ministack_arp.o ministack_ip.o ministack_udp.o udpbd.o udptty.o imports.o exports.o

all:: $(IOP_BIN)

clean::
	rm -f -r $(IOP_BIN) $(IOP_OBJS_DIR)

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.iopglobal
