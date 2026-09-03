#!/usr/bin/env bash
# Rough build progress: packages installed / total packages in this config.
# Buildroot has no real total-work number - step count doesn't track wall time
# (the kernel is ~8 steps and many minutes) - so this is a ballpark.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
O="$ROOT/output"
LOG="$O/build.log"

# Buildroot has no reliable total-work number, and printvars PACKAGES undercounts.
# So: during the FIRST build just show raw counts. After a build finishes we
# stash the true >>> step total in .build-steps-baseline and show a real bar.
BASELINE="$O/.build-steps-baseline"

pkgs=$(find "$O/build" -maxdepth 2 -name '.stamp_*_installed' 2>/dev/null \
       | sed 's#.*/build/##; s#/\.stamp.*##' | sort -u | grep -c '.' || true)
steps=$(grep -c '>>>' "$LOG" 2>/dev/null || echo 0)
cur=$(grep '>>>' "$LOG" 2>/dev/null | tail -1 | sed 's/.*>>> //; s/\x1b\[[0-9;]*m//g')
kernel_done=$([ -f "$O/images/bzImage" ] && echo yes || echo no)

running=false
pgrep -f 'nice.*make build' >/dev/null 2>&1 && running=true
pgrep -f 'buildroot.*BR2_EXTERNAL' >/dev/null 2>&1 && running=true

last_done=$(grep -c 'NeurOS: image ready' "$LOG" 2>/dev/null || echo 0)
if ! $running && [ "$last_done" -ge 1 ] && [ -f "$O/images/disk.img" ]; then
	[ -n "$steps" ] && echo "$steps" > "$BASELINE"
	echo "[####################] DONE ($(tail -1 "$LOG" | grep -c Leaving >/dev/null && echo 'clean' || echo '?')) - disk.img $(date -r "$O/images/disk.img" '+%H:%M')"
	exit 0
fi

if [ -f "$BASELINE" ]; then
	tot=$(cat "$BASELINE"); pct=$(( steps * 100 / (tot>0?tot:1) )); [ "$pct" -gt 99 ] && pct=99
	filled=$(( pct / 5 ))
	bar=$(printf '%*s' "$filled" '' | tr ' ' '#')$(printf '%*s' $((20-filled)) '' | tr ' ' '.')
	pre="[$bar] ~${pct}%  ${steps}/${tot} steps"
else
	pre="first build (no baseline yet):  ${pkgs} pkgs, ${steps} steps, kernel=${kernel_done}"
fi

if $running; then
	echo "${pre}  |  now: ${cur}"
else
	echo "${pre}  |  build NOT running - check output/build.log"
fi
