/*
 * NeurOS compositor - render UTF-8 text to a wlr_buffer via fcft. MIT.
 * Only for the small mono text (top strip); FIGlet text is scene rects.
 */
#ifndef NG_TEXTBUF_H
#define NG_TEXTBUF_H

struct fcft_font;
struct wlr_buffer;

/* Render one line of `utf8` with `font` in `color` (straight RGBA, 0..1).
 * Returns a wlr_buffer (caller: wlr_scene_buffer_set_buffer + wlr_buffer_drop),
 * or NULL. Fills *out_w / *out_h if non-NULL. No wrapping, single line. */
struct wlr_buffer *ng_text_render(struct fcft_font *font, const char *utf8, const float color[4], int *out_w,
				  int *out_h);

/* Emboldens the NEXT ng_text_render call by `px` extra strikes, then resets. */
void ng_text_set_bold(int px);

/* Frosted-glass panel: rounded rect (radius `rad`), faint top->bottom white fill
 * gradient, ~1.5px lighter border, inset top highlight. `dark` != 0 fills a
 * darker translucent centre (the chat pane). Buffer is premultiplied ARGB. */
struct wlr_buffer *ng_panel_render(int w, int h, int rad, int dark);

/* Panel + an accent border and a soft outer glow of `glow` px. The returned
 * buffer is (w+2*glow) x (h+2*glow); position the node at (box.x-glow, box.y-glow). */
struct wlr_buffer *ng_panel_render_ex(int w, int h, int rad, int dark, const float accent[3], int glow);

/* Filled rounded rect of one straight-alpha `color` (pills, home indicator). */
struct wlr_buffer *ng_pill_render(int w, int h, int rad, const float color[4]);

/* A pill (`pillcol`) with `text` centred in `font`/`textcol`, `padx`/`pady`
 * around the text. Returns a wlr_buffer or NULL. */
struct wlr_buffer *ng_pill_text_render(struct fcft_font *font, const char *text, const float textcol[4],
				       const float pillcol[4], int padx, int pady);

/* Filled circle of `color`; if `ring` > 0, a `ring`-px outline in `ringcol`. */
struct wlr_buffer *ng_dot_render(int d, const float color[4], int ring, const float ringcol[4]);

/* Round glass overlay button, `d` px. `icon`: 0 = camera, 1 = microphone.
 * `bg` fills the disc, `ring` is a 1.5px rim, `fg` draws the glyph (all straight
 * RGBA). Buffer is premultiplied ARGB, d x d. */
struct wlr_buffer *ng_button_render(int d, int icon, const float bg[4], const float ring[4], const float fg[4]);

/* Wrap a heap `data` array (w*h premultiplied ARGB8888, stride w*4) in a
 * wlr_buffer. Takes ownership of `data` (freed with the buffer). NULL on OOM. */
struct wlr_buffer *ng_argb_buffer(unsigned int *data, int w, int h);

#endif
