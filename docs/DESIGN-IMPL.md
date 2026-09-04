# NeurOS shell - implementing the Claude Design pass

Source: `Agent OS - Home.dc.html` / `Agent OS - Lockscreen.dc.html` (Claude Design
export, 2026-09-04). Design language: **iOS-style glassmorphism**, **Doto** font
for the big text, near-white text on the accent gradient.

## What changes from the current shell

| current | design |
|---------|--------|
| thick near-black frame boxes | **translucent glass panels** - rounded 30px, white gradient fill ~12% opacity, 1px light border, inset top highlight, drop shadow (no real backdrop-blur in wlr_scene - skip or fake) |
| near-black text on gradient | **near-white text** `#fdfbf8` / `#f2efe9`, with a soft shadow |
| FIGlet `.flf` art for big text | the **Doto** font (variable, wght ~900, uppercase) rendered via fcft. Drop FIGlet for the shell (keep figlet.c around, unused). |
| ad-hoc mono | **JetBrains Mono** for all small text |
| no agent dots | **6-dot scaffold row** under the strip - active dot full + white ring, others `opacity .32` |
| model not shown | **model pill** under the agent name (translucent rounded pill, JBMono 11px) |
| - | **home-indicator bar** at the bottom: 120x4.5 rounded, accent colour, opacity .55 |

## Layout (design is 393x852; scale to the output)

```
 pad 14/20/4   HH:MM  <dim>date</dim>            BAT nn%      strip, #fdfbf8 11.5px
 center, gap 9, pad 6/0/2       o o O o o o                   agent dots (9px), active O has ring
 pad 8/16/14, gap 12, flex column:
   GLASS  pad 18/10/16 centre   [ Doto AGENT-NAME 40px ]      #fdfbf8 + shadow
                                (  model-pill 11px  )
   GLASS  bg rgba(10,9,8,.4) flex:1   <mascot | chat | camera>
          overlay btns bottom-right: [cam] [mic]  (38px round glass)
   GLASS  pad 16/10/14 centre   * [ Doto STATE 26px ]         glowing dot + label
                                using <tool>  11px opacity .65
   centre        [====== 120x4.5 ======]                      home indicator
```

## Accent pairs (bg gradient = linear 180deg pair[0] -> pair[1])

| agent    | pair                    |
|----------|-------------------------|
| Claude   | `#D97757` -> `#4a2415`  |
| ChatGPT  | `#AB68FF` -> `#2e1a54`  |
| Gemini   | special (white base + 4-stop Google band) - later |
| Kimi     | `#1A1A1A` -> `#000000`  |
| DeepSeek | `#4D6BFE` -> `#1a2461`  |
| Qwen     | `#7B61FF` -> `#2e2266`  |

`neuros-ctl colors <#top> <#bot>` already does the gradient; add a preset table
keyed by agent name so `neuros-ctl agent <name>` also sets the colours + model.

## Central panel modes

- **mascot** (idle): a soft animated blob - expanding ring + a radial-gradient
  orb (accent light -> dark) + a white glow. `dcRing` 2.8s, `dcOrb1` 3.4s.
  Compositor: a few scene nodes with a slow timer, or a tiny GLES shader.
- **chat**: the embedded terminal (foot+tmux) - already done. The design shows a
  styled diff card / typing reply; that's the agent CLI's own rendering, keep it.
- **camera**: viewfinder + back(X) + shutter. M6/M7.

## Buttons

Round glass, `border-radius:50%`, translucent, blur. Camera + mic bottom-right of
the central panel (38px). Mic ON: accent border + accent-tint bg. Lockscreen:
mic / lock(56px) / camera in a row; **unlock = tap the lock button** (the design's
gesture - no PIN).

## Compositor gaps this needs

- **Rounded-corner panels + gradient fill + border**: render each panel as a
  pixman buffer (rounded rect path, vertical alpha gradient, 1px stroke, inset
  highlight line) -> `wlr_scene_buffer`, sized per layout. ~80 lines in textbuf.c.
- **Backdrop blur**: not possible in wlr_scene without a custom GLES pass. v1:
  skip (translucent white over the gradient still reads as "frosted-ish").
- **Input hit-testing** for the on-screen buttons: the compositor must catch
  pointer/touch events in the button rects and fire `neuros-mic` / camera. cage's
  seat.c handles input; add button regions + a cursor/touch handler.
- **Doto weight**: it's a variable font (`wght`, `ROND` axes). Try
  `fcft_from_name("Doto:weight=210")` (fontconfig black); fall back to a static
  Doto-Black if fcft can't instance it.

## Order of work

1. fonts bundled (Doto + JBMono) + fontconfig picks them up.  [done - mk]
2. shell.c: light text, Doto big text via fcft, JBMono small.
3. glass panels (pixman rounded-rect buffers) replacing the black frames.
4. agent dots row + model pill + home indicator.
5. per-agent colour/model preset table via `neuros-ctl agent`.
6. mascot blob animation.
7. on-screen buttons + input hit-testing.
8. lockscreen (`ext-session-lock-v1` + a `neuros-lock` client matching the design).
