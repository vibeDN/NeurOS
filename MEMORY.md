# neuros — project brief (context carry-over)

AI-agent operating system for repurposed Android phones. First target device:
Redmi Note 10 Pro 4G, codename **sweet**, Snapdragon 732G (SM6150), currently
mid bootloader-unlock wait.

## Backend
Each of the 6 selectable agents (Claude, ChatGPT, Gemini, Kimi, DeepSeek, Qwen)
runs via its **official CLI tool** (Claude Code, Gemini CLI, Codex CLI, etc.),
cross-compiled/ported to aarch64. Each CLI handles its own login flow natively
(account / API key / enterprise) — no custom auth code needed. neuros spawns
the selected agent's CLI process, pipes STT text into stdin, pipes its text
output to Piper for TTS.

Agents run with **root, no sandboxing** (personal single-user device, explicit
user decision) — recommended to still keep a basic guard against the most
obviously destructive shell commands as a backstop, not full sandboxing.

## Memory
`/home/<agent>/memory/{you,topics,area}/*.md` — **isolated per agent**, with an
explicit cross-read escape hatch: an agent can read another agent's memory
dir only when the user explicitly asks for it (tool granted on-demand, not in
default toolset).

Structure mirrors: `/you/*.md` (stable identity facts), `/topics/<x>.md`
(recurring habits/preferences — 2nd-mention rule, don't file on first passing
mention), `/area/<x>.md` (ongoing projects). Frontmatter: name/summary/updated.
Write trigger: NOT mid-conversation — either an incremental session-log
checkpoint (crash-safe) processed into memory at next boot, or explicit
`/exit`. Explicit "remember this" writes immediately.

## WM / rendering
**Wayland compositor, GPU-accelerated** (GLES, Adreno 618 on sweet).
Big "AI-name" / status text (Working/Thinking/Waiting/Idle using *tool*)
rendered via **FIGlet-style block font** (parse `.flf`, render as monospace
grid) for a retro-TTY look; everything else (time/date/battery) is normal
monospace UI text.

Background: vertical 2-stop gradient per agent, GPU shader lerp. **Gemini is
special**: white base layer + horizontal 4-stop hue band (red→yellow→green→
blue, Google's brand colors) blended on top with a vertical white-to-color
fade (stronger at bottom, fades to white at top) — this fade is Gemini-only,
not applied to other agents.

### Agent color presets
```
claude:   #D97757 → darker shade (top/bottom vertical)
chatgpt:  #AB68FF → darker shade
gemini:   base #FFFFFF, blend stops [#EA4335 #FBBC05 #34A853 #4285F4], fade top→bottom
kimi:     #1A1A1A → #000000
deepseek: #4D6BFE → darker shade
qwen:     #7B61FF → darker shade (distinct from chatgpt's purple)
```
Mini mascot/icon animations: Claude gets an **original** mascot (not Clawd —
trademark issue, Anthropic marketing request sent, no reply yet). Gemini's
sparkle/star request sent to Google's brand permission form (pending). OpenAI
and DeepSeek permission emails sent (to partnercomms@openai.com and
service@deepseek.com) — awaiting replies. Qwen and Kimi: user's own original
designs don't resemble their logos, no permission needed, proceeding as-is.

## VPN (for restricted-country users)
Happ is open-source (Xray-core/sing-box based) — **cross-compile from source
for aarch64** rather than repackaging the Debian .deb (wrong arch otherwise).
No hardware button-combo scheme needed: there's no separate "system layer" —
just the one console/terminal pane (the FIGlet UI screen) with a show/hide
toggle; Happ is run manually from that console like any CLI tool.

## Own tools (already built, MIT-licensed, vibeDN's own repos)
- **neurocut** (github.com/vibeDN/neurocut) — MCP video editor, MLT-framework
  based (needs ffmpeg/ffprobe/melt + Qt6/SDL/opengl deps — heaviest ARM build).
- **neuropaint-x** (github.com/vibeDN/neuropaint-x) — MCP layered paint canvas,
  skia-python based (ARM wheels exist, easier port). Start porting here first.
Both ~1000 tokens/edit session (efficient by design: downscaled previews,
batch ops, zero-token render/export).

## Bootloader unlock
sweet's normal Mi Unlock flow has a 7-day wait; user found a legacy account-
API endpoint Xiaomi didn't fully retire (3-day wait) and is using that
instead of the new Mi Community flow. Vendor blobs: official LineageOS device
tree (github.com/LineageOS/android_device_xiaomi_sweet) + vendor tree
(github.com/itsshashanksp/vendor_xiaomi_sweet) + sm6150-common blobs.

## TTS/STT
TTS: **Piper** (CPU-only ONNX, lightweight — right call for 732G; S1-mini
would be too heavy). STT: not yet decided.
