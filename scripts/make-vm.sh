#!/usr/bin/env bash
# Create/refresh a VirtualBox VM booting output/images/disk.img.
set -euo pipefail

VM="${VM:-NeurOS-dev}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
RAW="$ROOT/output/images/disk.img"
VDI="$ROOT/output/images/neuros-dev.vdi"

[ -f "$RAW" ] || { echo "no image at $RAW - run 'make' first" >&2; exit 1; }

echo ">> converting raw -> VDI"
rm -f "$VDI"
VBoxManage convertfromraw "$RAW" "$VDI" --format VDI

if VBoxManage showvminfo "$VM" >/dev/null 2>&1; then
	echo ">> removing existing VM $VM"
	VBoxManage unregistervm "$VM" --delete
fi

echo ">> creating VM $VM"
VBoxManage createvm --name "$VM" --ostype Linux_64 --register
VBoxManage modifyvm "$VM" \
	--memory 4096 --cpus 4 --firmware bios \
	--graphicscontroller vmsvga --vram 64 \
	--nic1 nat --nictype1 virtio \
	--audio-driver none \
	--uart1 0x3f8 4 --uartmode1 file "$ROOT/output/serial.log"
VBoxManage storagectl "$VM" --name SATA --add sata --controller IntelAhci --portcount 2
VBoxManage storageattach "$VM" --stor-controller SATA --port 0 --device 0 --type hdd --medium "$VDI"
VBoxManage modifyvm "$VM" --natpf1 "ssh,tcp,,2222,,22"

echo ">> done. boot with:  VBoxManage startvm $VM --type gui"
echo ">> serial log:        $ROOT/output/serial.log"
echo ">> ssh (after boot):  ssh -p 2222 root@localhost   (password: neuros)"
