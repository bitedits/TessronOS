#
# x64.mk — x64 の PC(UEFI。QEMU q35 + OVMF と Microsoft Surface Go 2 を1つのイメージで)向けの設定
#            (docs/tessronos/03-platform.md 3.7、docs/private/x64_surface_go2_study.md)
#
#   make TARGET=_X64_ BUILD_ID=<名前>          tessronos<名前>.elf と $(OBJDIR)/esp/(UEFI のローダとカーネル)
#   make TARGET=_X64_ BUILD_ID=<名前> qemu-x64 ビルドして Windows の QEMU(q35 + OVMF)で起動する
#   make TARGET=_X64_ BUILD_ID=<名前> test     起動してシリアルの出力を検査する(tools/qemu_run.sh)
#

# この .mk の規則が既定の目標にならないように
.DEFAULT_GOAL := all

LNKFILE := $(TOP)/etc/linker/x64/tkernel_map.ld
# カーネルの物理アドレスはローダが決めるので、決まった値は無い。共通部が読む LOAD_PA_VALUE は
# イメージの物理アドレスの変数(reset_main.c の knl_kimg_pa)になる
LOAD_PA := knl_kimg_pa

CFLAGS_TARGET  :=
IMAGE_TARGETS  := $(OBJDIR)/esp/EFI/BOOT/BOOTX64.EFI $(OBJDIR)/esp/tessronos.elf

# AArch64 の命令で書いた共通部のファイルは、x86-64 では建てない
SRCS := $(filter-out device/gpu/v3d/v3d_mmu.c outer_kernel/wm/compose_simd.c peripheral_kernel/libc/setjmp.S,$(SRCS)) \
	peripheral_kernel/libc/x86_64/setjmp.S

# ボード依存ソース(コア依存は sources.mk の SRCS_CORE)
SRCS += kernel/sysdepend/x64/hw_setting.c \
	kernel/sysdepend/x64/devinit.c \
	kernel/sysdepend/x64/rtc.c \
	kernel/sysdepend/x64/rng_hw.c \
	kernel/sysdepend/x64/fbcon.c \
	kernel/sysdepend/acpi/acpi.c \
	outer_kernel/wm/x86_64/compose_simd.c \
	device/disp/disp_gop.c \
	device/virtio/vio_pci.c \
	device/vnet/vnet.c \
	device/net/netdev.c \
	device/usb/usbdma.c \
	device/usb/xhci.c \
	device/usb/usbcore.c \
	device/usb/usbman.c \
	device/usb/hub.c \
	device/usb/hid.c \
	device/usb/msc.c \
	device/usb/uac.c \
	device/usb/uvc.c \
	device/usb/uusb.c \
	device/usb/vhc.c \
	device/snd/snd.c \
	device/cam/cam.c

# UEFI のローダ(tools/x64/uefi)。mingw-w64 の GCC で PE32+ の EFI アプリにする
EFI_CROSS ?= $(if $(TS_X64_EFI_TOOLCHAIN),$(TS_X64_EFI_TOOLCHAIN)/,$(XPACK_PREFIX)/x86_64-w64-mingw32-gcc/bin/)x86_64-w64-mingw32-
EFI_CC    := $(EFI_CROSS)gcc
EFI_SRCS  := $(TOP)/tools/x64/uefi/loader.c
EFI_HDRS  := $(TOP)/tools/x64/uefi/efi.h $(TOP)/kernel/sysdepend/cpu/core/x86_64/bootinfo.h
EFI_CFLAGS := -O2 -g0 -ffreestanding -fno-builtin -fshort-wchar -mno-red-zone -fno-stack-protector \
	-fno-asynchronous-unwind-tables -Wall -Wextra -Wno-unused-parameter \
	-nostdlib -Wl,--subsystem,10 -Wl,-e,efi_main -Wl,--image-base,0x10000000

$(OBJDIR)/BOOTX64.EFI: $(EFI_SRCS) $(EFI_HDRS)
	@mkdir -p $(dir $@)
	@echo 'EFI $@'
	@$(EFI_CC) $(EFI_CFLAGS) -o $@ $(EFI_SRCS)

# ESP の中身: \EFI\BOOT\BOOTX64.EFI、\tessronos.elf、\cmdline.txt(起動の設定)。
# QEMU はこのディレクトリをそのまま FAT のディスクとして見せる(fat:rw:)
X64_CMDLINE ?= video=1280x800
$(OBJDIR)/esp/EFI/BOOT/BOOTX64.EFI: $(OBJDIR)/BOOTX64.EFI
	@mkdir -p $(dir $@)
	@cp $< $@

$(OBJDIR)/esp/tessronos.elf: $(EXE_FILE).elf
	@mkdir -p $(dir $@)
	@cp $< $@
	@printf '%s\n' "$(X64_CMDLINE)" > $(OBJDIR)/esp/cmdline.txt

# 試験機: Windows の QEMU の q35 と、それに付いてくる OVMF(edk2-x86_64-code.fd)。
# WSL の apt の qemu-system-x86 と ovmf は sudo が要るので使わない
QEMU_WIN_X64 ?= /mnt/c/Program Files/qemu/qemu-system-x86_64.exe
QEMU         := $(QEMU_WIN_X64)
OVMF_CODE    ?= C:/Program Files/qemu/share/edk2-x86_64-code.fd
TEST_TMO     ?= 300
QEMU_MACHINE ?= q35
QEMU_CPU     ?= max
QEMU_SMP     ?= 4
QEMU_MEM     ?= 2G
QEMU_EXTRA   ?=
QEMU_FLAGS   := -M $(QEMU_MACHINE) -cpu $(QEMU_CPU) -smp $(QEMU_SMP) -m $(QEMU_MEM) \
	-drive "if=pflash,format=raw,readonly=on,file=$(OVMF_CODE)" \
	-no-reboot -net none $(QEMU_EXTRA)
X64_ESP_WIN   = $(shell wslpath -m $(abspath $(OBJDIR)/esp) 2>/dev/null)
X64_ESP_DRIVE = -drive format=raw,file=fat:rw:$(X64_ESP_WIN)

.PHONY: qemu-x64
qemu-x64: $(EXE_FILE).elf $(IMAGE_TARGETS)
	"$(QEMU)" $(QEMU_FLAGS) $(X64_ESP_DRIVE) -serial stdio

# カーネルの libgcc は -mno-red-zone の組(tools/x64/build_toolchain.sh のマルチライブ)
LDFLAGS_COMMON += $(ARCH_KFLAGS)
