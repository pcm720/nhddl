# If enabled, will print additional debug text to stdout
ENABLE_PRINTF ?= 0

#.SILENT:
GIT_VERSION := $(shell git describe --always --dirty --tags --exclude nightly)

ELF_BASE_NAME := nhddl-$(GIT_VERSION)

EE_BIN = $(ELF_BASE_NAME)_unc.elf
EE_BIN_PKD = $(ELF_BASE_NAME).elf

EE_OBJS = main.o module_init.o common.o options.o launcher.o title_id.o target.o
EE_OBJS += gui.o gui_graphics.o gui_args.o pad.o
EE_OBJS += devices.o devices_mmce.o devices_bdm.o devices_iso.o devices_hdl.o
# Basic modules
IRX_FILES += sio2man.irx mcman.irx mcserv.irx fileXio.irx iomanX.irx freepad.irx mmceman.irx
# BDM modules
IRX_FILES += ps2dev9.irx bdm.irx bdmfs_fatfs.irx ata_bd.irx usbd_mini.irx smap_udpbd.irx
IRX_FILES += usbmass_bd_mini.irx mx4sio_bd_mini.irx iLinkman.irx IEEE1394_bd_mini.irx
# HDL modules
IRX_FILES += ps2hdd.irx ps2fs.irx

EE_LIBS = -ldebug -lfileXio -lpatches -lgskit_toolkit -lgskit -ldmakit -lpng -lz -ltiff -lpad
EE_CFLAGS += -mno-gpopt -G0 -DGIT_VERSION="\"${GIT_VERSION}\""
EE_LINKFILE ?= linkfile

EE_OBJS_DIR = obj/
EE_ASM_DIR = asm/
EE_SRC_DIR = src/

EE_OBJS += $(IRX_FILES:.irx=_irx.o)
EE_OBJS := $(EE_OBJS:%=$(EE_OBJS_DIR)%)

ifeq ($(ENABLE_PRINTF), 1)
 EE_CFLAGS += -DENABLE_PRINTF
endif

EE_INCS := -Iinclude -I$(PS2DEV)/gsKit/include -I$(PS2SDK)/ports/include

EE_LDFLAGS := -L$(PS2DEV)/gsKit/lib -L$(PS2SDK)/ports/lib -s

BIN2C = $(PS2SDK)/bin/bin2c

.PHONY: all clean

all: $(EE_BIN_PKD)

$(EE_BIN_PKD): $(EE_BIN)
	ps2-packer $< $@

clean:
	$(MAKE) -C iop/smap_udpbd clean
	$(MAKE) -C iop/mmceman clean
	rm -rf $(EE_BIN) $(EE_BIN_PKD) $(EE_ASM_DIR) $(EE_OBJS_DIR)

# smap_udpbd.irx
iop/smap_udpbd/smap_udpbd.irx: iop/smap_udpbd
	$(MAKE) -C $<

%smap_udpbd_irx.c: iop/smap_udpbd/smap_udpbd.irx
	$(BIN2C) iop/smap_udpbd/$(*:$(EE_SRC_DIR)%=%)smap_udpbd.irx $@ $(*:$(EE_SRC_DIR)%=%)smap_udpbd_irx

# mmceman.irx
%mmceman_irx.c:
	$(MAKE) -C iop/mmceman/mmceman
	$(BIN2C) iop/mmceman/mmceman/irx/mmceman.irx $@ $(*:$(EE_SRC_DIR)%=%)mmceman_irx

# IRX files
%_irx.c:
	$(BIN2C) $(PS2SDK)/iop/irx/$(*:$(EE_SRC_DIR)%=%).irx $@ $(*:$(EE_SRC_DIR)%=%)_irx

$(EE_ASM_DIR):
	@mkdir -p $@

$(EE_OBJS_DIR):
	@mkdir -p $@

$(EE_OBJS_DIR)%.o: $(EE_SRC_DIR)%.c | $(EE_OBJS_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

$(EE_OBJS_DIR)%.o: $(EE_ASM_DIR)%.c | $(EE_OBJS_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
