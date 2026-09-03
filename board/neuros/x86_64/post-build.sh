#!/bin/sh
# Runs after the target rootfs is assembled, before image creation.
set -e

BOARD_DIR="$(dirname "$0")"

# Install the GRUB menu and stage the BIOS 1st-stage image for genimage.
install -D -m 0644 "$BOARD_DIR/grub.cfg" "$TARGET_DIR/boot/grub/grub.cfg"
cp -f "$TARGET_DIR/lib/grub/i386-pc/boot.img" "$BINARIES_DIR/boot.img"

# openssh's sample sshd_config has no Include line, so /etc/ssh/sshd_config.d/*
# is ignored. Add one (drop-in carries our dev PermitRootLogin/PasswordAuth).
SSHD="$TARGET_DIR/etc/ssh/sshd_config"
if [ -f "$SSHD" ] && ! grep -q '^Include /etc/ssh/sshd_config.d/' "$SSHD"; then
	sed -i '1i Include /etc/ssh/sshd_config.d/*.conf' "$SSHD"
fi
