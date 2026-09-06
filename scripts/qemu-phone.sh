#!/usr/bin/env bash
# Boot output/images/disk.img in QEMU as the portrait "phone" target.
# Headless: framebuffer on VNC :9, QMP on a unix socket, ssh forwarded to :2223.
#
#   scripts/qemu-phone.sh            # start (backgrounds qemu, waits for ssh)
#   scripts/qemu-phone.sh shot FILE  # QMP screendump -> FILE.png
#   scripts/qemu-phone.sh stop
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="$ROOT/output/images/disk.img"
RUN="$ROOT/output/qemu-phone"
QMP="$RUN/qmp.sock"
PIDF="$RUN/pid"
mkdir -p "$RUN"

qmp() { # send one QMP command, print the reply
	python3 - "$QMP" "$1" <<'PY'
import socket, sys, json
s = socket.socket(socket.AF_UNIX); s.connect(sys.argv[1])
f = s.makefile("rw")
f.readline(); f.write(json.dumps({"execute": "qmp_capabilities"}) + "\n"); f.flush(); f.readline()
f.write(sys.argv[2] + "\n"); f.flush()
print(f.readline().strip())
PY
}

case "${1:-start}" in
start)
	[ -f "$IMG" ] || { echo "no image at $IMG - run 'make' first" >&2; exit 1; }
	if [ -e "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then
		echo "already running (pid $(cat "$PIDF"))"; exit 0
	fi
	KVM=(); [ -w /dev/kvm ] && KVM=(-enable-kvm -cpu host)
	nohup qemu-system-x86_64 "${KVM[@]}" \
		-m 6144 -smp 4 \
		-drive file="$IMG",format=raw,if=virtio \
		-device virtio-vga -display none -vnc :9 \
		-netdev user,id=n,hostfwd=tcp::2223-:22 -device virtio-net,netdev=n \
		-smbios type=1,product=NeurOS-phone-1080x2400 \
		-serial file:"$RUN/serial.log" \
		-qmp unix:"$QMP",server,nowait \
		> "$RUN/qemu.log" 2>&1 &
	echo $! > "$PIDF"
	echo "qemu pid $(cat "$PIDF"); VNC :9 ; ssh -p 2223 root@localhost (pw: neuros)"
	echo -n "waiting for ssh"
	for i in $(seq 1 90); do
		if sshpass -p neuros ssh -p 2223 -o StrictHostKeyChecking=no \
			-o UserKnownHostsFile=/dev/null -o ConnectTimeout=2 \
			root@localhost true 2>/dev/null; then echo " up"; exit 0; fi
		echo -n .; sleep 2
	done
	echo " timeout (check $RUN/serial.log)"; exit 1
	;;
shot)
	out="${2:-$RUN/shot}"; ppm="$RUN/_shot.ppm"
	rm -f "$ppm"
	qmp "$(python3 -c 'import json,sys; print(json.dumps({"execute":"screendump","arguments":{"filename":sys.argv[1]}}))' "$ppm")" >/dev/null
	for i in $(seq 1 20); do [ -s "$ppm" ] && break; sleep 0.2; done
	if command -v convert >/dev/null; then convert "$ppm" "${out%.png}.png"
	elif command -v ffmpeg >/dev/null; then ffmpeg -y -i "$ppm" "${out%.png}.png" 2>/dev/null
	else cp "$ppm" "${out%.png}.ppm"; fi
	echo "wrote ${out%.png}.png"
	;;
stop)
	[ -e "$PIDF" ] && kill "$(cat "$PIDF")" 2>/dev/null || true
	rm -f "$PIDF"; echo stopped
	;;
*) echo "usage: $0 {start|shot [file]|stop}" >&2; exit 2 ;;
esac
