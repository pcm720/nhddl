#.SILENT:

GIT_VERSION := $(shell git describe --always --dirty --tags --exclude nightly)

ELF_BASE_NAME := nhddl-$(GIT_VERSION)

EE_BIN = $(ELF_BASE_NAME)_unc.elf
EE_BIN_PKD = $(ELF_BASE_NAME).elf
EE_BIN_DEBUG := $(ELF_BASE_NAME)-debug_unc.elf
EE_BIN_DEBUG_PKD := $(ELF_BASE_NAME)-debug.elf

EE_OBJS = main.o module_init.o common.o iso.o history.o options.o gui.o pad.o launcher.o iso_cache.o
IRX_FILES += sio2man.irx mcman.irx mcserv.irx fileXio.irx iomanX.irx freepad.irx
RES_FILES += icon_A.sys icon_C.sys icon_J.sys

EE_LIBS = -ldebug -lfileXio -lpatches -lgskit -ldmakit -lgskit_toolkit -lpng -lz -ltiff -lpad -lmc
EE_CFLAGS := -mno-gpopt -G0 -DGIT_VERSION="\"${GIT_VERSION}\""

EE_OBJS_DIR = obj/
EE_ASM_DIR = asm/
EE_SRC_DIR = src/

NEEDS_REBUILD := 0
ifeq ($(DEBUG), 1)
# If DEBUG=1, output targets to debug names
	EE_BIN = $(EE_BIN_DEBUG)
 	EE_BIN_PKD = $(EE_BIN_DEBUG_PKD)
# Define DEBUG and link to ELF loader with debug colors
 	EE_CFLAGS += -DDEBUG
	EE_LIBS += -lelf-loader
# Set rebuild flag
	NEEDS_REBUILD = 1
else
# Link to ELF loader without debug colors
	EE_LIBS += -lelf-loader-nocolour
	ifneq ("$(wildcard $(EE_BIN_DEBUG))","")
# Set rebuild flag if EE_BIN_DEBUG exists
		NEEDS_REBUILD = 1
	endif
endif

EE_OBJS += $(IRX_FILES:.irx=_irx.o)
EE_OBJS += $(RES_FILES:.sys=_sys.o)
EE_OBJS := $(EE_OBJS:%=$(EE_OBJS_DIR)%)

EE_INCS := -Iinclude -I$(PS2DEV)/gsKit/include -I$(PS2SDK)/ports/include

EE_LDFLAGS := -L$(PS2DEV)/gsKit/lib -L$(PS2SDK)/ports/lib -s

BIN2C = $(PS2SDK)/bin/bin2c

.PHONY: all clean .FORCE

.FORCE:

all: $(EE_BIN_PKD)

$(EE_BIN_PKD): $(EE_BIN)
	ps2-packer $< $@

clean:
	rm -rf $(EE_BIN) $(EE_BIN_PKD) $(EE_BIN_DEBUG) $(EE_BIN_DEBUG_PKD) $(EE_ASM_DIR) $(EE_OBJS_DIR)

# IRX files
%_irx.c:
	$(BIN2C) $(PS2SDK)/iop/irx/$(*:$(EE_SRC_DIR)%=%).irx $@ $(*:$(EE_SRC_DIR)%=%)_irx

# Resource files
%_sys.c:
	$(BIN2C) res/$(*:$(EE_SRC_DIR)%=%).sys $@ $(*:$(EE_SRC_DIR)%=%)_sys

$(EE_ASM_DIR):
	@mkdir -p $@

$(EE_OBJS_DIR):
	@mkdir -p $@

ifeq ($(NEEDS_REBUILD),1)
# If rebuild flag is set, add .FORCE to force full rebuild
$(EE_OBJS_DIR)%.o: $(EE_SRC_DIR)%.c .FORCE | $(EE_OBJS_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
else
$(EE_OBJS_DIR)%.o: $(EE_SRC_DIR)%.c | $(EE_OBJS_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@
endif

$(EE_OBJS_DIR)%.o: $(EE_ASM_DIR)%.c | $(EE_OBJS_DIR)
	$(EE_CC) $(EE_CFLAGS) $(EE_INCS) -c $< -o $@

include $(PS2SDK)/samples/Makefile.pref
include $(PS2SDK)/samples/Makefile.eeglobal
