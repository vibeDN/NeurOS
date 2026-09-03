/*
 * Minimal FIGlet .flf parser + renderer for the NeurOS status text.
 *
 * We don't do smushing: characters are concatenated at full width. The render
 * output is a bitmap of "ink" cells - the compositor draws one filled rect per
 * ink cell, which is all the block-font look needs. MIT.
 */
#ifndef NG_FIGLET_H
#define NG_FIGLET_H

#include <stddef.h>
#include <stdint.h>

#define FLF_FIRST 32
#define FLF_LAST 126
#define FLF_COUNT (FLF_LAST - FLF_FIRST + 1)

struct flf_font {
	int height;
	int baseline;
	int max_len;
	char hardblank;
	/* glyphs[c - FLF_FIRST] is an array of `height` NUL-terminated rows */
	char **glyph[FLF_COUNT];
	int glyph_w[FLF_COUNT]; /* widest row, in columns */
};

struct flf_render {
	int rows;
	int cols;
	uint8_t *cell; /* rows*cols, 1 = ink */
};

struct flf_font *flf_load(const char *path);
struct flf_font *flf_load_mem(const char *buf, size_t len);
void flf_free(struct flf_font *f);

/* Render ASCII `text` (chars outside 32..126 become spaces). Caller frees via
 * flf_render_free(). Returns NULL on allocation failure. */
struct flf_render *flf_render_text(const struct flf_font *f, const char *text);
void flf_render_free(struct flf_render *r);

#endif
