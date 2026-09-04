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

/* Render a `cols`x`rows` ink grid as solid `color` blocks, `cell` px each.
 * For the filled FIGlet block text. Returns a wlr_buffer or NULL. */
struct wlr_buffer *ng_grid_render(const unsigned char *cells, int cols, int rows, int cell, const float color[4]);

#endif
