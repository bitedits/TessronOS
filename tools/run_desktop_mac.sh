#!/bin/bash
# run_desktop_mac.sh — Run TessronOS Desktop in QEMU on macOS
#
# Hardware configuration:
# - Machine: virt, gic-version=2, CPU: cortex-a76 (4 SMP cores, 2GB RAM)
# - Storage: VirtIO block device (256MB GPT system disk with TSFS and /boot)
# - Removable storage: USB Mass Storage on xHCI (FAT32 partition)
# - Input: USB Keyboard and USB Tablet (absolute pointer) on xHCI
# - Audio: USB Audio on xHCI connected to host CoreAudio
# - Network: VirtIO Net connected to QEMU user-mode network (DHCP, IPv4, IPv6)
# - Display: Bochs VGA adapter via Cocoa GUI window (1280x800)
# - Console: Serial stdio

set -eu

TOP="$(cd "$(dirname "$0")/.." && pwd)"
DISK_DIR="$TOP/build_make/obj/desk"

if [ ! -f "$DISK_DIR/disk.img" ] || [ ! -f "$DISK_DIR/usb.img" ]; then
    echo "Disk images not found in $DISK_DIR. Copying fresh disks..."
    mkdir -p "$DISK_DIR"
    cp "$TOP/build_make/obj/_QEMU_VIRT__ktestdeskdisk/test_disk.img" "$DISK_DIR/disk.img"
    cp "$TOP/build_make/obj/_QEMU_VIRT__ktestdeskdisk/usb_disk.img" "$DISK_DIR/usb.img"
fi

DISPLAY_OPT="${DISPLAY_OPT:-cocoa}"
AUDIO_OPT="${AUDIO_OPT:-coreaudio}"

exec qemu-system-aarch64 \
    -M virt,gic-version=2 -cpu cortex-a76 -smp 4 -m 2G \
    -display "$DISPLAY_OPT" \
    -name "TessronOS Desktop" \
    -global virtio-mmio.force-legacy=false \
    -drive file="$DISK_DIR/disk.img",if=none,format=raw,id=hd0 \
    -device virtio-blk-device,drive=hd0 \
    -netdev user,id=n0 -device virtio-net-device,netdev=n0 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-tablet,bus=xhci.0 \
    -drive file="$DISK_DIR/usb.img",if=none,format=raw,id=usb0 \
    -device usb-storage,bus=xhci.0,drive=usb0,removable=on \
    -audiodev "$AUDIO_OPT",id=snd0 \
    -device usb-audio,bus=xhci.0,audiodev=snd0 \
    -device bochs-display \
    -serial stdio \
    -kernel "$TOP/build_make/tessronosdesk.elf" "$@"
