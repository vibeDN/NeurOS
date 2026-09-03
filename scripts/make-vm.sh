#!/usr/bin/env bash
# Create/refresh a VirtualBox VM booting output/images/disk.img.
set -euo pipefail

# VirtualBox binaries are group vboxusers (750 / setuid). If this shell didn't
# pick up the group at login, re-exec the whole script under it.
if ! id -nG | tr ' ' '\n' | grep -qx vboxusers; then
	exec sg vboxusers -c "$(printf '%q ' "$0" "$@")"
fi

VM="${VM:-NeurOS-dev}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RAW="$ROOT/output/images/disk.img"
VDI="$ROOT/output/images/neuros-dev.vdi"

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
	--memory 4096 --cpus 4 --firmware bios \
	--graphicscontroller vmsvga --vram 128 --accelerate3d on \
	--nic1 nat --nictype1 virtio \
	--audio-driver none \
	--uart1 0x3f8 4 --uartmode1 file "$ROOT/output/serial.log"
VBoxManage storagectl "$VM" --name SATA --add sata --controller IntelAhci --portcount 2
VBoxManage storageattach "$VM" --storagectl SATA --port 0 --device 0 --type hdd --medium "$VDI"
VBoxManage modifyvm "$VM" --natpf1 "ssh,tcp,,2222,,22"

echo ">> done."
echo ">> start (headless):  sg vboxusers -c 'VBoxManage startvm $VM --type headless'"
echo ">> start (gui):        sg vboxusers -c 'VBoxManage startvm $VM --type gui'"
echo ">> serial console:     scripts/vm-console.sh   (autologin root on ttyS0, tcp:2023)"
echo ">> ssh (after boot):   ssh -p 2222 root@localhost   (password: neuros)"
