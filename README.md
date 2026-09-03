# NeurOS

A from-scratch Linux distro that turns a phone into a standalone AI-agent device
("Jarvis phone edition"): voice in (STT) -> agent CLI -> voice out (Piper TTS),
autonomous camera, per-agent memory, retro block-font UI on a Wayland compositor.

First dev target is **x86_64 in a VM**; the hardware target is the
**Redmi Note 10 Pro 4G "sweet"** (SM6150 / Adreno 618), pending bootloader unlock.

See [`docs/ROADMAP.md`](docs/ROADMAP.md) for architecture decisions and milestones,
and [`MEMORY.md`](MEMORY.md) for the full design brief.

## Layout

```
configs/            Buildroot defconfigs (neuros_x86_64_defconfig)
board/neuros/        per-arch kernel config, grub/genimage, post scripts
overlay/            rootfs overlay (branding, systemd drop-ins, networkd)
package/            NeurOS's own Buildroot packages (agentd, compositor, ...)
scripts/            helper scripts (make-vm.sh)
buildroot/          Buildroot 2026.02.3 LTS, cloned, git-ignored
output/             build output, git-ignored
```

## Build (x86_64)

```sh
make config      # load neuros_x86_64_defconfig
make             # full build -> output/images/disk.img  (~30-45 min first run)
make vm          # convert to VDI + create the "NeurOS-dev" VirtualBox VM
make run         # start the VM

# after boot:
ssh -p 2222 root@localhost      # password: neuros
tail -f output/serial.log        # serial console
```

Build parallelism is capped (`JLEVEL=8`, `nice`/`ionice`) to keep the desktop
responsive; override with `make JLEVEL=N`.
