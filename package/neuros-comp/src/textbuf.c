/*
 * NeurOS compositor - render a line of UTF-8 text into a wlr_buffer via fcft,
 * for the small mono text (top strip clock/date/battery). MIT.
 *
 * FIGlet block text (agent name / status) does NOT use this - it's drawn as
 * scene rects in shell.c. This is only for real proportional/mono glyphs.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <drm_fourcc.h>
#include <fcft/fcft.h>
#include <pixman.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/util/log.h>

#include "textbuf.h"

struct ng_tbuf {
	struct wlr_buffer base;
	uint32_t *data; /* ARGB8888 */
	size_t stride;
};

static void
tbuf_destroy(struct wlr_buffer *b)
{
	struct ng_tbuf *t = wl_container_of(b, t, base);
	free(t->data);
	free(t);
}

static bool
tbuf_begin(struct wlr_buffer *b, uint32_t flags, void **data, uint32_t *format, size_t *stride)
{
	struct ng_tbuf *t = wl_container_of(b, t, base);
	if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
		return false;
	*data = t->data;
	*format = DRM_FORMAT_ARGB8888;
	*stride = t->stride;
	return true;
}

static void
tbuf_end(struct wlr_buffer *b)
{
}

static const struct wlr_buffer_impl tbuf_impl = {
	.destroy = tbuf_destroy,
	.begin_data_ptr_access = tbuf_begin,
	.end_data_ptr_access = tbuf_end,
};

/* minimal UTF-8 -> UTF-32; returns count, fills cps (caller-sized) */
static size_t
utf8_decode(const char *s, uint32_t *cps, size_t cap)
{
	size_t n = 0;
	const unsigned char *p = (const unsigned char *) s;
	while (*p && n < cap) {
		uint32_t cp;
		int extra;
		if (*p < 0x80) {
			cp = *p;
			extra = 0;
		} else if ((*p & 0xe0) == 0xc0) {
			cp = *p & 0x1f;
			extra = 1;
		} else if ((*p & 0xf0) == 0xe0) {
			cp = *p & 0x0f;
			extra = 2;
		} else if ((*p & 0xf8) == 0xf0) {
			cp = *p & 0x07;
			extra = 3;
		} else {
			cp = 0xfffd;
			extra = 0;
		}
		p++;
		for (int i = 0; i < extra; i++) {
			if ((*p & 0xc0) != 0x80) {
				cp = 0xfffd;
				break;
			}
			cp = (cp << 6) | (*p++ & 0x3f);
		}
		cps[n++] = cp;
	}
	return n;
}

/* poor-man's emboldening: re-strike each glyph N extra px to the right.
 * ng_text_set_bold(n) affects the next ng_text_render call, then resets. */
static int g_bold = 0;
void
ng_text_set_bold(int px)
{
	g_bold = px < 0 ? 0 : px;
}

/* width in px of one UTF-32 line */
static int
line_width(struct fcft_font *font, const uint32_t *cps, size_t n)
{
	int w = 0;
	for (size_t i = 0; i < n; i++) {
		const struct fcft_glyph *g = fcft_rasterize_char_utf32(font, cps[i], FCFT_SUBPIXEL_NONE);
		if (g)
			w += g->advance.x;
	}
	return w;
}

struct wlr_buffer *
ng_text_render(struct fcft_font *font, const char *utf8, const float color[4], int *out_w, int *out_h)
{
	if (!font || !utf8 || !utf8[0])
		return NULL;

	/* decode, splitting into lines on '\n' */
	uint32_t cps[4096];
	size_t total = utf8_decode(utf8, cps, 4096);
	if (total == 0)
		return NULL;

	size_t lstart[64];
	size_t llen[64];
	int nlines = 0;
	size_t s = 0;
	for (size_t i = 0; i <= total && nlines < 64; i++) {
		if (i == total || cps[i] == '\n') {
			lstart[nlines] = s;
			llen[nlines] = i - s;
			nlines++;
			s = i + 1;
		}
	}

	int line_h = font->ascent + font->descent;
	int width = 1;
	for (int l = 0; l < nlines; l++) {
		int w = line_width(font, cps + lstart[l], llen[l]);
		if (w > width)
			width = w;
	}
	int height = line_h * nlines;
	if (width < 1 || height < 1)
		return NULL;

	size_t stride = (size_t) width * 4;
	uint32_t *data = calloc((size_t) height, stride);
	if (!data)
		return NULL;

	pixman_image_t *dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, width, height, data, stride);
	pixman_color_t pc = {
		.red = (uint16_t) (color[0] * 0xffff),
		.green = (uint16_t) (color[1] * 0xffff),
		.blue = (uint16_t) (color[2] * 0xffff),
		.alpha = (uint16_t) (color[3] * 0xffff),
	};
	pixman_image_t *src = pixman_image_create_solid_fill(&pc);

	for (int l = 0; l < nlines; l++) {
		int pen = 0;
		int baseline = l * line_h + font->ascent;
		for (size_t i = 0; i < llen[l]; i++) {
			const struct fcft_glyph *g =
				fcft_rasterize_char_utf32(font, cps[lstart[l] + i], FCFT_SUBPIXEL_NONE);
			if (!g)
				continue;
			if (g->pix)
				for (int b = 0; b <= g_bold; b++)
					pixman_image_composite32(PIXMAN_OP_OVER, src, g->pix, dst, 0, 0, 0, 0,
								 pen + g->x + b, baseline - g->y, g->width,
								 g->height);
			pen += g->advance.x + (g_bold ? 1 : 0);
		}
	}
	g_bold = 0;

	pixman_image_unref(src);
	pixman_image_unref(dst);

	struct ng_tbuf *t = calloc(1, sizeof(*t));
	if (!t) {
		free(data);
		return NULL;
	}
	wlr_buffer_init(&t->base, &tbuf_impl, width, height);
	t->data = data;
	t->stride = stride;

	if (out_w)
		*out_w = width;
	if (out_h)
		*out_h = height;
	return &t->base;
}

/* -- shape helpers (glass panels, pills, dots) ------------------------- */

static uint32_t
premul(float r, float g, float b, float a)
{
	if (a < 0)
		a = 0;
	if (a > 1)
		a = 1;
	uint32_t A = (uint32_t) (a * 255 + 0.5f);
	uint32_t R = (uint32_t) (r * a * 255 + 0.5f);
	uint32_t G = (uint32_t) (g * a * 255 + 0.5f);
	uint32_t B = (uint32_t) (b * a * 255 + 0.5f);
	return (A << 24) | (R << 16) | (G << 8) | B;
}

static struct wlr_buffer *
buf_from_data(uint32_t *data, int w, int h)
{
	struct ng_tbuf *t = calloc(1, sizeof(*t));
	if (!t) {
		free(data);
		return NULL;
	}
	wlr_buffer_init(&t->base, &tbuf_impl, w, h);
	t->data = data;
	t->stride = (size_t) w * 4;
	return &t->base;
}

/* coverage 0..1 of pixel (px,py) inside a rounded rect [0,w]x[0,h] radius r,
 * with a 1px antialiased edge */
static float
rr_cover(float px, float py, int w, int h, int r)
{
	float dx = 0, dy = 0;
	if (px < r)
		dx = r - px;
	else if (px > w - r)
		dx = px - (w - r);
	if (py < r)
		dy = r - py;
	else if (py > h - r)
		dy = py - (h - r);
	if (dx == 0 && dy == 0)
		return 1.0f;
	float d = dx * dx + dy * dy;
	float rr = (float) r * r;
	if (d <= (r - 1) * (r - 1))
		return 1.0f;
	if (d >= rr)
		return 0.0f;
	return (rr - d) / (rr - (r - 1) * (r - 1));
}

struct wlr_buffer *
ng_panel_render(int w, int h, int rad, int dark)
{
	return ng_panel_render_ex(w, h, rad, dark, NULL, 0);
}

/* signed distance to a rounded rect [0,w]x[0,h] radius r: >0 inside, <0 outside */
static float
rr_sd(float px, float py, float w, float h, float r)
{
	float ox = px < r ? r - px : (px > w - r ? px - (w - r) : 0);
	float oy = py < r ? r - py : (py > h - r ? py - (h - r) : 0);
	float outside = sqrtf(ox * ox + oy * oy) - r;
	if (outside > 0)
		return -outside;
	float ix = px < w - px ? px : w - px;
	float iy = py < h - py ? py : h - py;
	float inside = ix < iy ? ix : iy;
	return inside;
}

struct wlr_buffer *
ng_panel_render_ex(int w, int h, int rad, int dark, const float accent[3], int glow)
{
	if (w < 2 || h < 2)
		return NULL;
	if (rad > w / 2)
		rad = w / 2;
	if (rad > h / 2)
		rad = h / 2;
	if (rad < 1)
		rad = 1;
	if (glow < 0)
		glow = 0;

	int W = w + 2 * glow, H = h + 2 * glow;
	uint32_t *data = calloc((size_t) W * H, 4);
	if (!data)
		return NULL;

	float ar = accent ? accent[0] : 1.0f;
	float ag = accent ? accent[1] : 1.0f;
	float ab = accent ? accent[2] : 1.0f;
	const float border = 2.0f;

	for (int y = 0; y < H; y++) {
		float py = y - glow + 0.5f;
		float grad = (float) (y - glow) / (h > 1 ? h - 1 : 1);
		if (grad < 0)
			grad = 0;
		if (grad > 1)
			grad = 1;
		for (int x = 0; x < W; x++) {
			float px = x - glow + 0.5f;
			float sd = rr_sd(px, py, w, h, rad);

			if (sd <= -1.0f) {
				/* outside the panel: soft accent glow */
				if (glow > 0 && -sd < glow) {
					float t = 1.0f - (-sd) / glow;
					float a = 0.22f * t * t;
					data[(size_t) y * W + x] = premul(ar, ag, ab, a);
				}
				continue;
			}

			float l, a;
			if (sd < border) {
				/* accent border ring */
				float e = sd < 0 ? 1.0f + sd : 1.0f;
				a = 0.55f * (e < 0 ? 0 : e);
				data[(size_t) y * W + x] = premul(ar, ag, ab, a);
				continue;
			}

			if (dark) {
				l = 0.03f;
				a = 0.46f - 0.08f * grad;
			} else {
				l = 1.0f;
				a = 0.15f - 0.11f * grad;
				if (sd < border + 2.5f && py < h / 2)
					a = 0.24f; /* inset top highlight */
			}
			data[(size_t) y * W + x] = premul(l, l, l, a);
		}
	}
	return buf_from_data(data, W, H);
}

struct wlr_buffer *
ng_pill_render(int w, int h, int rad, const float color[4])
{
	if (w < 2 || h < 2)
		return NULL;
	if (rad > w / 2)
		rad = w / 2;
	if (rad > h / 2)
		rad = h / 2;
	if (rad < 1)
		rad = 1;
	uint32_t *data = calloc((size_t) w * h, 4);
	if (!data)
		return NULL;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			float cov = rr_cover(x + 0.5f, y + 0.5f, w, h, rad);
			if (cov > 0.0f)
				data[(size_t) y * w + x] = premul(color[0], color[1], color[2], color[3] * cov);
		}
	return buf_from_data(data, w, h);
}

struct wlr_buffer *
ng_dot_render(int d, const float color[4], int ring, const float ringcol[4])
{
	if (d < 2)
		return NULL;
	uint32_t *data = calloc((size_t) d * d, 4);
	if (!data)
		return NULL;
	float c = (d - 1) / 2.0f, rad = d / 2.0f;
	for (int y = 0; y < d; y++)
		for (int x = 0; x < d; x++) {
			float dist = sqrtf((x - c) * (x - c) + (y - c) * (y - c));
			float cov = rad - dist;
			if (cov <= 0)
				continue;
			if (cov > 1)
				cov = 1;
			const float *col = color;
			if (ring > 0 && dist > rad - ring)
				col = ringcol;
			data[(size_t) y * d + x] = premul(col[0], col[1], col[2], col[3] * cov);
		}
	return buf_from_data(data, d, d);
}

struct wlr_buffer *
ng_pill_text_render(struct fcft_font *font, const char *text, const float textcol[4], const float pillcol[4],
		    int padx, int pady)
{
	if (!font || !text || !text[0])
		return NULL;

	/* rasterise the text into a scratch pixman image first */
	uint32_t cps[256];
	size_t n = utf8_decode(text, cps, 256);
	if (!n)
		return NULL;
	int tw = 0;
	for (size_t i = 0; i < n; i++) {
		const struct fcft_glyph *g = fcft_rasterize_char_utf32(font, cps[i], FCFT_SUBPIXEL_NONE);
		if (g)
			tw += g->advance.x;
	}
	int th = font->ascent + font->descent;
	if (tw < 1 || th < 1)
		return NULL;

	int W = tw + 2 * padx, H = th + 2 * pady;
	if (padx < 2)
		W = tw + 8;
	int rad = H / 2;

	uint32_t *data = calloc((size_t) W * H, 4);
	if (!data)
		return NULL;

	/* pill background */
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			float cov = rr_cover(x + 0.5f, y + 0.5f, W, H, rad);
			if (cov > 0.0f)
				data[(size_t) y * W + x] =
					premul(pillcol[0], pillcol[1], pillcol[2], pillcol[3] * cov);
		}

	/* text via pixman OVER */
	pixman_image_t *dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, W, H, data, (size_t) W * 4);
	pixman_color_t pc = {
		.red = (uint16_t) (textcol[0] * 0xffff),
		.green = (uint16_t) (textcol[1] * 0xffff),
		.blue = (uint16_t) (textcol[2] * 0xffff),
		.alpha = (uint16_t) (textcol[3] * 0xffff),
	};
	pixman_image_t *src = pixman_image_create_solid_fill(&pc);
	int pen = (W - tw) / 2, baseline = (H - th) / 2 + font->ascent;
	for (size_t i = 0; i < n; i++) {
		const struct fcft_glyph *g = fcft_rasterize_char_utf32(font, cps[i], FCFT_SUBPIXEL_NONE);
		if (!g)
			continue;
		if (g->pix)
			pixman_image_composite32(PIXMAN_OP_OVER, src, g->pix, dst, 0, 0, 0, 0, pen + g->x,
						 baseline - g->y, g->width, g->height);
		pen += g->advance.x;
	}
	pixman_image_unref(src);
	pixman_image_unref(dst);

	return buf_from_data(data, W, H);
}
