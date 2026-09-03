#!/bin/sh
# Assemble the BIOS-bootable disk.img from the ext4 rootfs + GRUB.
set -e

BOARD_DIR="$(dirname "$0")"
GENIMAGE_CFG="$BOARD_DIR/genimage.cfg"

# BR2_EXTERNAL post-image scripts run with CWD = Buildroot source tree.
support/scripts/genimage.sh -c "$GENIMAGE_CFG"

echo "NeurOS: image ready -> ${BINARIES_DIR}/disk.img"
