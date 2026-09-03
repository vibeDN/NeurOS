# NeurOS build roadmap

## Architecture decisions (locked)

- **From-scratch Linux distro**, built with **Buildroot** (BR2_EXTERNAL = this repo).
  Not an Android fork, not a Debian/Alpine/pmOS fork.
- Target 1 (now): **x86_64**, runs in VirtualBox/QEMU. Development happens here
  while the phone's bootloader unlock window is still closed (~2026-09-10).
- Target 2 (later): **aarch64 / Redmi Note 10 Pro 4G "sweet"** (SM6150, Adreno 618).
- Init: **systemd** (logind seats/sessions for the compositor; unit management for
  the agent runtime). Forces a glibc toolchain.
- UI: our own **wlroots-based Wayland compositor** on DRM/KMS + GBM. FIGlet-style
  block font for the big status text; normal monospace for everything else.

## "The sweet drivers blob" - what actually exists

There is no single blob that makes non-Android Linux work on sweet. From the
stock Xiaomi fastboot ROM (`sweet_*_images_*.tgz`) we harvest, in order of value:

1. **Firmware files** (`/lib/firmware`): Adreno GMU/zap (`a615_*.bin` / a6xx),
   WiFi/BT (`cnss`/`qca`), modem (`mba.mbn`, `modem.mbn`), DSP (`adsp`, `cdsp`),
   Venus (video). These are plain files, copied as-is. Extract from `vendor.img`
   / the `firmware` partition, or take from `linux-firmware` where mirrored.
2. **Kernel**: built from source - Xiaomi's GPL tree
   `MiCode/Xiaomi_Kernel_OpenSource` branch `sweet-*-oss`. Not a blob. We rebase
   the touchscreen (Focaltech/Goodix/NVT), panel (DSI), and charger drivers onto
   a Buildroot-managed kernel; long-term goal is mainline + minimal downstream.
3. **Device tree**: from that kernel tree / `dtbo.img`.
4. **GPU userspace**: with **Freedreno + Mesa** (open, mature for a6xx) we do NOT
   need the proprietary Adreno GLES blobs. libhybris is the fallback only if
   Freedreno can't drive the panel/GPU.

Android HALs from `vendor.img` (hwbinder, bionic-linked) are **not usable** in a
from-scratch glibc Linux without an Android container - deliberately out of scope.

### Parallel task, doable now (bootloader still locked)
Download the sweet fastboot ROM (RU + global), unpack, inventory
`proprietary-files.txt` from the LineageOS `android_device_xiaomi_sweet` tree
against what's in the dump. Produces the firmware manifest for target 2.

## Milestones

- [ ] **M0 - build chain boots** (x86_64): `make config && make` -> `disk.img` ->
  VirtualBox -> autologin root shell under systemd, sshd reachable on :2222.
- [ ] **M1 - graphics stack**: mesa (swrast/virgl in VM), libdrm, wlroots,
  seatd/libseat, our compositor skeleton drawing a solid agent-color background.
- [ ] **M2 - FIGlet UI**: `.flf` parser + GLES text rendering; status line
  (Working/Thinking/Waiting/Idle + tool), clock/date/battery.
- [ ] **M3 - agent runtime (`neuros-agentd`)**: spawn selected agent CLI, pipe
  STT text -> stdin, stdout -> Piper TTS. Agent switching. Per-agent isolated
  memory dirs under `/home/<agent>/memory/{you,topics,area}`.
- [ ] **M4 - audio**: Piper (TTS, CPU ONNX) packaged; STT engine chosen + packaged.
- [ ] **M5 - aarch64 target**: `neuros_sweet_defconfig`, cross toolchain, kernel
  from Xiaomi OSS tree, firmware manifest, `boot.img`/`super` layout.
- [ ] **M6 - first flash** (after 2026-09-10 unlock): fastboot the aarch64 image,
  bring up display -> touch -> wifi -> GPU -> camera -> modem, in that order.
- [ ] **M7 - VPN**: Happ cross-compiled from source for aarch64.

## Open questions

- Compositor language: C (wlroots native) vs Rust (smithay). Leaning wlroots/C
  for the smallest dependency surface on ARM.
- Writable storage layout: read-only squashfs root + overlayfs, vs plain ext4.
  The AI CLIs self-update and write memory, so at least `/opt` + `/home` writable.
- STT: whisper.cpp (heavier, better) vs vosk (lighter). TBD on 732G budget.
