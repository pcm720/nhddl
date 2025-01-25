#.SILENT:
STANDALONE ?= 0

GIT_VERSION := $(shell git describe --always --dirty --tags --exclude nightly)

ELF_BASE_NAME := nhddl-$(GIT_VERSION)

EE_BIN = $(ELF_BASE_NAME)_unc.elf
EE_BIN_STANDALONE = $(ELF_BASE_NAME)-standalone_unc.elf
EE_BIN_PKD = $(ELF_BASE_NAME).elf
EE_BIN_PKD_STANDALONE = $(ELF_BASE_NAME)-standalone.elf

EE_OBJS = main.o module_init.o common.o options.o gui.o gui_graphics.o pad.o launcher.o iso_cache.o iso_title_id.o devices.o devices_iso.o devices_hdl.o target.o
IRX_FILES += sio2man.irx mcman.irx mcserv.irx fileXio.irx iomanX.irx freepad.irx mmceman.irx
ELF_FILES += loader.elf

ifeq ($(STANDALONE), 1)
 GIT_VERSION := "$(GIT_VERSION)-standalone"
 IRX_FILES += ps2dev9.irx bdm.irx bdmfs_fatfs.irx ata_bd.irx usbd_mini.irx smap_udpbd.irx ps2hdd_bdm.irx
 IRX_FILES += usbmass_bd_mini.irx mx4sio_bd_mini.irx iLinkman.irx IEEE1394_bd_mini.irx udptty.irx
 EE_CFLAGS += -DSTANDALONE
 EE_BIN = $(EE_BIN_STANDALONE)
 EE_BIN_PKD = $(EE_BIN_PKD_STANDALONE)
endif

EE_LIBS = -ldebug -lfileXio -lpatches -lgskit -ldmakit -lgskit_toolkit -lpng -lz -ltiff -lpad -lmc
EE_CFLAGS += -mno-gpopt -G0 -DGIT_VERSION="\"${GIT_VERSION}\""

EE_OBJS_DIR = obj/
EE_ASM_DIR = asm/
EE_SRC_DIR = src/

EE_OBJS += $(IRX_FILES:.irx=_irx.o)
EE_OBJS += $(ELF_FILES:.elf=_elf.o)
EE_OBJS := $(EE_OBJS:%=$(EE_OBJS_DIR)%)

EE_INCS := -Iinclude -I$(PS2DEV)/gsKit/include -I$(PS2SDK)/ports/include

EE_LDFLAGS := -L$(PS2DEV)/gsKit/lib -L$(PS2SDK)/ports/lib -s

BIN2C = $(PS2SDK)/bin/bin2c

.PHONY: all clean

all: $(EE_BIN_PKD)

$(EE_BIN_PKD): $(EE_BIN)
	ps2-packer $< $@

cleanobj:
	$(MAKE) -C loader clean
	$(MAKE) -C iop/smap_udpbd clean
	rm -rf $(EE_ASM_DIR) $(EE_OBJS_DIR)

clean:
	$(MAKE) -C loader clean
	$(MAKE) -C iop/smap_udpbd clean
	rm -rf $(EE_BIN) $(EE_BIN_PKD) $(EE_BIN_STANDALONE) $(EE_BIN_PKD_STANDALONE) $(EE_ASM_DIR) $(EE_OBJS_DIR)

# ELF loader
loader/loader.elf: loader
	$(MAKE) -C $<

%loader_elf.c: loader/loader.elf
	$(BIN2C) $(*:$(EE_SRC_DIR)%=loader/%)loader.elf $@ $(*:$(EE_SRC_DIR)%=%)loader_elf

# smap_udpbd.irx
iop/smap_udpbd/smap_udpbd.irx: iop/smap_udpbd
	$(MAKE) -C $<

%smap_udpbd_irx.c: iop/smap_udpbd/smap_udpbd.irx
	$(BIN2C) iop/smap_udpbd/$(*:$(EE_SRC_DIR)%=%)smap_udpbd.irx $@ $(*:$(EE_SRC_DIR)%=%)smap_udpbd_irx

# ps2hdd-bdm.irx
%ps2hdd_bdm_irx.c:
	$(BIN2C) $(PS2SDK)/iop/irx/$(*:$(EE_SRC_DIR)%=%)ps2hdd-bdm.irx $@ $(*:$(EE_SRC_DIR)%=%)ps2hdd_bdm_irx

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
