IOP_BIN      = resetspu.irx
IOP_OBJS     = resetspu.o imports.o
IOP_OBJS_DIR = obj/
IOP_SRC_DIR  = ./
IOP_INC_DIR  = ./

all: $(IOP_BIN)

clean:
	rm -f -r $(IOP_OBJS_DIR) $(IOP_BIN) resetspu.notiopmod.elf resetspu.notiopmod.stripped.elf

include $(PS2SDK)/Defs.make
include $(PS2SDK)/samples/Makefile.iopglobal
