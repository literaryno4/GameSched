# SPDX-License-Identifier: GPL-2.0
# GameSched - Gaming-Optimized sched_ext Scheduler
#
# Build with: make LINUX_SRC=/path/to/linux-source

# Default Linux source path (can be overridden)
LINUX_SRC ?= $(HOME)/code/linux

# Validate LINUX_SRC exists
ifeq ($(wildcard $(LINUX_SRC)/tools/sched_ext),)
$(error LINUX_SRC must point to a Linux kernel source tree with sched_ext. Set LINUX_SRC=/path/to/linux)
endif

# Directories
CURDIR := $(abspath .)
SRCDIR := $(CURDIR)/src
BUILDDIR := $(CURDIR)/build
OBJDIR := $(BUILDDIR)/obj

# Linux kernel paths
LINUX_TOOLS := $(LINUX_SRC)/tools
LINUX_SCHED_EXT := $(LINUX_TOOLS)/sched_ext
LINUX_LIBDIR := $(LINUX_TOOLS)/lib
LINUX_BPFDIR := $(LINUX_LIBDIR)/bpf
LINUX_INCDIR := $(LINUX_TOOLS)/include
LINUX_APIDIR := $(LINUX_INCDIR)/uapi
LINUX_GENDIR := $(LINUX_SRC)/include/generated
LINUX_SCX_INC := $(LINUX_SCHED_EXT)/include

# Use kernel's pre-built tools if available, otherwise build
LINUX_BUILD := $(LINUX_SCHED_EXT)/build
BPFTOOL := $(LINUX_BUILD)/sbin/bpftool
BPFOBJ := $(LINUX_BUILD)/obj/libbpf/libbpf.a

# Check if kernel tools are built
ifeq ($(wildcard $(BPFTOOL)),)
$(error Please build sched_ext tools first: cd $(LINUX_SCHED_EXT) && make)
endif

# Compiler settings
CC := gcc
CLANG := clang

# Flags
CFLAGS := -g -O2 -Wall -Werror -pthread \
	-I$(LINUX_BUILD)/include \
	-I$(LINUX_GENDIR) \
	-I$(LINUX_LIBDIR) \
	-I$(LINUX_INCDIR) \
	-I$(LINUX_APIDIR) \
	-I$(LINUX_SCX_INC) \
	-I$(SRCDIR)

ifneq ($(wildcard $(LINUX_GENDIR)/autoconf.h),)
CFLAGS += -DHAVE_GENHDR
endif

LDFLAGS := -lelf -lz -lpthread

# BPF compiler flags
BPF_CFLAGS := -g -O2 -target bpf \
	-D__TARGET_ARCH_x86 \
	-I$(LINUX_SCX_INC) \
	-I$(LINUX_SCX_INC)/bpf-compat \
	-I$(LINUX_BUILD)/include \
	-I$(LINUX_APIDIR) \
	-I$(LINUX_SRC)/include \
	-I$(SRCDIR) \
	-Wall -Wno-compare-distinct-pointer-types

# Get system includes for BPF
define get_sys_includes
$(shell $(CLANG) -v -E - </dev/null 2>&1 \
	| sed -n '/<...> search starts here:/,/End of search list./{ s| \(/.*\)|-idirafter \1|p }')
endef

BPF_CFLAGS += $(call get_sys_includes)

# Targets
TARGET := $(BUILDDIR)/scx_gamesched

.PHONY: all clean

all: $(TARGET)

$(BUILDDIR) $(OBJDIR):
	mkdir -p $@

# Compile BPF object
$(OBJDIR)/scx_gamesched.bpf.o: $(SRCDIR)/scx_gamesched.bpf.c $(SRCDIR)/scx_gamesched.h | $(OBJDIR)
	$(CLANG) $(BPF_CFLAGS) -c $< -o $@

# Generate skeleton
$(BUILDDIR)/scx_gamesched.bpf.skel.h: $(OBJDIR)/scx_gamesched.bpf.o | $(BUILDDIR)
	$(BPFTOOL) gen object $(OBJDIR)/scx_gamesched.bpf.linked1.o $<
	$(BPFTOOL) gen object $(OBJDIR)/scx_gamesched.bpf.linked2.o $(OBJDIR)/scx_gamesched.bpf.linked1.o
	$(BPFTOOL) gen object $(OBJDIR)/scx_gamesched.bpf.linked3.o $(OBJDIR)/scx_gamesched.bpf.linked2.o
	$(BPFTOOL) gen skeleton $(OBJDIR)/scx_gamesched.bpf.linked3.o name scx_gamesched > $@

# Compile userspace
$(OBJDIR)/scx_gamesched.o: $(SRCDIR)/scx_gamesched.c $(BUILDDIR)/scx_gamesched.bpf.skel.h $(SRCDIR)/scx_gamesched.h | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(BUILDDIR) -c $< -o $@

# Link final binary
$(TARGET): $(OBJDIR)/scx_gamesched.o | $(BUILDDIR)
	$(CC) -o $@ $< $(BPFOBJ) $(LDFLAGS)

clean:
	rm -rf $(BUILDDIR)

# Help
help:
	@echo "GameSched - Gaming-Optimized sched_ext Scheduler"
	@echo ""
	@echo "Usage: make [target] LINUX_SRC=/path/to/linux"
	@echo ""
	@echo "Targets:"
	@echo "  all     - Build scx_gamesched (default)"
	@echo "  clean   - Remove build artifacts"
	@echo "  help    - Show this help"
	@echo ""
	@echo "Environment:"
	@echo "  LINUX_SRC - Path to Linux kernel source (required)"
	@echo "              Current: $(LINUX_SRC)"
