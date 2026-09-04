# NeurOS build roadmap

## Architecture decisions (locked)

- **From-scratch Linux distro**, built with **Buildroot** (BR2_EXTERNAL = this repo).
  glibc + **systemd**. Not an Android fork, not a Debian/Alpine/pmOS fork.
- **Hardware enablement is Halium-style** (like Droidian / Ubuntu Touch - still a
  Linux distro, not Android): downstream Android kernel for sweet + vendor
  firmware blobs + **libhybris** to use the Android GPU/camera drivers from a
  glibc userspace. Chosen because the answer on drivers was "take the official
  ones from LineageOS + stock, don't reinvent" - and the open Freedreno/mainline
  path needs a mainline `msm` DRM that the downstream kernel doesn't have.
  (Re-confirmable at M5; the x86_64 dev target is unaffected by this.)
- **Dev target now:** x86_64, in **QEMU** (primary, fast headless iteration) and
  **VirtualBox** (for eyeballing). **aarch64/sweet target runs in parallel from M3.**
- **Hardware:** Redmi Note 10 Pro 4G "sweet" (SM6150, Adreno 618), 8 GB RAM
  variant + 4 GB zram -> ~12 GB budget. Bootloader unlock window ~2026-09-10
  (see the sweet-BL-unlock memory). Userspace bits are testable *now* on the
  still-locked phone via an aarch64 chroot under Termux.
- **UI:** a fork of **cage** (wlroots kiosk compositor, C) - one app fullscreen,
  exactly NeurOS's model - plus a lockscreen (`ext-session-lock-v1` + a minimal
  `neuros-lock` client). GLES2 throughout: shader-lerp gradient background +
  glyph atlas for text. FIGlet-style block font (runtime `.flf` parser) for the
  big status text; normal monospace for clock/date/battery. Layout: see
  "UI shell (v0)" and `docs/ui-mockup-v0.jpg`.

## Decisions by area

### A. Kernel & phone hardware
1. **Kernel:** downstream Android kernel for sweet (LineageOS
   `android_kernel_xiaomi_sm6150` / Xiaomi OSS `sweet-*-oss`), Android boot-image
   format. All driver-dependent bits come from the LineageOS device + vendor
   trees and stock HyperOS. **Pin the newest stable HyperOS fastboot ROM for
   sweet** (newer = more security/bug fixes; the device is on an early-2024
   build now but we harvest from current). Mainline kernel: a someday goal.
2. **GPU:** libhybris over the Adreno GLES/EGL blob (pairs with the downstream
   KGSL driver). Freedreno is out - it needs mainline `msm` DRM.
3. **Camera:** Android camera HAL via hybris (or a minimal HAL container).
   "Autonomous camera" is core to the concept but lands *after* first boot (M6/M7).
4. **Partitions:** our **own GPT**. **A/B slots.** Keep the stock firmware /
   modem / dsp / persist partitions untouched (blobs load from there). New:
   `boot_a/b`, `vbmeta_a/b`, `neuros_a/b` (erofs root, ~2 GB each), `data`
   (f2fs, remaining space, shared across slots).
5. **Verity/AVB:** sign `boot` + `vbmeta` with **our own key**. Bootloader stays
   unlocked (re-lock with a custom AVB key is a later, risky option).
6. **Recovery:** a fastboot-flashable known-good image + a boot-as-recovery
   ramdisk. TWRP optionally kept on the side during bring-up. *(implementer's call)*

### B. Compositor & UI
7. **Base:** fork **cage**. Add `ext-session-lock-v1` + `neuros-lock` for the
   **lockscreen**.
8. **Render:** GLES2 for everything (gradient shader + text atlas), one pipeline. *(call)*
9. **FIGlet:** runtime `.flf` parser. *(call)*
10. **Boot -> UI:** **seatd** + compositor as the seat session, no login manager
    (single-user appliance). systemd is PID 1.

### C. OS & updates
11. **Root fs:** **erofs**, read-only. *(call)*
12. **Updates:** **A/B seamless** via **swupdate**; failed boot -> automatic
    rollback to the other slot.
13. **Node runtime:** one shared Node in the image, from the official aarch64
    tarball (not the Buildroot `nodejs` package - version lag, CLIs are picky).
14. **Agent CLIs:** *not* bundled or auto-installed. The user guide documents
    pulling them from the on-device terminal into `/opt/neuros/agents/<agent>`
    (survives OTA - it's on the data partition).
15. **v1 agent:** **Claude Code only**, baked into the image. The 6-agent
    framework (color presets, mascots, switching) is deferred past v1.
16. **Destructive-command guard:** none of our own - rely on Claude Code's built-in
    permission / safety system (e.g. its own checks before `rm`-ing root).

### D. Agent runtime & audio
17. **`neuros-agentd`:** **Rust** (stream parsing, async, robustness). Injects
    the mandatory memory system prompt (`docs/agent-memory-prompt.md`) into every
    agent, same text for all backends.
18. **TTS routing:** a markdown-stripping filter sits between agent stdout and
    Piper - drop `**`/`__`, code fences, headings, turn links into their text,
    collapse tables, chunk into sentences. Raw text would have Piper saying
    "asterisk asterisk".
19. **Piper voices:** **male only** - ru `ruslan`, en `ryan`. Others downloadable. *(call)*
20. **STT / mic:** **on-screen** mic + camera buttons in the chat pane (a
    hardware volume long-press may be bound to mic later, not required). Mic
    toggles an always-listening stream: ON -> **vosk-small** does VAD
    segmentation (no wake word, any speech transcribed), **whisper.cpp `small`
    f16** transcribes each segment; OFF -> mic disabled. The agent also gets a
    tool to open the mic/camera itself.

### D2. Connectivity (WiFi + cellular data both required)
The SIM has unlimited data - mobile data is a first-class path, not optional.
- **WiFi:** `qcacld-3.0` (the big out-of-tree driver in the downstream kernel,
  WCN3990) + `wcnss`/`cnss` firmware + `wpa_supplicant`.
- **Bluetooth:** WCN3990 via `hci_qca` / `btqca` (+ `wcnss_filter`).
- **Cellular data:** modem firmware brought up by `rmtfs` + `qrtr-ns` +
  `pd-mapper` + `tqftpserv` (remoteproc). Data path via **ofono** with the
  RIL-over-`libhybris` plugin (consistent with the Halium choice); fallback
  `ModemManager` + `libqmi` + `rmnet_data0`. APN config shipped + editable.
- **Network manager:** **NetworkManager** on the phone (WiFi + ModemManager,
  WiFi-preferred routing, simple UI hooks). `systemd-networkd` stays only on the
  x86 dev VM.
- Voice calls / VoLTE: out of scope for now (data + SMS only).
Buildroot additions: `qcacld` firmware handling, `rmtfs`, `qrtr`, `pd-mapper`,
`tqftpserv`, `ofono`, `networkmanager`, `modemmanager`, `libqmi`.

### E. Dev workflow
21. Build **both** QEMU and VirtualBox images from the x86_64 config.
22. aarch64 target wired up **from M3**; full-system tests in QEMU, userspace
    smoke tests (agentd, Piper, whisper, vosk) in an aarch64 Termux chroot on
    the locked phone - even before unlock.

## UI shell (v0)

Mockup: `docs/ui-mockup-v0.jpg`. Portrait, thick rounded-rect frames, the
agent-color gradient wallpaper showing behind everything.

```
 time, date                                    battery      <- thin strip, plain mono
+-------------------------------------------------------+
|                    Ai-name  (FIGlet)                  |   top pane: agent + model
|                       model                          |
+-------------------------------------------------------+
|                                                      |
|            center pane - mode-switched:              |
|                                                      |
|   - mascot : full-pane AI animation (Claude ->       |
|             Claw'd, pending rights; generic until)   |
|   - chat   : embedded VT running the agent CLI.      |
|             Claude Code's own rendering - tool       |
|             calls, file-edit diffs (Update(path)     |
|             +4 -2 + hunk), thinking. Fits the TTY    |
|             look. Overlaid: [photo] [mic] buttons.   |
|   - camera : live viewfinder; replaces the pane      |
|             until photo taken / cancelled; image     |
|             goes to the agent.                       |
|                                                      |
+-------------------------------------------------------+
|            Working / Thinking / Waiting / Idle        |   bottom pane: state (FIGlet)
|                  using *tool-name*                    |   + activity ("reading important.md")
+-------------------------------------------------------+
```

**Plumbing** - candidate design (M3): the agent CLI runs inside a **tmux**
session; `foot` in the centre pane runs `tmux attach` (read-only for the user).
`neuros-agentd` then only orchestrates, not a VT emulator:
- `tmux new-session -d claude` to start / restart the agent,
- `tmux pipe-pane` tees the pane output -> markdown filter -> Piper,
- `tmux send-keys` injects STT transcripts,
- parses the piped stream for status / tool events -> bottom pane (via a small
  IPC to neuros-comp, e.g. a Unix socket + a `set_status` request),
- exposes a **`camera.capture` / `camera.view`** tool (and mic open) so the
  agent can look / listen on its own.
Prototype agentd as a shell script; move to **Rust** once the design holds.
tmux handles the pty, scrollback, and the tee/inject for free.

Compositor (cage fork) owns the 4 zones, the gradient wallpaper (GLES shader),
frame chrome, the on-screen buttons, and the lockscreen. Milestones: M2 = the
static 4-zone shell + VT + FIGlet panes; M3 = agentd pty wiring; M4 = mic
button <-> stream; M7 = camera pane + mascot animation + `camera.*` tool.

**Wallpaper:** per-agent vertical 2-stop gradient, `logo colour -> darker shade
of the same colour`, GLES shader lerp. Full preset table is in the brief
(`MEMORY.md`). Note: the ChatGPT agent uses **Codex purple** (`#AB68FF`), *not*
black - black is Kimi's colour, they must stay distinct. Gemini is the one
special case (white base + 4-stop Google hue band), also in the brief.

## What we harvest from sweet's stock firmware / LineageOS

Halium-style, so more is reused than a pure-mainline build:

1. **Downstream kernel** - built from GPL source (LineageOS / Xiaomi OSS). Not a blob.
2. **Firmware files** -> `/lib/firmware` / `/vendor/firmware`: Adreno GMU/zap,
   WiFi/BT (`wcnss`/`cnss`/`qca` for `qcacld-3.0`), modem (`mba.mbn`,
   `modem.mbn`, `modemuw`), DSP (`adsp`, `cdsp`), Venus. Plain files, copied
   as-is. Modem NV / `rmtfs` data comes from the stock `modemst1/2` + `fsg`
   partitions - leave those in place.
3. **Vendor blobs** (`/vendor` from LineageOS `proprietary-files.txt`): Adreno
   GLES/EGL userspace, camera HAL, sensor libs - loaded through **libhybris**.
4. **Device tree / dtbo** - from the kernel tree.

Buildroot additions this implies: `libhybris`, `android-headers`, and possibly a
minimal Android property/HAL container (for camera). Packaged in this BR2_EXTERNAL.

## Milestones

- [x] **M0 - build chain boots** (x86_64): boots in VirtualBox, systemd reports
  `running` with 0 failed units, autologin root, sshd on :2222. Bootlin external
  toolchain (gcc 14 / glibc). `make config && make && make vm && make run-headless`.
- [x] **M1 - graphics stack**: mesa3d 26, libdrm, wlroots 0.19.2, libseat,
  libinput, libxkbcommon, **foot** + DejaVu Mono in the image. `package/neuros-comp`
  = fork of cage 0.2.1, builds and runs in the VM (headless backend, Wayland
  display up, child spawn/reap OK). VBox exposes no 3D -> **llvmpipe** added for
  GLES2-in-software (LLVM building now). `/dev/dri/card0` (vmwgfx) present.
- [x] **M2 - UI shell**: renders + autostarts in the VM, end to end. `shell.c` -
  48-band gradient wallpaper, 4-zone geometry, tint+border framed panes (scaled
  thickness), client confined to the centre pane, agent name / state word as
  FIGlet block text (`figlet.c` + bundled `banner.flf`, one scene rect per ink
  cell). Borderless `foot` in the centre pane (C.UTF-8 generated). `neuros-comp
  -k` keeps the shell up with no client. Autostart: getty@tty1 -> profile.d ->
  `neuros-session` -> `neuros-comp -k -s -d -- foot`. **`ipc.c`** control socket
  + `neuros-ctl` - `agent`/`status`/`colors` verified driving the panes live.
  **Small mono text done** (`textbuf.c`: UTF-8 -> fcft -> pixman -> wlr_buffer):
  top strip clock/date/battery (`neuros-clock` -> `neuros-ctl strip`) and the
  bottom "using <tool>" activity sub-line (`neuros-ctl activity`, agentd maps
  Bash/Edit/Read/Grep/Web -> a phrase, cleared on Idle). Verified in the VM -
  the shell now matches the mockup on all zones.
  Battery is right-aligned (`neuros-ctl strip_right`). The "using <tool>" line
  still grazes the status word's descenders by ~2px - cosmetic, left as-is
  (tried narrowing the FIGlet / taller panes, both made overflow worse).
  Deferred: lockscreen (`ext-session-lock-v1`); erofs + overlay-/etc on x86.
- [ ] **M2 - UI shell**: the static 4-zone shell (see "UI shell (v0)") - thin
  time/date/battery strip, top agent/model pane, center pane, bottom state pane;
  `.flf` parser + GLES glyph atlas for the FIGlet panes; embedded VT in the
  center; on-screen photo/mic buttons; **lockscreen** (`ext-session-lock-v1` +
  `neuros-lock`).
- [~] **M3 - agent runtime**: prototype working in the VM. `neuros-agentd`
  (shell) runs the agent in a **tmux** session, `foot` in the centre pane
  attaches to it; `tmux pipe-pane` -> `tts-filter` (markdown/ANSI stripped) ->
  `/run/neuros/tts.txt`; `agent-status` classifies output -> `neuros-ctl status`
  -> bottom pane (verified: mock agent -> "Working"). Compositor is now a systemd
  service (`neuros-comp.service`, owns tty1, `Conflicts=getty@tty1`). `mock-agent`
  stands in for the real CLI.
  Next: Piper (M4) on the tts stream; real Claude Code in the image + the
  mandatory memory prompt (`docs/agent-memory-prompt.md`) + `/home/claude/memory/
  {you,topics,area}`; STT inject via `tmux send-keys`; Rustify once stable.
  **aarch64 build target.**
- [x] **M4 - audio**: full voice loop works in the VM (TTS buffered, STT + mic
  listener + VAD, whisper 'small' at ~2x RTF).
  - **TTS**: `piper` (prebuilt) + `piper-voices` (en ryan, ru ruslan). Synth
    valid WAV; `agentd` speaks filtered prose via `piper --output-raw | aplay`.
  - **STT**: `whisper-cpp` built from source (+OpenBLAS) + `whisper-model`
    (ggml-small.bin, f16, 466 MB) + `neuros-stt` (WAV -> text -> `tmux send-keys`).
  - **Verified end to end**: piper synth "what is the capital of France" -> WAV
    -> `neuros-stt` -> whisper transcribes "What is the capital of France?" ->
    injected into the agent session -> agent responds.
  - **whisper `small` RTF ~2.3x in the VM** after enabling ggml AVX2/FMA/F16C
    (was ~15x on generic x86-64: 31s -> 6.9s for a 3s clip). openblas needs a
    pinned x86 CPU variant - skipped for x86, revisit on ARM at M5.
  - **TTS response buffering done**: agentd accumulates the reply and speaks it
    once on status->Idle (was line-by-line), `is_prose()` drops prompt/TUI noise.
  - **Mic listener done** (`neuros-listen`): `arecord` window -> `whisper-cli
    --vad` (Silero v5) -> `tmux send-keys`. `neuros-mic on|off|toggle` flag.
    Verified: silence -> no injection (VAD works); whisper+VAD on real speech
    transcribes exactly. Only the live-mic capture link is unverified (no mic in
    the headless VM). Coarse fixed-window recording; streaming endpointing later.
  Next: on-screen mic/camera buttons in the shell (input hit-testing in shell.c);
  or a hardware key -> `neuros-mic` via seat.c.
- [ ] **M5 - aarch64 / sweet target**: `neuros_sweet_defconfig`; downstream
  kernel package; `libhybris` + `android-headers` packages; firmware manifest;
  own-GPT + A/B layout; our AVB key; swupdate.
- [ ] **M6 - first flash** (after unlock ~2026-09-10): bring-up order
  display -> touch -> **wifi (`qcacld-3.0`)** -> GPU (hybris) -> audio -> sensors
  -> **modem + cellular data (`rmtfs`/`qrtr` + ofono, `rmnet_data0`)** -> BT.
- [ ] **M7**: camera pane + `camera.*` agent tool (hybris HAL) + autonomy;
  mascot animation; Happ VPN from source (aarch64); the 6-agent framework.

### Parallel tasks, doable now (bootloader still locked)
- Download the newest-stable HyperOS fastboot ROM for sweet + clone LineageOS
  `android_device_xiaomi_sweet` / `vendor_xiaomi_sweet`; diff `proprietary-files.txt`
  against the dump -> firmware + blob manifest for M5.
- Stand up an aarch64 Termux chroot on the phone for userspace smoke tests.

## Still open
- **Big-font look.** User picked FIGlet **Standard** over `banner`. It now renders
  correctly (`flf_render_string` + kerning/smushing + fcft mono, sized to the
  pane, screenshots `output/m2-std*.png`) - but Standard is thin line-art, so at
  display size it reads low-contrast on the gradient, not bold like
  `docs/ui-mockup-v0.jpg`. Options: (a) a thicker/blockier `.flf` (e.g. `block`,
  `colossal`, a custom one), (b) a bold TTF via fcft (drop FIGlet for the big
  text), (c) accept thin Standard. Both `neuros-standard.flf` and
  `neuros-banner.flf` are bundled; `NG_FONT_PATH` selects.
- Lockscreen unlock gesture (PIN / any-key / pattern).
- Camera pane + on-screen mic/camera button placement.
- Pick the exact newest-stable HyperOS fastboot ROM build for sweet.
- Claw'd mascot: rights request sent to Anthropic (pending).

## Storage layout

**Phone:** read-only image root + one persistent data partition.

| mount        | fs / mechanism                          | notes                          |
|--------------|-----------------------------------------|--------------------------------|
| `/`          | **erofs**, read-only (lz4hc)            | shipped in the A/B slot image  |
| `/etc`       | **overlayfs** (lower=image, upper=data) | machine-id, ssh keys, wifi persist |
| `/var`       | dir on data partition (bring-up)        | real logs; -> tmpfs later      |
| `/home`, `/opt` | dirs on data partition, rw           | agent installs + memory        |
| swap         | 4 GB zram                               |                                |

- data partition fs: **f2fs** (flash-optimized, power-loss safe).
- mounts pivoted by a tiny initramfs.
- Buildroot pieces present: `BR2_TARGET_ROOTFS_EROFS`, `BR2_PACKAGE_F2FS_TOOLS`,
  `CONFIG_OVERLAY_FS` (already in the kernel config).

**x86_64 dev:** plain rw ext4 for M0; erofs + overlay layout added at M1.

## MLT / NeuroCut packaging (resolved)

- Buildroot has **no `mlt` package** -> add `package/mlt/` to this BR2_EXTERNAL.
- **No SWIG / python bindings needed**: NeuroCut shells out to the `melt` CLI +
  `ffmpeg` + `fc-cache` (verified in `neurocut/render.py`, `probe.py`). CLI-only.
- Modules: core + `avformat` (ffmpeg) + `pango` (pango/cairo/fontconfig) for
  titles. Disable qt6/frei0r/gtk/opengl/rtaudio/sdl.
- mcp-paint: `skia-python` has aarch64 wheels -> thin wheel-fetch package or a
  runtime venv. numpy is a BR package.

## STT sizing reference (SD732G, CPU only)

whisper.cpp multilingual GGML - disk / peak RAM / real-time factor:

| model  | f16 disk | f16 RAM | q5_1 disk | q5_1 RAM | RTF (q5_1) |
|--------|---------:|--------:|----------:|---------:|-----------:|
| tiny   |   75 MB  | ~275 MB |   ~31 MB  | ~180 MB  | ~0.4x      |
| base   |  142 MB  | ~390 MB |   ~57 MB  | ~230 MB  | ~0.8x      |
| small  |  466 MB  | ~950 MB |  ~181 MB  | ~600 MB  | ~2x        |
| medium |  1.5 GB  | ~2.6 GB |  ~514 MB  | ~1.5 GB  | ~6x        |

Vosk (Kaldi, streaming, per language): small ru/en ~40-50 MB disk, 250-500 MB
RAM, very light CPU (used here only as VAD). Large models not on device.
Piper TTS: ~20-65 MB/voice, ~50-150 MB RAM, faster than realtime.
