#
# toolchain.mk — ツールチェーンの探索
#
#   優先順位: TS_TOOLCHAIN(bin ディレクトリ。x86_64 は TS_X64_TOOLCHAIN) > PATH > ~/.local/xPacks
#   (tools/setup_wsl.sh、tools/x64/build_toolchain.sh の既定)
#

XPACK_PREFIX ?= $(HOME)/.local/xPacks

# ARCH(makefile が TARGET から決める)ごとのコンパイラの名前の頭と QEMU
TC_PREFIX_aarch64  := aarch64-none-elf-
QEMU_SYS_aarch64   := qemu-system-aarch64
QEMU_XPACK_aarch64 := qemu-arm
# x86_64: tools/x64/build_toolchain.sh が ~/.local/xPacks に作る x86_64-elf の GCC。QEMU は
# Windows 版(x64.mk の QEMU_WIN_X64)を使う
TC_PREFIX_x86_64   := x86_64-elf-
QEMU_SYS_x86_64    := qemu-system-x86_64
QEMU_XPACK_x86_64  := qemu-x86_64
TC_PREFIX := $(TC_PREFIX_$(ARCH))
QEMU_SYS  := $(QEMU_SYS_$(ARCH))

# x86_64 の bin ディレクトリは TS_X64_TOOLCHAIN(tools/x64/env.sh)。TS_TOOLCHAIN は aarch64 のもの
ifeq ($(ARCH),x86_64)
  TC_BIN_DIR := $(TS_X64_TOOLCHAIN)
else
  TC_BIN_DIR := $(TS_TOOLCHAIN)
endif

ifneq ($(TC_BIN_DIR),)
  CROSS := $(TC_BIN_DIR)/$(TC_PREFIX)
else ifneq ($(shell command -v $(TC_PREFIX)gcc 2>/dev/null),)
  CROSS := $(TC_PREFIX)
else ifneq ($(wildcard $(XPACK_PREFIX)/$(TC_PREFIX)gcc/bin/$(TC_PREFIX)gcc),)
  CROSS := $(XPACK_PREFIX)/$(TC_PREFIX)gcc/bin/$(TC_PREFIX)
else
  $(error $(TC_PREFIX)gcc not found: run tools/setup_wsl.sh and "source tools/env.sh" (x86_64: tools/x64/build_toolchain.sh and "source tools/x64/env.sh"))
endif

CC      := $(CROSS)gcc
AS      := $(CROSS)gcc
LD      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size
GDB     := $(CROSS)gdb

ifneq ($(shell command -v $(QEMU_SYS) 2>/dev/null),)
  QEMU ?= $(QEMU_SYS)
else
  QEMU ?= $(XPACK_PREFIX)/$(QEMU_XPACK_$(ARCH))/bin/$(QEMU_SYS)
endif
