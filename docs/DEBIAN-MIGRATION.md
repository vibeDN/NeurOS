# Dev host migration: Gentoo -> Debian 13 (trixie)

2026-09-06. The build host was re-installed (same PC, Gentoo -> Debian 13). What
broke and how the dev environment is put back.

## What did NOT carry over

- `output/` (the whole buildroot tree) - **not portable across host libc.**
  The Gentoo host built `output/host/*` against **glibc >= 2.42**; Debian trixie
  ships **2.41**, so dozens of host binaries fail to load:
  ```
  glib-compile-schemas: /lib/x86_64-linux-gnu/libc.so.6: version `GLIBC_2.43' not found
  ```
  affected: `grub-*`, `genimage`, `libext2fs` / `mke2fs`, `depmod`/`insmod`,
  `bison`, `gperf`, `libglib`/`libgio`, most of util-linux, ...
  The cross **compiler** happened to still run (so `neuros-comp` rebuilt fine via
  ninja), but image assembly dies at `target-finalize`.
  **Fix: `rm -rf output && make neuros_x86_64_defconfig && make`** - a full world
  rebuild on Debian. The Bootlin external toolchain is re-fetched (a tarball, not
  rebuilt). `BR2_DL_DIR` (`~/.cache/neuros/dl`) also did not survive, so every
  source re-downloads (~GBs, slow on the default Debian mirror) the first time.

- `~/.local/platform-tools` (Google adb/fastboot) - replaced with Debian pkgs.
- `~/.local/share/miunlock-venv` + `~/.migatesession/` (Xiaomi unlock CLI +
  cached login) - see the `sweet BL unlock` memory.
- VirtualBox - not installed, and **not in Debian `main`** (needs `contrib` +
  `non-free` + `linux-headers` + DKMS + `vboxusers` re-login). The VM scripts
  (`scripts/make-vm.sh`, `make run`) are VirtualBox-only.

## Fresh Debian setup

```sh
# device tools (Debian main)
sudo apt install -y adb fastboot            # 1:34.0.5, in plugdev already

# buildroot host prerequisites
sudo apt install -y build-essential git wget cpio unzip rsync bc \
                    python3 python3-venv file which \
                    ruby                    # <- wpewebkit's code generators (FindRuby)

# VM (option A: QEMU, matches ROADMAP "primary")
sudo apt install -y qemu-system-x86 qemu-utils    # KVM is in-kernel, no DKMS
#   boot output/images/disk.img directly (raw); screenshot via QMP `screendump`

# VM (option B: VirtualBox, "for eyeballing")
sudo sed -i 's/ main$/ main contrib non-free non-free-firmware/' /etc/apt/sources.list
sudo apt update
sudo apt install -y linux-headers-amd64 virtualbox virtualbox-dkms virtualbox-qt
sudo usermod -aG vboxusers "$USER"          # then re-login / `newgrp vboxusers`
```

## Known-broken packages after migration

- **`wpewebkit`** (`fa3858f`, WIP):
  - CMake `FindRuby` failed with no `ruby` on the host - install Debian `ruby`.
  - It picks up the **system** `/usr/bin/cmake` for `--regenerate-during-build`;
    keep `output/host/bin` early in `PATH` when poking the build by hand.
  - **OOMs a 16 GB box.** WebKit's parallel C++ compile needs many GB/job;
    `BR2_JLEVEL=6` + a desktop = kernel OOM-kill mid-`WebCore`. To build it you
    need `BR2_JLEVEL=1`, tens of GB of swap, or a bigger machine. Until then
    disable it in `output/.config` (`BR2_PACKAGE_COG` / `WPEWEBKIT` /
    `WPEBACKEND_FDO` / `LIBWPE` off, then `make olddefconfig`) to get an image.
- **`claude-code`**: pinned to a host binary version that no longer exists after
  updates - fixed in `3a7a1e3` to take the newest under
  `~/.local/share/claude/versions/`.

## Runtime fixes for the rebuilt image (`e646fa1`)

- `neuros-session` forced `LIBSEAT_BACKEND=builtin`, but the defconfig only
  builds the seatd **daemon** backend -> compositor crash-loop
  (`No backend matched name 'builtin'`). Now auto-probes.
- `WLR_RENDERER=gles2` + `LIBGL_ALWAYS_SOFTWARE=1` SEGVs in EGL on mesa 26 when
  the vGPU advertises hardware ("Not allowed to force software rendering ...").
  Default is now the **pixman** renderer (software, no EGL). gles2 for the phone
  via `/etc/neuros/display.conf`.

## Incremental `neuros-comp` builds without a working host meson

`output/host/bin/meson` has a `#!/usr/bin/env python3` shebang -> picks the
system python, which lacks `mesonbuild`. Until a full rebuild fixes the host
tree, prefix `PATH` with the host python:

```sh
PATH="$PWD/output/host/bin:$PATH" \
  ninja -C output/build/neuros-comp-0.1.0/buildroot-build neuros-comp
```
