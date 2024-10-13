#.SILENT:

EE_BIN = nhddl_unc.elf
EE_BIN_PKD = nhddl.elf

EE_OBJS = main.o module_init.o common.o iso.o history.o options.o gui.o pad.o launcher.o iso_cache.o
IRX_FILES += sio2man.irx mcman.irx mcserv.irx fileXio.irx iomanX.irx freepad.irx
RES_FILES += icon_A.sys icon_C.sys icon_J.sys

EE_OBJS_DIR = obj/
EE_ASM_DIR = asm/
EE_SRC_DIR = src/

EE_OBJS += $(IRX_FILES:.irx=_irx.o)
EE_OBJS += $(RES_FILES:.sys=_sys.o)
EE_OBJS := $(EE_OBJS:%=$(EE_OBJS_DIR)%)

EE_INCS := -Iinclude -I$(PS2DEV)/gsKit/include -I$(PS2SDK)/ports/include

EE_LDFLAGS := -L$(PS2DEV)/gsKit/lib -L$(PS2SDK)/ports/lib -s
EE_LIBS = -ldebug -lfileXio -lpatches -lelf-loader -lgsKit -ldmaKit -lgskit_toolkit -lpng -lz -ltiff -lpad
EE_CFLAGS := -mno-gpopt -G0

BIN2C = $(PS2SDK)/bin/bin2c

.PHONY: all clean

all: $(EE_BIN_PKD)

$(EE_BIN_PKD): $(EE_BIN)
	ps2-packer $< $@

clean:
	rm -rf $(EE_BIN) $(EE_BIN_PKD) $(EE_ASM_DIR) $(EE_OBJS_DIR)

# IRX files
%_irx.c:
	$(BIN2C) $(PS2SDK)/iop/irx/$(*:$(EE_SRC_DIR)%=%).irx $@ $(*:$(EE_SRC_DIR)%=%)_irx

%_sys.c:
	$(BIN2C) res/$(*:$(EE_SRC_DIR)%=%).sys $@ $(*:$(EE_SRC_DIR)%=%)_sys

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
