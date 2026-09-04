/*
 * Minimal FIGlet .flf parser + renderer with horizontal kerning + smushing.
 * See figlet.h. MIT.
 *
 * .flf header:  "flf2a" <hardblank> <height> <baseline> <maxlen> <oldlayout>
 *               <comment_lines> [<print_dir> <full_layout> <codetag_count>]
 * then <comment_lines> comment lines, then codepoints 32..126, <height> lines
 * each, every line ending in one or more endmark chars (two on the last line).
 *
 * Layout: we do "kerning" (slide each glyph left until it touches) plus the
 * smushing rules the Standard font uses (oldlayout 15 = equal | underscore |
 * hierarchy | opposite-pair). Good enough to match `figlet` output closely.
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "figlet.h"

/* smush rule bits (subset of the FIGlet layout mask) */
#define SM_EQUAL 1
#define SM_UNDERSCORE 2
#define SM_HIERARCHY 4
#define SM_PAIR 8

struct reader {
	const char *p;
	const char *end;
};

static char *
read_line(struct reader *r)
{
	if (r->p >= r->end)
		return NULL;
	const char *nl = memchr(r->p, '\n', (size_t) (r->end - r->p));
	const char *stop = nl ? nl : r->end;
	size_t n = (size_t) (stop - r->p);
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
	int comment_lines = 0, old_layout = -1;
	if (sscanf(h + 1, "%d %d %d %d %d", &f->height, &f->baseline, &f->max_len, &old_layout, &comment_lines) < 5 ||
	    f->height <= 0 || f->height > 256) {
		free(hdr);
		free(f);
		return NULL;
	}
	free(hdr);

	if (old_layout < 0)
		f->layout = -1; /* full width */
	else
		f->layout = old_layout; /* 0 = kerning, >0 = smush bits */

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
			int len = (int) strlen(line);
			if (len > w)
				w = len;
			f->glyph[g][row] = line; /* hardblank kept for smush logic */
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

/* --- smushing --------------------------------------------------------- */

static int
hierarchy_rank(char c)
{
	switch (c) {
	case '|':
		return 1;
	case '/':
	case '\\':
		return 2;
	case '[':
	case ']':
		return 3;
	case '{':
	case '}':
		return 4;
	case '(':
	case ')':
		return 5;
	case '<':
	case '>':
		return 6;
	default:
		return 0;
	}
}

/* try to smush chars a (left) and b (right); return the merged char, or 0 */
static char
smush_chars(char a, char b, char hardblank, int rules)
{
	if (a == ' ')
		return b;
	if (b == ' ')
		return a;

	if (a == hardblank || b == hardblank)
		return 0; /* leave hardblanks alone here */

	if ((rules & SM_EQUAL) && a == b)
		return a;

	if (rules & SM_UNDERSCORE) {
		const char *set = "|/\\[]{}()<>";
		if (a == '_' && strchr(set, b))
			return b;
		if (b == '_' && strchr(set, a))
			return a;
	}

	if (rules & SM_HIERARCHY) {
		int ra = hierarchy_rank(a), rb = hierarchy_rank(b);
		if (ra && rb && ra != rb)
			return ra > rb ? a : b;
	}

	if (rules & SM_PAIR) {
		if ((a == '[' && b == ']') || (a == ']' && b == '['))
			return '|';
		if ((a == '{' && b == '}') || (a == '}' && b == '{'))
			return '|';
		if ((a == '(' && b == ')') || (a == ')' && b == '('))
			return '|';
	}

	return 0;
}

/* Compose the FIGlet art for `text` into H NUL-terminated rows, each padded to
 * *out_w with spaces (hardblank -> space). Caller frees the rows + array. */
static char **
render_rows(const struct flf_font *f, const char *text, int *out_w)
{
	int H = f->height;
	size_t tlen = strlen(text);

	/* each output row is a fixed-width [cap] char array, space-filled to cur_w */
	int cap = 8 + (int) tlen * (f->max_len + 2);
	char **row = calloc(H, sizeof(char *));
	if (!row)
		return NULL;
	for (int r = 0; r < H; r++) {
		row[r] = malloc((size_t) cap);
		if (!row[r]) {
			for (int k = 0; k < r; k++)
				free(row[k]);
			free(row);
			return NULL;
		}
		memset(row[r], ' ', (size_t) cap);
	}
	int cur_w = 0;

	for (size_t i = 0; i < tlen; i++) {
		int cp = (unsigned char) text[i];
		if (cp < FLF_FIRST || cp > FLF_LAST)
			cp = ' ';
		int gi = cp - FLF_FIRST;
		int gw = f->glyph_w[gi];
		if (gw <= 0 || cur_w + gw + 2 >= cap)
			continue;

		/* glyph rows, space-padded to gw, hardblank->space */
		char **gr = calloc(H, sizeof(char *));
		for (int r = 0; r < H; r++) {
			gr[r] = calloc((size_t) gw + 1, 1);
			const char *src = f->glyph[gi][r];
			int sl = (int) strlen(src);
			for (int x = 0; x < gw; x++) {
				char c = x < sl ? src[x] : ' ';
				gr[r][x] = (c == f->hardblank) ? ' ' : c;
			}
		}

		/* classic FIGlet smushamount: per row, (blank slack) + at most one
		 * smushable column; overlap = min over rows */
		int overlap = 0;
		if (cur_w > 0 && f->layout >= 0) {
			overlap = gw < cur_w ? gw : cur_w;
			for (int r = 0; r < H; r++) {
				int tr = 0;
				while (tr < cur_w && row[r][cur_w - 1 - tr] == ' ')
					tr++;
				int lr = 0;
				while (lr < gw && gr[r][lr] == ' ')
					lr++;
				int amt = tr + lr;
				int li = cur_w - tr - 1; /* left's last ink */
				int ri = lr;             /* right's first ink */
				if (f->layout > 0 && li >= 0 && ri < gw &&
				    smush_chars(row[r][li], gr[r][ri], f->hardblank, f->layout))
					amt += 1;
				if (amt < overlap)
					overlap = amt;
			}
			if (overlap < 0)
				overlap = 0;
		}

		int start = cur_w - overlap;
		for (int r = 0; r < H; r++) {
			for (int x = 0; x < gw; x++) {
				char c = gr[r][x];
				if (c == ' ')
					continue;
				int col = start + x;
				char cur = row[r][col];
				if (cur == ' ' || col >= cur_w) {
					row[r][col] = c;
				} else {
					char m = smush_chars(cur, c, f->hardblank, f->layout);
					row[r][col] = m ? m : c;
				}
			}
		}
		if (start + gw > cur_w)
			cur_w = start + gw;

		for (int r = 0; r < H; r++)
			free(gr[r]);
		free(gr);
	}

	if (cur_w < 1)
		cur_w = 1;
	for (int r = 0; r < H; r++)
		row[r][cur_w] = '\0';
	*out_w = cur_w;
	return row;
}

struct flf_render *
flf_render_text(const struct flf_font *f, const char *text)
{
	int H = f->height, cur_w = 0;
	char **row = render_rows(f, text, &cur_w);
	if (!row)
		return NULL;

	struct flf_render *out = calloc(1, sizeof(*out));
	if (out) {
		out->rows = H;
		out->cols = cur_w;
		out->cell = calloc((size_t) H * cur_w, 1);
		if (!out->cell) {
			free(out);
			out = NULL;
		} else {
			for (int r = 0; r < H; r++)
				for (int x = 0; x < cur_w; x++) {
					char c = row[r][x];
					if (c != ' ' && c != '\0' && c != f->hardblank)
						out->cell[(size_t) r * cur_w + x] = 1;
				}
		}
	}
	for (int r = 0; r < H; r++)
		free(row[r]);
	free(row);
	return out;
}

/* Merged FIGlet art as one string, rows joined by '\n', no trailing blanks. */
char *
flf_render_string(const struct flf_font *f, const char *text)
{
	int H = f->height, w = 0;
	char **row = render_rows(f, text, &w);
	if (!row)
		return NULL;

	char *s = malloc((size_t) H * (w + 1) + 1);
	if (!s) {
		for (int r = 0; r < H; r++)
			free(row[r]);
		free(row);
		return NULL;
	}
	size_t o = 0;
	for (int r = 0; r < H; r++) {
		int end = w;
		while (end > 0 && (row[r][end - 1] == ' ' || row[r][end - 1] == f->hardblank))
			end--;
		for (int x = 0; x < end; x++)
			s[o++] = row[r][x] == f->hardblank ? ' ' : row[r][x];
		if (r < H - 1)
			s[o++] = '\n';
		free(row[r]);
	}
	s[o] = '\0';
	free(row);
	return s;
}

void
flf_render_free(struct flf_render *r)
{
	if (!r)
		return;
	free(r->cell);
	free(r);
}

void
flf_fill(struct flf_render *r)
{
	if (!r || r->rows < 1 || r->cols < 1)
		return;
	int R = r->rows, C = r->cols;
	size_t N = (size_t) R * C;

	uint8_t *outside = calloc(N, 1);
	if (!outside)
		return;

	/* BFS/DFS flood from every border non-ink cell, 4-connectivity */
	int *stack = malloc(N * sizeof(int));
	if (!stack) {
		free(outside);
		return;
	}
	int sp = 0;
	for (int y = 0; y < R; y++)
		for (int x = 0; x < C; x++) {
			if (y != 0 && y != R - 1 && x != 0 && x != C - 1)
				continue;
			int i = y * C + x;
			if (!r->cell[i] && !outside[i]) {
				outside[i] = 1;
				stack[sp++] = i;
			}
		}
	while (sp > 0) {
		int i = stack[--sp];
		int y = i / C, x = i % C;
		int nb[4] = {y > 0 ? i - C : -1, y < R - 1 ? i + C : -1, x > 0 ? i - 1 : -1, x < C - 1 ? i + 1 : -1};
		for (int k = 0; k < 4; k++) {
			int j = nb[k];
			if (j >= 0 && !r->cell[j] && !outside[j]) {
				outside[j] = 1;
				stack[sp++] = j;
			}
		}
	}

	/* any non-ink cell the flood didn't reach is an interior -> fill it */
	for (size_t i = 0; i < N; i++)
		if (!r->cell[i] && !outside[i])
			r->cell[i] = 1;

	free(stack);
	free(outside);
}
