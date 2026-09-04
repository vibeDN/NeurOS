/*
 * NeurOS compositor - the on-screen shell. Fork of cage; NeurOS-specific. MIT.
 *
 * Layout (docs/ui-mockup-v0.jpg):
 *   strip  : time/date/battery      (no frame; small mono text - TODO, needs fcft)
 *   top    : agent name             (framed; FIGlet block font)
 *   center : the embedded client    (framed)
 *   bottom : state / activity       (framed; FIGlet block font)
 *
 * FIGlet text is drawn as one scene rect per "ink" cell - no glyph rasteriser
 * needed for the block font.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <string.h>
#include <fcft/fcft.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "figlet.h"
#include "server.h"
#include "shell.h"
#include "textbuf.h"

/* glassmorphism palette (Claude Design pass, see docs/DESIGN-IMPL.md) */
static const float FRAME_COLOR[4] = {1.0f, 1.0f, 1.0f, 0.30f};    /* glass border */
static const float PANE_TINT[4] = {1.0f, 1.0f, 1.0f, 0.10f};      /* glass fill */
static const float TEXT_COLOR[4] = {0.992f, 0.984f, 0.973f, 1.0f}; /* #fdfbf8 */
static const float STRIP_COLOR[4] = {0.992f, 0.984f, 0.973f, 0.92f};

/* default wallpaper = Claude accent -> darker shade (#D97757 -> #4a2415) */
static const float DEFAULT_TOP[4] = {0.851f, 0.463f, 0.341f, 1.0f};
static const float DEFAULT_BOTTOM[4] = {0.290f, 0.141f, 0.082f, 1.0f};

static void
lerp4(float out[4], const float a[4], const float b[4], float t)
{
	for (int i = 0; i < 4; i++)
		out[i] = a[i] + (b[i] - a[i]) * t;
}

static void
place(struct wlr_scene_rect *rect, int x, int y, int w, int h)
{
	if (w < 1)
		w = 1;
	if (h < 1)
		h = 1;
	wlr_scene_rect_set_size(rect, w, h);
	wlr_scene_node_set_position(&rect->node, x, y);
}

/* -- framed pane (tint wash + 4 border edges) --------------------------- */

static void
frame_create(struct ng_frame *f, struct wlr_scene_tree *parent)
{
	f->tint = wlr_scene_rect_create(parent, 1, 1, PANE_TINT);
	for (int i = 0; i < 4; i++)
		f->edge[i] = wlr_scene_rect_create(parent, 1, 1, FRAME_COLOR);
}

static void
frame_place(struct ng_frame *f, const struct wlr_box *b, int t)
{
	place(f->tint, b->x, b->y, b->width, b->height);
	place(f->edge[0], b->x, b->y, b->width, t);                       /* top */
	place(f->edge[1], b->x, b->y + b->height - t, b->width, t);       /* bottom */
	place(f->edge[2], b->x, b->y, t, b->height);                      /* left */
	place(f->edge[3], b->x + b->width - t, b->y, t, b->height);       /* right */
}

/* -- big text: the Doto display font via fcft, sized to the pane --------- */

#define NG_BIG_FONT  "Doto:weight=210"
#define NG_MONO_FONT "JetBrains Mono"

static void
ng_shell_size_big_font(struct ng_shell *shell, int pane_h)
{
	int sz = pane_h * 46 / 100; /* single line, ~46% of the pane height */
	if (sz < 10)
		sz = 10;
	if (sz > 120)
		sz = 120;
	if (sz == shell->big_size && shell->big_font)
		return;

	if (shell->big_font)
		fcft_destroy(shell->big_font);
	char attr[48];
	snprintf(attr, sizeof(attr), "size=%d", sz);
	const char *names[] = {NG_BIG_FONT};
	shell->big_font = fcft_from_name(1, names, attr);
	shell->big_size = shell->big_font ? sz : 0;
}

/* uppercase ASCII copy (design: text-transform:uppercase) */
static char *
upper_dup(const char *s)
{
	if (!s)
		return NULL;
	char *o = strdup(s);
	if (o)
		for (char *p = o; *p; p++)
			if (*p >= 'a' && *p <= 'z')
				*p -= 32;
	return o;
}

static void
bigtext_render(struct ng_shell *shell, struct wlr_scene_buffer *node, const char *text, const struct wlr_box *box)
{
	if (!node)
		return;
	if (!shell->big_font || !text || !text[0] || box->width < 8 || box->height < 8) {
		wlr_scene_buffer_set_buffer(node, NULL);
		return;
	}

	char *up = upper_dup(text);
	int w = 0, h = 0;
	struct wlr_buffer *buf = ng_text_render(shell->big_font, up ? up : text, TEXT_COLOR, &w, &h);
	free(up);
	if (!buf || w < 1 || h < 1) {
		wlr_scene_buffer_set_buffer(node, NULL);
		return;
	}

	/* fit into 88% of the box, preserve aspect */
	int fitw = box->width * 88 / 100;
	int fith = box->height * 88 / 100;
	double sx = (double) fitw / w, sy = (double) fith / h;
	double s = sx < sy ? sx : sy;
	if (s > 1.0)
		s = 1.0;
	int dw = (int) (w * s), dh = (int) (h * s);
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;

	wlr_scene_buffer_set_buffer(node, buf);
	wlr_scene_buffer_set_dest_size(node, dw, dh);
	wlr_buffer_drop(buf);

	wlr_scene_node_set_position(&node->node, box->x + (box->width - dw) / 2, box->y + (box->height - dh) / 2);
}

/* -- shell -------------------------------------------------------------- */

struct ng_shell *
ng_shell_create(struct cg_server *server)
{
	struct ng_shell *shell = calloc(1, sizeof(*shell));
	if (!shell)
		return NULL;
	shell->server = server;

	shell->tree = wlr_scene_tree_create(&server->scene->tree);
	if (!shell->tree) {
		free(shell);
		return NULL;
	}

	shell->font = flf_load(NG_FONT_PATH);
	if (!shell->font)
		wlr_log(WLR_ERROR, "ng_shell: could not load %s (status text disabled)", NG_FONT_PATH);

	for (int i = 0; i < NG_GRADIENT_BANDS; i++)
		shell->band[i] = wlr_scene_rect_create(shell->tree, 1, 1, DEFAULT_TOP);

	frame_create(&shell->top_frame, shell->tree);
	frame_create(&shell->center_frame, shell->tree);
	frame_create(&shell->bottom_frame, shell->tree);

	static bool fcft_ready = false;
	if (!fcft_ready)
		fcft_ready = fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);

	const char *names[] = {NG_MONO_FONT};
	shell->strip_font = fcft_from_name(1, names, "size=13");
	if (!shell->strip_font)
		wlr_log(WLR_ERROR, "ng_shell: no monospace font (text disabled)");
	shell->big_font = NULL; /* sized in ng_shell_layout to the pane height */
	shell->big_size = 0;

	shell->agent_node = wlr_scene_buffer_create(shell->tree, NULL);
	shell->status_node = wlr_scene_buffer_create(shell->tree, NULL);
	shell->agent_text = strdup("NeurOS");
	shell->status_text = strdup("Idle");

	shell->strip_node = wlr_scene_buffer_create(shell->tree, NULL);
	shell->strip_right_node = wlr_scene_buffer_create(shell->tree, NULL);
	shell->activity_node = wlr_scene_buffer_create(shell->tree, NULL);

	ng_shell_set_colors(shell, DEFAULT_TOP, DEFAULT_BOTTOM);

	wlr_log(WLR_INFO, "ng_shell: created");
	return shell;
}

void
ng_shell_destroy(struct ng_shell *shell)
{
	if (!shell)
		return;
	free(shell->agent_text);
	free(shell->status_text);
	free(shell->strip_text);
	free(shell->strip_right_text);
	free(shell->activity_text);
	if (shell->strip_font)
		fcft_destroy(shell->strip_font);
	if (shell->big_font)
		fcft_destroy(shell->big_font);
	if (shell->font)
		flf_free(shell->font);
	if (shell->tree)
		wlr_scene_node_destroy(&shell->tree->node);
	free(shell);
}

void
ng_shell_set_colors(struct ng_shell *shell, const float top[4], const float bottom[4])
{
	memcpy(shell->top_color, top, sizeof(shell->top_color));
	memcpy(shell->bottom_color, bottom, sizeof(shell->bottom_color));
	for (int i = 0; i < NG_GRADIENT_BANDS; i++) {
		float t = (NG_GRADIENT_BANDS == 1) ? 0.0f : (float) i / (NG_GRADIENT_BANDS - 1);
		float c[4];
		lerp4(c, shell->top_color, shell->bottom_color, t);
		wlr_scene_rect_set_color(shell->band[i], c);
	}
}

void
ng_shell_set_agent(struct ng_shell *shell, const char *name)
{
	free(shell->agent_text);
	shell->agent_text = name ? strdup(name) : NULL;
	bigtext_render(shell, shell->agent_node, shell->agent_text, &shell->top_box);
}

void
ng_shell_set_status(struct ng_shell *shell, const char *state)
{
	free(shell->status_text);
	shell->status_text = state ? strdup(state) : NULL;
	bigtext_render(shell, shell->status_node, shell->status_text, &shell->bottom_box);
}

/* left-align the strip buffer, vertically centred in strip_box */
static void
strip_reposition(struct ng_shell *shell)
{
	if (!shell->strip_node || !shell->strip_node->buffer)
		return;
	int bh = shell->strip_node->buffer->height;
	int y = shell->strip_box.y + (shell->strip_box.height - bh) / 2;
	wlr_scene_node_set_position(&shell->strip_node->node, shell->strip_box.x, y < shell->strip_box.y ? shell->strip_box.y : y);
}

void
ng_shell_set_strip(struct ng_shell *shell, const char *text)
{
	free(shell->strip_text);
	shell->strip_text = text ? strdup(text) : NULL;

	if (!shell->strip_node)
		return;
	if (!shell->strip_font || !shell->strip_text || !shell->strip_text[0]) {
		wlr_scene_buffer_set_buffer(shell->strip_node, NULL);
		return;
	}

	struct wlr_buffer *buf = ng_text_render(shell->strip_font, shell->strip_text, STRIP_COLOR, NULL, NULL);
	wlr_scene_buffer_set_buffer(shell->strip_node, buf);
	if (buf)
		wlr_buffer_drop(buf);
	strip_reposition(shell);
}

static void
strip_right_reposition(struct ng_shell *shell)
{
	if (!shell->strip_right_node || !shell->strip_right_node->buffer)
		return;
	int bw = shell->strip_right_node->buffer->width;
	int bh = shell->strip_right_node->buffer->height;
	int x = shell->strip_box.x + shell->strip_box.width - bw;
	int y = shell->strip_box.y + (shell->strip_box.height - bh) / 2;
	wlr_scene_node_set_position(&shell->strip_right_node->node, x, y < shell->strip_box.y ? shell->strip_box.y : y);
}

void
ng_shell_set_strip_right(struct ng_shell *shell, const char *text)
{
	free(shell->strip_right_text);
	shell->strip_right_text = text ? strdup(text) : NULL;

	if (!shell->strip_right_node)
		return;
	if (!shell->strip_font || !shell->strip_right_text || !shell->strip_right_text[0]) {
		wlr_scene_buffer_set_buffer(shell->strip_right_node, NULL);
		return;
	}
	struct wlr_buffer *buf = ng_text_render(shell->strip_font, shell->strip_right_text, STRIP_COLOR, NULL, NULL);
	wlr_scene_buffer_set_buffer(shell->strip_right_node, buf);
	if (buf)
		wlr_buffer_drop(buf);
	strip_right_reposition(shell);
}

/* centre the activity sub-line horizontally, just below the bottom pane's box */
static void
activity_reposition(struct ng_shell *shell)
{
	if (!shell->activity_node || !shell->activity_node->buffer)
		return;
	int bw = shell->activity_node->buffer->width;
	int bh = shell->activity_node->buffer->height;
	int x = shell->bottom_box.x + (shell->bottom_box.width - bw) / 2;
	/* centre it in the lower 28% band reserved in ng_shell_layout */
	int band_top = shell->bottom_box.y + shell->bottom_box.height * 72 / 100;
	int band_h = shell->bottom_box.y + shell->bottom_box.height - shell->frame_t - band_top;
	int y = band_top + (band_h - bh) / 2;
	wlr_scene_node_set_position(&shell->activity_node->node, x, y);
}

void
ng_shell_set_activity(struct ng_shell *shell, const char *text)
{
	free(shell->activity_text);
	shell->activity_text = text ? strdup(text) : NULL;

	if (!shell->activity_node)
		return;
	if (!shell->strip_font || !shell->activity_text || !shell->activity_text[0]) {
		wlr_scene_buffer_set_buffer(shell->activity_node, NULL);
		return;
	}
	struct wlr_buffer *buf = ng_text_render(shell->strip_font, shell->activity_text, STRIP_COLOR, NULL, NULL);
	wlr_scene_buffer_set_buffer(shell->activity_node, buf);
	if (buf)
		wlr_buffer_drop(buf);
	activity_reposition(shell);
}

void
ng_shell_layout(struct ng_shell *shell, int width, int height)
{
	shell->width = width;
	shell->height = height;

	int band_h = (height + NG_GRADIENT_BANDS - 1) / NG_GRADIENT_BANDS;
	for (int i = 0; i < NG_GRADIENT_BANDS; i++)
		place(shell->band[i], 0, i * band_h, width, band_h);

	int margin = height / 50;
	if (margin < 6)
		margin = 6;
	int gap = margin;

	shell->frame_t = height / 80;
	if (shell->frame_t < 6)
		shell->frame_t = 6;
	if (shell->frame_t > 26)
		shell->frame_t = 26;

	int strip_h = height * 4 / 100;
	if (strip_h < 22)
		strip_h = 22;
	int top_h = height * 19 / 100;
	int bottom_h = height * 19 / 100;

	int x = margin;
	int w = width - 2 * margin;
	int y = margin;

	shell->strip_box = (struct wlr_box){x, y, w, strip_h};
	y += strip_h + gap;

	shell->top_box = (struct wlr_box){x, y, w, top_h};
	y += top_h + gap;

	int center_y = y;
	int center_h = height - margin - bottom_h - gap - center_y;
	if (center_h < 1)
		center_h = 1;
	shell->center_box = (struct wlr_box){x, center_y, w, center_h};
	shell->bottom_box = (struct wlr_box){x, height - margin - bottom_h, w, bottom_h};

	frame_place(&shell->top_frame, &shell->top_box, shell->frame_t);
	frame_place(&shell->center_frame, &shell->center_box, shell->frame_t);
	frame_place(&shell->bottom_frame, &shell->bottom_box, shell->frame_t);

	/* leave the lower ~28% of the bottom pane for the activity sub-line */
	struct wlr_box status_box = shell->bottom_box;
	status_box.height = status_box.height * 72 / 100;

	int pane_h = shell->top_box.height < status_box.height ? shell->top_box.height : status_box.height;
	ng_shell_size_big_font(shell, pane_h);

	bigtext_render(shell, shell->agent_node, shell->agent_text, &shell->top_box);
	bigtext_render(shell, shell->status_node, shell->status_text, &status_box);
	strip_reposition(shell);
	strip_right_reposition(shell);
	activity_reposition(shell);

	wlr_log(WLR_INFO, "ng_shell: layout %dx%d, centre pane %d,%d %dx%d", width, height, shell->center_box.x,
		shell->center_box.y, shell->center_box.width, shell->center_box.height);
}
