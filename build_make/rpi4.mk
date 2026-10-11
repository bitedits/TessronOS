#
# rpi4.mk — Raspberry Pi 4、Raspberry Pi 400、Compute Module 4(BCM2711)向けの設定
#
#   命令セットの基準は ARMv8.0-A に CRC32 を足したもの(common.mk)。NetBSD のスタックと
#   mozc もその基準で作った別の置き場のもの(~/tessronos-netbsd-v80、~/tessronos-mozc-v80)を
#   結ぶ。ブラウザ(BROWSER=1)は組み込まない(docs/tessronos/03-platform.md 3.6)
#

LNKFILE := $(TOP)/etc/linker/rpi4/tkernel_map.ld
LOAD_PA := 0x200000

CFLAGS_TARGET  :=
IMAGE_TARGETS  := $(EXE_FILE).img

ifeq ($(BROWSER),1)
$(error TARGET=_RPI4_ does not take BROWSER=1 (the browser is built for ARMv8.2-A only))
endif

# make DISP_SWAP_RB=1: 画面の赤と青が入れ替わって見えるとき、ファームウェアの
# 画素の並びを逆に読む(設計書 16.5.1)
ifeq ($(DISP_SWAP_RB),1)
CFLAGS_TARGET += -DCNF_DISP_SWAP_RB=1
endif

# 電池の無い時計の代わりに、起動時に DHCP でアドレスを取り、SNTP で時計を合わせる
# (設計書 12.6)。make NET_AUTO=0 で止める
ifneq ($(NET_AUTO),0)
CFLAGS_TARGET += -DCNF_NET_DHCP=1 -DCNF_NET_SNTP=1
endif

# make OBJTEST=0: 起動の後のデバイスの実身の確認(objtest.c、objtest: の行)を行わない
ifeq ($(OBJTEST),0)
CFLAGS_TARGET += -DCNF_OBJTEST=0
endif

# make PCIE_POLL=1: PCIe の先の xHCI を割込み(INTA)ではなく一定の間隔で見る
ifeq ($(PCIE_POLL),1)
CFLAGS_TARGET += -DCNF_PCIE_POLL=1
endif

# ボード依存ソース(コア依存は sources.mk の SRCS_CORE)。メールボックス、時計、乱数は
# Raspberry Pi 5 と共通(kernel/sysdepend/rpi)
SRCS += kernel/sysdepend/rpi4/hw_setting.c \
	kernel/sysdepend/rpi4/devinit.c \
	kernel/sysdepend/rpi4/pcie_rc.c \
	kernel/sysdepend/rpi4/objtest.c \
	kernel/sysdepend/rpi/mbox.c \
	kernel/sysdepend/rpi/rtc.c \
	kernel/sysdepend/rpi/rng_hw.c \
	kernel/sysdepend/rpi/wdog.c \
	device/gpio/gpio_bcm2711.c \
	device/ser/ser.c \
	device/net/eth_genet.c \
	device/net/netdev.c \
	device/sd/sdhci.c \
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
	device/snd/snd.c \
	device/cam/cam.c \
	device/disp/disp_rpi5.c

# SD カードの FAT 区画(make sd SD=/mnt/e)
SD ?=

# QEMU の raspi4b(設計書 03-platform.md 3.6.4)。xPack の 9.2 にもある。
# make TARGET=_RPI4_ qemu-rpi4 で起動し、make TARGET=_RPI4_ KTEST=1 test-rpi4 で試験を走らせる。
# カーネルはファームウェアと同じくヘッダの無いフラットな像を 0x80000 から始める
# (boot.S がリンク番地 0x200000 へ写す)。デバイスツリーはファームウェアの配布物のもの
RPI4_QEMU_DTB ?= obj/boot/bcm2711-rpi-4-b.dtb
RPI4_QEMU_FLAGS := -M raspi4b -smp 4 -nographic -no-reboot
TEST_TMO ?= 900
