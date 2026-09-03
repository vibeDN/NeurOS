#!/bin/sh
# Runs after the target rootfs is assembled, before image creation.
set -e

BOARD_DIR="$(dirname "$0")"

# Install the GRUB menu and stage the BIOS 1st-stage image for genimage.
install -D -m 0644 "$BOARD_DIR/grub.cfg" "$TARGET_DIR/boot/grub/grub.cfg"
cp -f "$TARGET_DIR/lib/grub/i386-pc/boot.img" "$BINARIES_DIR/boot.img"
