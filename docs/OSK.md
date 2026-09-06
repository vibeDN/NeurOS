# NeurOS on-screen keyboard

`package/neuros-comp/src/osk.c` + `osk.h`. A soft keyboard the **compositor draws
itself** - no layer-shell protocol, no external OSK process. Taps are hit-tested
in `osk.c` and replayed as real key events through a synthetic `wlr_keyboard`, so
the focused client (`foot` / the agent CLI) just sees ordinary keystrokes.

## Layers

`enum layer` - one synthetic keyboard, five on-screen layouts:

| layer      | rows | contents                                            |
|------------|------|-----------------------------------------------------|
| `LY_EN`    | 4    | QWERTY, shift, `?123`, globe, emoji, space, `,` `.`, enter, hide |
| `LY_RU`    | 4    | ЙЦУКЕН (12/11/9 letter keys - needs the `[ ] ; ' , .` keycodes) |
| `LY_SYM`   | 4    | `?123` - digits row + common punctuation, `#+=` toggle |
| `LY_SYM2`  | 4    | `#+=` - brackets, math/currency (`€ £ ¥ § ° · × ÷ • …`) |
| `LY_EMOJI` | 5    | 42-emoji grid over the letter+digit keycodes         |

`osk->letter` remembers the last alphabetic layer (`LY_EN` / `LY_RU`) so `ABC`
and the globe return to the right one. The globe key: **tap** toggles EN↔RU,
**long-press** opens a layout picker (see below).

## xkb keymap - the 4-group trick

The synthetic keyboard loads a keymap built **at runtime** as a text string
(`build_keymap()`), compiled with `xkb_keymap_new_from_string`. It starts from
`pc+us+inet(evdev)` and then `override key`s the letter/digit keycodes to carry
several symbols each, one per xkb **group**:

| group | index¹ | name      | reached by                          |
|-------|--------|-----------|-------------------------------------|
| 1     | 0      | English   | `LY_EN`, `LY_SYM`, `LY_SYM2` (ASCII) |
| 2     | 1      | Russian   | `LY_RU` (Cyrillic keysym names)      |
| 3     | 2      | Emoji     | `LY_EMOJI` + emoji long-press cells  |
| 4     | 3      | Accents   | every long-press alternate (`ACC_GROUP`) |

¹ 0-based index passed to `wlr_keyboard_notify_modifiers(..., group)`.

**xkbcommon caps a key at 4 groups**, so group 4 is the last slot available -
every long-press alternate (accents, dashes, curly quotes, `ё`) has to share it.
Each alternate codepoint is therefore parked on **its own keycode's** group 4:

- `g_acc[0..31]`  - accent forms, on the 32 letter keycodes (`AD*12 AC*11 AB*9`)
- `g_acc[32..40]` - symbol forms (`– — … ¿ ¡ “ ” ‘ ’`), on 9 of the digit keycodes
- `g_acc[41]`     - `ё`, on the last digit keycode (`AE10`)

`build_accents()` fills `g_acc[]`; `acc_evdev(i)` maps an index back to its evdev
code; `key_alts()` returns the alternates string for a key on the current layer;
`alts_resolve()` turns that string into `(evdev code, label)` pairs by finding
each codepoint's slot. The popup then sends `code` in `ACC_GROUP`.

Currency/maths symbols on `LY_SYM2` use a similar scheme on the **F-keys**
(`FK01..FK10`, group 3) - see the `FK_SYM` table.

If the keymap fails to compile, `ng_osk_create` falls back to a plain US keymap
and logs `ng_osk: no keymap - key injection disabled` for the non-ASCII parts.

## Key injection

`osk_key_down()` / `osk_key_up()` / `osk_send()`:

1. `wlr_seat_set_keyboard(seat, &osk->kb)` - make the synthetic kb current
2. `wlr_keyboard_notify_modifiers(&osk->kb, shift?0x1:0, 0, 0, group)` then
   `wlr_seat_keyboard_notify_modifiers` - set the level (shift) and group
3. `wlr_keyboard_notify_key` + `wlr_seat_keyboard_notify_key` PRESSED
4. on release, key RELEASED + modifiers cleared

Most `KK_CHAR` keys press-and-hold (`osk_key_down` on press) so the client's own
key-repeat runs. Exceptions that commit **on release** so a long-press can
pre-empt them: any key with alternates (`key_alts` non-NULL) and the globe.
`enter` is a discrete tap (no repeat).

Repeat rate: `wlr_keyboard_set_repeat_info(&osk->kb, 28, 480)`.

## Long-press

`osk_arm_hold(osk, k)` starts a `wl_event_loop` timer (330 ms). If it fires
before release, `osk_hold_cb` opens a popup:

| held key                        | popup                                      |
|---------------------------------|--------------------------------------------|
| letter a/e/i/o/u/y/n/c/s/z (EN) | accent forms - `ACCENTS` table             |
| `-` `.` `?` `!` `"` `'` (SYM)   | `– —` / `…` / `¿` / `¡` / `“ ”` / `‘ ’` - `SYMALTS` |
| `е` (RU)                        | `ё`                                        |
| globe                           | layout picker: `EN` `RU` `?12` `:)`        |

`osk_popup_show()` lays the cells in a row over the source key (clamped to the
keyboard rect, flips below the key if there's no room above) and picks the cell
nearest the finger as the initial selection. While the press is held, pointer /
touch motion is routed to `ng_osk_motion()` (see routing) which re-highlights
the cell under the finger. Release commits the highlighted cell:

- accent / symbol popup (`popup_mode == 0`): `osk_send(code, ACC_GROUP)`
- layout picker (`popup_mode == 1`): switch `osk->layer` (+ `osk->letter`)

Releasing **before** the timer fires = a normal tap of the base key. Moving the
finger more than ~`keyh/3 + 6` px before it fires cancels the hold (you were
swipe-typing, not long-pressing).

## Input routing (`seat.c`)

```
touch/pointer PRESS  inside the OSK rect -> ng_osk_press()  -> seat->osk_grab = true
                     (consumed; not forwarded to the client)
touch/pointer MOTION while osk_grab       -> ng_osk_motion()  (drag-to-select)
touch/pointer RELEASE while osk_grab      -> ng_osk_release() -> seat->osk_grab = false
```

`osk_handle_press` also **shows** the keyboard when it's hidden and the tap
landed on a client surface (and still forwards that tap so the terminal places
its cursor). `ng_shell_refresh()` / `view_position_all()` run on every show/hide
so the client resizes above `ng_osk_top()` and the shell's camera/mic buttons
stay clear.

## Rendering

`osk_render()` paints one `PIXMAN_a8r8g8b8` buffer (`ng_argb_buffer` -> scene
buffer node). Dark translucent panel, rounded-rect keys (`fill_rr`, coverage AA),
labels via `draw_label` (fcft: `JetBrains Mono` + `Noto Emoji` fallback, size
`keyh * 36/100` clamped 10..34). Held key 0.42 alpha, hot/active key 0.30, idle
0.13. The long-press popup is a near-opaque dark card drawn on top, hot cell 0.5.

## Geometry

`ng_osk_layout(w, h)` sizes from **key size**, not a flat screen fraction: 11
columns across the usable width, keys 6:5 (a touch wider than tall), capped at
42 % of a portrait screen / 52 % landscape. A tall phone panel otherwise gave
2:1 portrait keys.

## Control socket (`neuros-ctl kbd ...`)

| command            | effect                                    |
|--------------------|-------------------------------------------|
| `kbd toggle`       | show / hide                               |
| `kbd on` / `kbd off` | force show / hide                        |
| `kbd tap X Y`      | synth a tap at layout coords (test hook)  |
| `kbd press X Y` / `kbd release` | split press/release (test hook) |

## Files

- `osk.c` - everything above
- `osk.h` - `ng_osk_create/destroy`, `_layout`, `_set_visible`, `_is_visible`,
  `_tap`, `_press`, `_release`, `_motion`, `_top`
- keymap validated out-of-tree with a small xkbcommon harness (compile the
  keymap, resolve every alternate) - see batch 11 in `ROADMAP.md`
