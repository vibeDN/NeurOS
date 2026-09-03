# NeurOS status - autonomous session (2026-09-04, ~00:50-03:30 local)

## Done: M0 -> M4, all verified in a VirtualBox VM

| milestone | state | proof |
|-----------|-------|-------|
| M0 build chain | done | boots, systemd `running`, 0 failed units, ssh :2222 |
| M1 graphics    | done | mesa3d 26 + llvmpipe, wlroots 0.19, `neuros-comp` (cage fork) runs |
| M2 UI shell    | done | 4-zone layout, gradient wallpaper, FIGlet text, control socket |
| M3 agent       | done (proto) | mock agent in tmux, output -> status pane, markdown filter |
| M4 audio       | done | **piper TTS (en/ru) + whisper STT; full voice loop verified** |

**Full voice loop test:** piper synthesizes "what is the capital of France" ->
`neuros-stt` -> whisper.cpp `small` transcribes it exactly -> `tmux send-keys`
into the agent -> agent responds. Text in, speech out, speech in, text out.

## Repo layout of what was built

```
package/neuros-comp/     fork of cage 0.2.1: shell.c (4-zone + gradient),
                         figlet.c (.flf parser), ipc.c (control socket),
                         neuros-ctl, -k flag, centre-pane client confinement
package/neuros-agentd/    shell orchestrator: tmux plumbing, tts-filter,
                         agent-status classifier, mock-agent, neuros-stt
package/piper/            prebuilt piper (bundles onnxruntime + espeak-ng)
package/piper-voices/     en_US-ryan-medium + ru_RU-ruslan-medium
package/whisper-cpp/      built from source (+OpenBLAS), whisper-cli + VAD tool
package/whisper-model/    ggml-small.bin (f16, 466 MB)
overlay/                 systemd units (neuros-comp.service), branding, locale
board/neuros/x86_64/      kernel config, grub, genimage, post-build
```

Image: ~946 MB (466 MB is the whisper model). 41 commits.

## Key decisions made along the way (also in ROADMAP)

- Bootlin external toolchain (don't build glibc from source).
- llvmpipe for GLES2 in the VM (no VBox 3D); phone uses Adreno via hybris.
- Compositor = fork of **cage** (not from scratch); wlr_scene for the shell.
- FIGlet text = one `wlr_scene_rect` per ink cell (no glyph rasteriser needed).
- Agent plumbing = **tmux** (pipe-pane tees, send-keys injects) - agentd stays thin.
- Piper + whisper.cpp shipped as packages; `small` model, no quantization.
- Compositor runs as a systemd service owning tty1 (`Conflicts=getty@tty1`).

## Not done / needs you

- **M5 (aarch64 / sweet)** - the whole phone bringup. Needs your device
  knowledge + bootloader unlock (~2026-09-10). Halium-style: downstream kernel +
  libhybris + vendor blobs, own GPT, A/B, own AVB key.
- Real **mic capture** + VAD (arecord + whisper-vad or vosk) - the loop currently
  takes a WAV file, not a live mic.
- **Response buffering** for TTS (currently speaks line-by-line, choppy).
- **Real Claude Code** in the image (auth, Node) - mock-agent stands in now.
- `GGML_NATIVE` / `-march` tuning - whisper is ~15x RTF in the VM (generic x86-64).
- Small mono text in the top strip (clock/date/battery) - needs fcft text-to-buffer.
- Lockscreen (`ext-session-lock-v1`).
- erofs + overlay-/etc storage layout (still plain ext4 in the dev image).

## Open questions still parked (ROADMAP "Still open")

- Which exact newest-stable HyperOS build for sweet to harvest blobs from.
- Claw'd mascot rights (emailed Anthropic, pending).
