# makefile to fail if any command in pipe is failed.
SHELL = /bin/bash -o pipefail

MAKEFLAGS += "-j $(shell nproc)"

# using gcc version 10.2.1
BASE    = arm-none-linux-gnueabihf
TOOLCHAIN_BIN = $(wildcard $(CURDIR)/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin/$(BASE)-gcc)

ifneq ($(TOOLCHAIN_BIN),)
    TOOLCHAIN_DIR = $(CURDIR)/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf/bin
    CC    = $(TOOLCHAIN_DIR)/$(BASE)-gcc
    LD    = $(TOOLCHAIN_DIR)/$(BASE)-ld
    STRIP = $(TOOLCHAIN_DIR)/$(BASE)-strip
else
    CC    = $(BASE)-gcc
    LD    = $(BASE)-ld
    STRIP = $(BASE)-strip
endif

ifeq ($(V),1)
	Q :=
else
	Q := @
endif

INCLUDE	= -I./
INCLUDE	+= -I./lib/libco
INCLUDE	+= -I./lib/miniz
INCLUDE	+= -I./lib/md5
INCLUDE += -I./lib/lzma
INCLUDE += -I./lib/zstd/lib
INCLUDE += -I./lib/libchdr/include
INCLUDE += -I./lib/bluetooth
INCLUDE += -I./lib/serial_server/library

BUILDDIR = bin

PRJ = MiSTer
C_SRC =   $(wildcard *.c) \
          $(wildcard ./lib/miniz/*.c) \
          $(wildcard ./lib/md5/*.c) \
          $(wildcard ./lib/lzma/*.c) \
					$(wildcard ./lib/zstd/lib/common/*.c) \
					$(wildcard ./lib/zstd/lib/decompress/*.c) \
          $(wildcard ./lib/libchdr/*.c) \
          lib/libco/arm.c

CPP_SRC = $(wildcard *.cpp) \
          $(wildcard ./lib/serial_server/library/*.cpp) \
          $(wildcard ./support/*/*.cpp)

IMG =     $(wildcard *.png)
TTF =     $(wildcard *.ttf)

IMLIB2_LIB  = -Llib/imlib2 -lfreetype -lbz2 -lpng16 -lz -lImlib2

OBJ	= $(C_SRC:%.c=$(BUILDDIR)/%.c.o) $(CPP_SRC:%.cpp=$(BUILDDIR)/%.cpp.o) $(IMG:%.png=$(BUILDDIR)/%.png.o) $(TTF:%.ttf=$(BUILDDIR)/%.ttf.o)
DEP	= $(C_SRC:%.c=$(BUILDDIR)/%.c.d) $(CPP_SRC:%.cpp=$(BUILDDIR)/%.cpp.d)

DFLAGS	= $(INCLUDE) -D_7ZIP_ST -DPACKAGE_VERSION=\"1.3.3\" -DHAVE_LROUND -DHAVE_STDINT_H -DHAVE_STDLIB_H -DHAVE_SYS_PARAM_H -DENABLE_64_BIT_WORDS=0 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE64_SOURCE -DVDATE=\"`date +"%y%m%d"`\"
CFLAGS	= $(DFLAGS) -Wall -Wextra -Wno-strict-aliasing -Wno-stringop-overflow -Wno-stringop-truncation -Wno-format-truncation -Wno-psabi -Wno-restrict -c
# ARM Cortex-A9 (DE10-Nano / Cyclone V SoC) tuning
CFLAGS += -mcpu=cortex-a9 -mfpu=neon-vfpv3 -mfloat-abi=hard -ftree-vectorize
LFLAGS	= -lc -lstdc++ -lm -lrt $(IMLIB2_LIB) -Llib/bluetooth -lbluetooth -lpthread

OUTPUT_FILTER = sed -e 's/\(.[a-zA-Z]\+\):\([0-9]\+\):\([0-9]\+\):/\1(\2,\ \3):/g'

ifneq ($(DEBUG),1)
	CFLAGS += -O3
else
	CFLAGS += -O0 -g -fomit-frame-pointer
endif

ifeq ($(PROFILING),1)
	DFLAGS += -DPROFILING
endif

.PHONY: all
all: $(BUILDDIR)/$(PRJ)

$(BUILDDIR)/$(PRJ): $(OBJ)
	$(Q)$(info $@)
	$(Q)$(CC) -o $@ $+ $(LFLAGS)
	$(Q)cp $@ $@.elf
ifneq ($(DEBUG),1)
	$(Q)$(STRIP) $@
endif

.PHONY: clean
clean:
	$(Q)rm -rf bin

$(BUILDDIR)/%.c.o: %.c
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu99 -o $@ -c $< 2>&1 | $(OUTPUT_FILTER)

$(BUILDDIR)/%.cpp.o: %.cpp
	$(Q)$(info $<)
	$(Q)$(CC) $(CFLAGS) -std=gnu++14 -Wno-class-memaccess -o $@ -c $< 2>&1 | $(OUTPUT_FILTER)

$(BUILDDIR)/%.png.o: %.png
	$(Q)$(info $<)
	$(Q)$(LD) -r -b binary -o $@ $< 2>&1 | $(OUTPUT_FILTER)

$(BUILDDIR)/%.ttf.o: %.ttf
	$(Q)$(info $<)
	$(Q)$(LD) -r -b binary -o $@ $< 2>&1 | $(OUTPUT_FILTER)

ifneq ($(MAKECMDGOALS), clean)
-include $(DEP)
endif
$(BUILDDIR)/%.c.d: %.c
	@mkdir -p $(dir $(BUILDDIR)/$*)
	$(Q)$(info $< >> $@)
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.c.o -MF $@ 2>&1 | $(OUTPUT_FILTER)

$(BUILDDIR)/%.cpp.d: %.cpp
	@mkdir -p $(dir $(BUILDDIR)/$*)
	$(Q)$(info $< >> $@)
	$(Q)$(CC) $(DFLAGS) -MM $< -MT $@ -MT $*.cpp.o -MF $@ 2>&1 | $(OUTPUT_FILTER)

# Ensure correct time stamp
$(BUILDDIR)/main.cpp.o: $(filter-out $(BUILDDIR)/main.cpp.o, $(OBJ))
