CROSS_COMPILE ?=arm-linux-gnueabihf-
GCC ?= $(CROSS_COMPILE)gcc
G++ ?= $(CROSS_COMPILE)g++
STRIP ?= $(CROSS_COMPILE)strip

C_FLAGS := -Wall -O2 -march=armv7-a
C_FLAGS += -fPIC -DPIC -DOMXILCOMPONENTSPATH=\"/$(BUILD_DIR)\" -DCONFIG_DEBUG_LEVEL=255

OBJ_TOP ?= $(shell pwd)

IPCM_OUT := $(shell pwd)/../../../out


STLPATH := /opt/arm-linux-gnueabihf-4.8.3-201404/arm-linux-gnueabihf/lib
LDFLAGS += -L$(STLPATH) -lsupc++
LDFLAGS := -ldl -lpthread -lm

PROGS = uvc-gadget

SRCS=$(wildcard ./src/*.c)
SRCS_NO_DIR=$(notdir $(SRCS))
OBJECTS=$(patsubst %.c, %.c.o,  $(SRCS_NO_DIR))

OBJDIR ?= $(shell pwd)/obj
BINDIR ?= $(shell pwd)/bin
INSTALLDIR ?= $(shell pwd)/install
INCDIR ?= $(shell pwd)/inc

OBJPROG = $(addprefix $(OBJDIR)/, $(PROGS))

.PHONY: clean prepare PROGS

all: prepare $(OBJPROG)

prepare:

clean:
	@rm -Rf $(OBJDIR)
	@rm -Rf $(INSTALLDIR)
	@rm -Rf $(BINDIR)

$(OBJPROG):	$(addprefix $(OBJDIR)/, $(OBJECTS))
	@mkdir -p $(OBJ_TOP)/bin
	@echo "  BIN $@"
	@$(GCC) $(LDFLAGS) -o $@ $(addprefix $(OBJDIR)/, $(OBJECTS))
	@echo ""
	@cp -f ${OBJDIR}/$(PROGS) ${BINDIR}

$(OBJDIR)/%.c.o : src/%.c
	@mkdir -p obj
	@echo "  CC  $<"
	@$(GCC) $(C_FLAGS) $(C_INCLUDES) -c $< -o $@

