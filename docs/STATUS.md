# NeurOS status - autonomous session (2026-09-04, ~00:50 -> ~05:00 local)

## Done: M0 -> M4, all verified in a VirtualBox VM. 53 commits.

| milestone | state | proof |
|-----------|-------|-------|
| M0 build chain | done | boots, systemd `running`, 0 failed, ssh :2222 |
| M1 graphics    | done | mesa3d 26 + llvmpipe, wlroots 0.19, `neuros-comp` (cage fork) |
| M2 UI shell    | done | 4-zone layout matches the mockup: gradient wallpaper, framed |
|                |      | panes, FIGlet agent/status text, **fcft strip clock + activity line** |
| M3 agent       | done (proto) | mock agent in tmux, output->status/activity panes, md filter |
| M4 audio       | done | **full voice loop**: TTS (piper, buffered) + STT (whisper small |
|                |      | + Silero VAD + mic listener), whisper ~2x RTF after AVX2 |

**Voice loop verified:** piper synth "what is the capital of France" -> `neuros-stt`
-> whisper transcribes exactly -> `tmux send-keys` -> agent responds -> reply
spoken as one utterance.

## The shell now (docs/ui-mockup-v0.jpg for comparison)

- **strip**: `HH:MM   Day DD Mon   BAT nn%` - real mono via fcft, updated 20s
- **top pane**: agent name (FIGlet `banner`) - `neuros-ctl agent <name>`
- **centre pane**: `foot` attached to the agent's tmux session, borderless
- **bottom pane**: state word (FIGlet) + "using <tool>" sub-line (fcft)
- wallpaper: per-agent 2-stop gradient (`neuros-ctl colors #top #bot`)

## Compositor control socket (`neuros-ctl` / `$XDG_RUNTIME_DIR/neuros-comp.sock`)

`agent <name>` · `status <Working|Thinking|Waiting|Idle>` · `activity <text>` ·
`strip <text>` · `colors <#top> <#bot>`

## Packages built this session

```
neuros-comp/     cage 0.2.1 fork: shell.c (4-zone+gradient), figlet.c (.flf),
                 textbuf.c (fcft->wlr_buffer), ipc.c (socket), neuros-ctl, -k
neuros-agentd/    tmux orchestrator + tts-filter + agent-status + neuros-stt +
                 neuros-listen (mic+VAD) + neuros-mic + neuros-clock + mock-agent
piper / piper-voices     prebuilt piper + ryan (en) + ruslan (ru)
whisper-cpp / whisper-model   from source (+AVX2), ggml-small.bin + Silero VAD
```

Image ~950 MB (466 MB is the whisper model). Compositor runs as
`neuros-comp.service` on tty1 (`Conflicts=getty@tty1`), libseat builtin backend.

## Needs you (parked - not guessed)

- **M5 (aarch64 / sweet)** - the phone bringup. Device knowledge + bootloader
  unlock (~2026-09-10). Halium-style per the locked decisions.
- **Real Claude Code** in the image (Node + auth) - `mock-agent` stands in.
- **Lockscreen** UX - `ext-session-lock-v1` plumbing is clear; the unlock
  gesture (PIN? any-key? pattern?) is your call.
- **Camera pane** + on-screen mic/camera buttons - needs input hit-testing in
  the compositor and your UX call on button placement/size.
- Live-mic capture is the one unverified link in the STT path (no mic in the VM).

## Smaller polish left (safe to do solo next)

- erofs read-only root + overlay-/etc + f2fs data (still plain ext4 in dev image).
- NTP/timezone (VM shows UTC).
- Right-align the battery in the strip; nudge the activity line up a few px.
- `GGML` `-march` tuning for the ARM target at M5.
- Buffer whole STT windows with proper endpointing (currently fixed 5s windows).

## Still open (ROADMAP)

- Exact newest-stable HyperOS build for sweet to harvest blobs from.
- Claw'd mascot rights (emailed Anthropic, pending).
