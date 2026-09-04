#!/usr/bin/env bash
# Create/refresh a VirtualBox VM booting output/images/disk.img.
set -euo pipefail

# VirtualBox binaries are group vboxusers (750 / setuid). If this shell didn't
# pick up the group at login, re-exec the whole script under it.
if ! id -nG | tr ' ' '\n' | grep -qx vboxusers; then
	exec sg vboxusers -c "$(printf '%q ' "$0" "$@")"
fi

# PHONE=1 -> portrait 1080x2400, matching the Redmi Note "sweet" panel. Pick the
# "NeurOS (phone / portrait)" entry at the grub menu (it passes neuros.mode=).
PHONE="${PHONE:-0}"
if [ "$PHONE" = 1 ]; then
	VM="${VM:-NeurOS-phone}"; RES_W=1080; RES_H=2400; MEM=6144; SSHPORT=2223
else
	VM="${VM:-NeurOS-dev}"; RES_W=1280; RES_H=720; MEM=4096; SSHPORT=2222
fi
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RAW="$ROOT/output/images/disk.img"
VDI="$ROOT/output/images/${VM}.vdi"

[ -f "$RAW" ] || { echo "no image at $RAW - run 'make' first" >&2; exit 1; }

echo ">> tearing down any previous $VM"
VBoxManage controlvm "$VM" poweroff 2>/dev/null || true
sleep 1
VBoxManage unregistervm "$VM" --delete 2>/dev/null || true
VBoxManage closemedium disk "$VDI" --delete 2>/dev/null || true
rm -rf "$HOME/.VirtualBox/Machines/$VM" "$HOME/VirtualBox VMs/$VM"
rm -f "$VDI"

echo ">> converting raw -> VDI"
VBoxManage convertfromraw "$RAW" "$VDI" --format VDI

echo ">> creating VM $VM"
VBoxManage createvm --name "$VM" --ostype Linux_64 --register
VBoxManage modifyvm "$VM" \
	--memory "$MEM" --cpus 4 --firmware bios \
	--graphicscontroller vmsvga --vram 128 --accelerate3d on \
	--nic1 nat --nictype1 virtio \
	--audio-driver none --audio-controller hda --audio-out on \
	--uart1 0x3f8 4 --uartmode1 file "$ROOT/output/serial.log"
VBoxManage setextradata "$VM" "CustomVideoMode1" "${RES_W}x${RES_H}x32"
VBoxManage setextradata "$VM" "GUI/LastGuestSizeHint" "${RES_W},${RES_H}"
if [ "$PHONE" = 1 ]; then
	# neuros-session reads DMI product name and forces the portrait output mode
	VBoxManage setextradata "$VM" \
		"VBoxInternal/Devices/pcbios/0/Config/DmiSystemProduct" "NeurOS-phone-${RES_W}x${RES_H}"
fi
VBoxManage storagectl "$VM" --name SATA --add sata --controller IntelAhci --portcount 2
VBoxManage storageattach "$VM" --storagectl SATA --port 0 --device 0 --type hdd --medium "$VDI"
VBoxManage modifyvm "$VM" --natpf1 "ssh,tcp,,${SSHPORT},,22"

echo ">> done."
[ "$PHONE" = 1 ] && echo ">> PHONE mode: portrait ${RES_W}x${RES_H} (auto via DMI; no grub pick needed)"
echo ">> start (headless):  sg vboxusers -c 'VBoxManage startvm $VM --type headless'"
echo ">> start (gui):        sg vboxusers -c 'VBoxManage startvm $VM --type gui'"
echo ">> serial console:     scripts/vm-console.sh   (autologin root on ttyS0, tcp:2023)"
echo ">> ssh (after boot):   ssh -p ${SSHPORT} root@localhost   (password: neuros)"
