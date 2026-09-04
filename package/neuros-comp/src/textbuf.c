/*
 * NeurOS compositor - render a line of UTF-8 text into a wlr_buffer via fcft,
 * for the small mono text (top strip clock/date/battery). MIT.
 *
 * FIGlet block text (agent name / status) does NOT use this - it's drawn as
 * scene rects in shell.c. This is only for real proportional/mono glyphs.
 */
#define _POSIX_C_SOURCE 200809L

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
				pixman_image_composite32(PIXMAN_OP_OVER, src, g->pix, dst, 0, 0, 0, 0,
							 pen + g->x, baseline - g->y, g->width, g->height);
			pen += g->advance.x;
		}
	}

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

struct wlr_buffer *
ng_grid_render(const unsigned char *cells, int cols, int rows, int cell, const float color[4])
{
	if (!cells || cols < 1 || rows < 1 || cell < 1)
		return NULL;
	int W = cols * cell, Hh = rows * cell;
	size_t stride = (size_t) W * 4;
	uint32_t *data = calloc((size_t) Hh, stride);
	if (!data)
		return NULL;

	uint32_t a = (uint32_t) (color[3] * 255);
	uint32_t px = (a << 24) | ((uint32_t) (color[0] * color[3] * 255) << 16) |
		      ((uint32_t) (color[1] * color[3] * 255) << 8) | (uint32_t) (color[2] * color[3] * 255);

	for (int gy = 0; gy < rows; gy++)
		for (int gx = 0; gx < cols; gx++) {
			if (!cells[gy * cols + gx])
				continue;
			for (int y = 0; y < cell; y++) {
				uint32_t *r = data + (size_t) (gy * cell + y) * W + gx * cell;
				for (int x = 0; x < cell; x++)
					r[x] = px;
			}
		}

	struct ng_tbuf *t = calloc(1, sizeof(*t));
	if (!t) {
		free(data);
		return NULL;
	}
	wlr_buffer_init(&t->base, &tbuf_impl, W, Hh);
	t->data = data;
	t->stride = stride;
	return &t->base;
}
