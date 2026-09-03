#!/usr/bin/env bash
# Attach to the NeurOS VM serial console (VBox uart1 -> tcp:2023).
# ttyS0 has autologin root, so this drops you straight into a shell.
# Logs everything to output/serial.log. Ctrl-] or Ctrl-C to detach.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PORT:-2023}"

if command -v socat >/dev/null; then
	exec socat -,raw,echo=0,escape=0x1d "TCP:localhost:$PORT" 2>&1 | tee -a "$ROOT/output/serial.log"
elif command -v nc >/dev/null; then
	exec nc localhost "$PORT" | tee -a "$ROOT/output/serial.log"
else
	echo "need socat or nc" >&2; exit 1
fi
