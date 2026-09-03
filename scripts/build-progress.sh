#!/usr/bin/env bash
# Rough build progress: packages installed / total packages in this config.
# Buildroot has no real total-work number - step count doesn't track wall time
# (the kernel is ~8 steps and many minutes) - so this is a ballpark.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
O="$ROOT/output"
LOG="$O/build.log"

total=$(make -s -C "$ROOT/buildroot" O="$O" BR2_EXTERNAL="$ROOT" printvars VARS=PACKAGES 2>/dev/null \
        | tr ' ' '\n' | sed 's/^PACKAGES=//' | grep -c '[a-z]')

done=$(find "$O/build" -maxdepth 2 -name '.stamp_*_installed' 2>/dev/null \
       | sed 's#.*/build/##; s#/\.stamp.*##' | sort -u | grep -c '.' || true)

steps=$(grep -c '>>>' "$LOG" 2>/dev/null || echo 0)
cur=$(grep '>>>' "$LOG" 2>/dev/null | tail -1 | sed 's/.*>>> //; s/\x1b\[[0-9;]*m//g')

pct=$(( total > 0 ? done * 100 / total : 0 ))
filled=$(( pct / 5 ))
bar=$(printf '%*s' "$filled" '' | tr ' ' '#')$(printf '%*s' $((20 - filled)) '' | tr ' ' '.')

if [ -f "$O/images/disk.img" ]; then
	echo "[$bar] DONE - output/images/disk.img"
elif pgrep -f 'make.*BR2_EXTERNAL' >/dev/null 2>&1 || pgrep -f 'nice.*make build' >/dev/null 2>&1; then
	echo "[$bar] ${done}/${total} pkgs (~${pct}%)  |  ${steps} steps  |  now: ${cur}"
else
	echo "[$bar] ${done}/${total} pkgs  |  build not running (stopped or finished)"
fi
