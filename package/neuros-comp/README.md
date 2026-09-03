# neuros-comp

The NeurOS Wayland compositor. **Fork of [cage](https://github.com/cage-kiosk/cage)**
(the wlroots kiosk compositor), base **v0.2.1** (targets wlroots-0.19, which is
what Buildroot ships). MIT, same as cage.

`src/` is the vendored cage 0.2.1 tree with local modifications. Upstream diff
points so far:

- `meson.build`: project renamed `cage` -> `neuros-comp`, `werror=false`
  (Bootlin gcc 14 vs 2024 code), man-pages off.

Planned (see `docs/ROADMAP.md` "UI shell (v0)"):
- 4-zone layout: time/date/battery strip, agent+model bar, center pane, state bar.
- Per-agent GLES gradient wallpaper (shader lerp, logo colour -> darker).
- FIGlet block font for the big text (runtime `.flf` parser).
- `ext-session-lock-v1` support for the lockscreen.
- One embedded terminal in the center pane (chat), plus mascot / camera modes.

The `CG_`/`CAGE_` prefixes in the C source are kept as-is for now to minimise the
diff against upstream; they'll migrate to `neuros`/`ng` over time.
