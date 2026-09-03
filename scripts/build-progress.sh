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

# printvars PACKAGES undercounts (skips a few implicit/virtual packages that
# still emit install stamps), so done can exceed total near the end. Clamp.
[ "$done" -gt "$total" ] && total="$done"
pct=$(( total > 0 ? done * 100 / total : 0 ))
[ "$pct" -gt 100 ] && pct=100
filled=$(( pct / 5 ))
bar=$(printf '%*s' "$filled" '' | tr ' ' '#')$(printf '%*s' $((20 - filled)) '' | tr ' ' '.')

running=false
pgrep -f 'nice.*make build' >/dev/null 2>&1 && running=true
pgrep -f 'buildroot.*BR2_EXTERNAL' >/dev/null 2>&1 && running=true

if [ -f "$O/images/disk.img" ]; then
	echo "[####################] DONE - output/images/disk.img"
elif $running; then
	case "$cur" in
		linux*|*rootfs*|*genimage*|*"Generating"*)
			echo "[$bar] ~${pct}% pkgs done, now on the slow tail  |  ${steps} steps  |  now: ${cur}" ;;
		*)
			echo "[$bar] ~${pct}% (${done} pkgs)  |  ${steps} steps  |  now: ${cur}" ;;
	esac
else
	echo "[$bar] build not running (stopped or finished); no disk.img yet"
fi
