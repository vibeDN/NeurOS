/*
 * Minimal FIGlet .flf parser + renderer. See figlet.h. MIT.
 *
 * .flf layout:
 *   line 0:  "flf2a" <hardblank> <height> <baseline> <maxlen> <oldlayout>
 *            <comment_lines> [<print_dir> <full_layout> <codetag_count>]
 *   next <comment_lines> lines: free-text comment
 *   then, for codepoints 32..126 in order: <height> lines each, every line
 *   ending in one or more endmark chars; the last line of a glyph ends in two.
 *   (Code-tagged glyphs after that are ignored.)
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "figlet.h"

struct reader {
	const char *p;
	const char *end;
};

/* copy one line (without the newline) into a freshly malloc'd NUL-terminated
 * string; advance past the newline. returns NULL at EOF. */
static char *
read_line(struct reader *r)
{
	if (r->p >= r->end)
		return NULL;
	const char *nl = memchr(r->p, '\n', (size_t) (r->end - r->p));
	const char *stop = nl ? nl : r->end;
	size_t n = (size_t) (stop - r->p);
	/* trim a trailing CR */
	if (n > 0 && r->p[n - 1] == '\r')
		n--;
	char *s = malloc(n + 1);
	if (s) {
		memcpy(s, r->p, n);
		s[n] = '\0';
	}
	r->p = nl ? nl + 1 : r->end;
	return s;
}

/* strip trailing endmark chars from a glyph row. FIGlet uses the last visible
 * char of the first row as the endmark; we just strip any run of the row's
 * final character (commonly '@'), and also a lone trailing run of '@'. */
static void
strip_endmarks(char *row)
{
	size_t n = strlen(row);
	if (n == 0)
		return;
	char mark = row[n - 1];
	while (n > 0 && row[n - 1] == mark)
		n--;
	row[n] = '\0';
}

static struct flf_font *
parse(struct reader *r)
{
	char *hdr = read_line(r);
	if (!hdr || strncmp(hdr, "flf2a", 5) != 0) {
		free(hdr);
		return NULL;
	}

	struct flf_font *f = calloc(1, sizeof(*f));
	if (!f) {
		free(hdr);
		return NULL;
	}

	const char *h = hdr + 5;
	f->hardblank = *h ? *h : '$';
	int comment_lines = 0;
	/* height baseline maxlen oldlayout comment_lines ... */
	if (sscanf(h + 1, "%d %d %d %*d %d", &f->height, &f->baseline, &f->max_len, &comment_lines) < 4 ||
	    f->height <= 0 || f->height > 256) {
		free(hdr);
		free(f);
		return NULL;
	}
	free(hdr);

	for (int i = 0; i < comment_lines; i++) {
		char *c = read_line(r);
		if (!c) {
			flf_free(f);
			return NULL;
		}
		free(c);
	}

	for (int g = 0; g < FLF_COUNT; g++) {
		f->glyph[g] = calloc(f->height, sizeof(char *));
		if (!f->glyph[g]) {
			flf_free(f);
			return NULL;
		}
		int w = 0;
		for (int row = 0; row < f->height; row++) {
			char *line = read_line(r);
			if (!line) {
				flf_free(f);
				return NULL;
			}
			strip_endmarks(line);
			/* substitute hardblank -> space for layout */
			for (char *q = line; *q; q++)
				if (*q == f->hardblank)
					*q = ' ';
			int len = (int) strlen(line);
			if (len > w)
				w = len;
			f->glyph[g][row] = line;
		}
		f->glyph_w[g] = w;
	}

	return f;
}

struct flf_font *
flf_load_mem(const char *buf, size_t len)
{
	struct reader r = {.p = buf, .end = buf + len};
	return parse(&r);
}

struct flf_font *
flf_load(const char *path)
{
	FILE *fp = fopen(path, "rb");
	if (!fp)
		return NULL;
	fseek(fp, 0, SEEK_END);
	long sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (sz <= 0) {
		fclose(fp);
		return NULL;
	}
	char *buf = malloc((size_t) sz);
	if (!buf) {
		fclose(fp);
		return NULL;
	}
	size_t got = fread(buf, 1, (size_t) sz, fp);
	fclose(fp);
	struct flf_font *f = flf_load_mem(buf, got);
	free(buf);
	return f;
}

void
flf_free(struct flf_font *f)
{
	if (!f)
		return;
	for (int g = 0; g < FLF_COUNT; g++) {
		if (f->glyph[g]) {
			for (int row = 0; row < f->height; row++)
				free(f->glyph[g][row]);
			free(f->glyph[g]);
		}
	}
	free(f);
}

struct flf_render *
flf_render_text(const struct flf_font *f, const char *text)
{
	size_t tlen = strlen(text);

	int cols = 0;
	for (size_t i = 0; i < tlen; i++) {
		int c = (unsigned char) text[i];
		if (c < FLF_FIRST || c > FLF_LAST)
			c = ' ';
		cols += f->glyph_w[c - FLF_FIRST];
	}
	if (cols <= 0)
		cols = 1;

	struct flf_render *out = calloc(1, sizeof(*out));
	if (!out)
		return NULL;
	out->rows = f->height;
	out->cols = cols;
	out->cell = calloc((size_t) out->rows * cols, 1);
	if (!out->cell) {
		free(out);
		return NULL;
	}

	int xoff = 0;
	for (size_t i = 0; i < tlen; i++) {
		int c = (unsigned char) text[i];
		if (c < FLF_FIRST || c > FLF_LAST)
			c = ' ';
		int gi = c - FLF_FIRST;
		int gw = f->glyph_w[gi];
		for (int row = 0; row < f->height; row++) {
			const char *src = f->glyph[gi][row];
			int slen = (int) strlen(src);
			for (int x = 0; x < slen && x < gw; x++) {
				if (src[x] != ' ')
					out->cell[(size_t) row * cols + xoff + x] = 1;
			}
		}
		xoff += gw;
	}

	return out;
}

void
flf_render_free(struct flf_render *r)
{
	if (!r)
		return;
	free(r->cell);
	free(r);
}
