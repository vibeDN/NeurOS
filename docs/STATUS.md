# NeurOS status - 2026-09-06

x86_64 dev target. ~70 commits. M0-M4 done and previously VM-verified; M2 shell
has had a full design + interaction pass since. M5 (aarch64/sweet) not started -
gated on the bootloader unlock (see the `sweet BL unlock` memory).

## Milestones

| milestone | state | notes |
|-----------|-------|-------|
| M0 build chain | done | Bootlin external toolchain, systemd PID 1, ssh |
| M1 graphics    | done | mesa 26 + llvmpipe, wlroots 0.19, `neuros-comp` (cage fork) |
| M2 UI shell    | done + polished | see below |
| M3 agent       | proto+ | real Claude Code 2.1.263 now boots in the centre pane (`package/claude-code` ships the host binary); orchestration still the mock-agent shell |
| M4 audio       | done | full voice loop (piper TTS buffered, whisper-small + Silero VAD) |
| M5 aarch64/sweet | not started | needs the unlock + device bringup |
| M6 first flash | blocked | unlock window slipped (timer reset to ~287 h) |

## Shell / compositor (`package/neuros-comp`)

- **4-zone glass layout** - strip (clock/date/battery), agent-name pane, centre
  client pane, state pane, home indicator. Per-agent 2-stop gradient wallpaper.
- **Typography pass** - sized for the 1080px phone panel (the compositor is
  DPI-unaware); FIGlet "Big" font for the agent/state text, JetBrains Mono
  elsewhere.
- **Lockscreen** - compositor-drawn overlay: clock view -> 6-digit PIN pad
  (`/etc/neuros/passcode`, default `000000`), real "Cancel" pill. Input fully
  contained while locked (keys swallowed, OSK hidden, taps intercepted). Not yet
  promoted to `ext-session-lock-v1` (the remaining piece; deferred).
- **Power menu** - long-press Power -> Power off / Restart / Cancel pills.
- **Camera mode** - full-panel viewfinder over the centre pane with back +
  shutter; live-frame reload path done (synthetic pulse in the VM, needs
  `/dev/video0` on the phone).
- **On-screen keyboard** - see `docs/OSK.md`. 5 layers (EN/RU/?123/#+=/emoji),
  runtime 4-group xkb keymap, synthetic `wlr_keyboard`, long-press alternates
  (accents, symbol punctuation, `ё`), globe long-press layout picker, space-bar
  caret trackpad.
- **Hardware side keys** (`seat.c`, evdev 116/115/114): Power short = screen
  toggle / long = power menu; Vol short = TTS volume / long = TTS toggle;
  Power+VolUp = 2nd workspace terminal.

## Control socket (`neuros-ctl` -> `$XDG_RUNTIME_DIR/neuros-comp.sock`)

`agent` · `status` · `activity` · `strip` · `strip_right` · `colors` ·
`mic` · `camera on|off|toggle` · `lock` / `unlock` / `lock "HH:MM|..."` ·
`screen on|off|toggle` · `power [hide]` · `kbd tap|press|release|toggle|on|off`

## Verified in QEMU 2026-09-06 (fresh Debian build)

Boots at 1080x2400, pixman renderer. Screenshots: Claude Code running in the
centre pane; OSK long-press accents (`e`->è é ê ë), globe->layout picker
(EN/RU/?12/:)), symbol alts (`-`->– —); settings overlay. `scripts/qemu-phone.sh`.

## Current blockers / TODO

- **Dev host migrated Gentoo -> Debian 13** - full `output/` rebuild done and
  working. See `docs/DEBIAN-MIGRATION.md`.
- **`wpewebkit`** (`fa3858f`, WIP browser) - `FindRuby` (fixed: install `ruby`)
  **and OOMs a 16 GB box** during the WebCore compile. Disabled in `.config` for
  now; needs `BR2_JLEVEL=1` / big swap / a bigger machine.
- **Real Claude Code** in the image (Node + auth) - `mock-agent` stands in.
- **`ext-session-lock-v1`** - deferred (input is already contained).
- Live-mic capture, camera `/dev/video0`, erofs ro-root + f2fs data - all still open.

## Bootloader unlock (sweet)

Bind confirmed on the RU datacenter; the wait timer got reset + extended to
~287 h (~2026-09-18) after post-migration re-checks. No free Qualcomm wait-bypass
exists for `sweet` (EDL needs an authed Firehose loader; no eng ABL published).
Plan: wait it out, unlock via `miunlock`. Full detail in the `sweet BL unlock`
memory.
