# NeurOS - how it should look

Reference mockup: `docs/ui-mockup-v0.jpg`.

## Overall

- Retro-TTY aesthetic. Wayland compositor, GPU-accelerated (GLES).
- Portrait orientation (phone). Thick rounded-rect frames around the panes.
- The per-agent gradient wallpaper shows behind everything.

## Layout - 4 stacked zones

```
 time, date  ······································  battery     <- thin strip, no frame, small mono
+-----------------------------------------------------------+
|                     Ai-name   (big, FIGlet)               |  <- top pane, framed
|                       model   (smaller)                   |
+-----------------------------------------------------------+
|                                                           |
|   CENTRE PANE - one of three modes:                       |  <- framed
|                                                           |
|   - mascot : full-pane AI animation. Claude gets Claw'd    |
|             (rights request sent to Anthropic; a generic   |
|             mascot until they reply). Shown when idle.     |
|                                                           |
|   - chat   : the agent CLI running in an embedded          |
|             terminal - its own native rendering (tool      |
|             calls, file-edit diffs like `Update(path)      |
|             +4 -2` with the hunk, thinking). Overlaid      |
|             on-screen buttons: [photo] [mic].              |
|                                                           |
|   - camera : live viewfinder, replaces the pane until a    |
|             photo is taken or cancelled; the image is      |
|             handed to the agent. The agent also has a      |
|             tool to open the camera / mic itself.          |
|                                                           |
+-----------------------------------------------------------+
|            Working / Thinking / Waiting / Idle            |  <- bottom pane, framed
|                    using *tool-name*                      |     state word big (FIGlet),
+-----------------------------------------------------------+     "using <tool>" smaller
```

## Wallpaper

Per-agent **vertical 2-stop gradient**: `logo colour -> a darker shade of the
same colour`, done as a GLES shader lerp.

| agent    | gradient                                                        |
|----------|----------------------------------------------------------------- |
| Claude   | `#D97757` -> darker shade                                        |
| ChatGPT  | `#AB68FF` (Codex purple) -> darker. **Not black** - black is Kimi |
| Gemini   | white base + horizontal 4-stop hue band `#EA4335 #FBBC05 #34A853 #4285F4`, blended over a vertical white->colour fade (stronger at the bottom, fades to white at top). Gemini only. |
| Kimi     | `#1A1A1A` -> `#000000`                                           |
| DeepSeek | `#4D6BFE` -> darker shade                                        |
| Qwen     | `#7B61FF` -> darker shade (kept distinct from ChatGPT's purple)  |

## Typography

- **Big text** (agent name, state word): FIGlet **Standard** font (`.flf`),
  full uppercase + lowercase, with kerning + smushing (the normal `figlet`
  look). Rendered via a monospace face so the art aligns.
- **Small text** (strip clock/date/battery, "using <tool>"): plain monospace.

## Behaviour

- **Lockscreen** (`ext-session-lock-v1`). Unlock gesture: TBD (your call).
- **Mic**: an on-screen toggle (in the chat pane, next to the camera button)
  flips an always-listening voice stream on/off. A hardware key can be bound to
  it too. When on: VAD segments -> Whisper transcribes -> goes to the agent.
- **6 agents** selectable (Claude, ChatGPT, Gemini, Kimi, DeepSeek, Qwen). v1
  ships **Claude only**; the framework carries the other five.
- Mascot animations are per-agent (Claude -> Claw'd, Gemini -> sparkle/star,
  etc.) - brand-permission emails are out for the ones that need them.

## Status states (bottom pane)

`Working` · `Thinking` · `Waiting` · `Idle` - plus the current tool as the
`using <tool>` sub-line, cleared when the agent goes Idle.
