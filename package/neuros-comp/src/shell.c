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
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "figlet.h"
#include "server.h"
#include "shell.h"

static const float FRAME_COLOR[4] = {0.04f, 0.04f, 0.04f, 1.0f};  /* border */
static const float PANE_TINT[4] = {0.0f, 0.0f, 0.0f, 0.22f};      /* wash */
static const float TEXT_COLOR[4] = {0.05f, 0.05f, 0.05f, 1.0f};

/* default wallpaper = Claude accent -> darker shade */
static const float DEFAULT_TOP[4] = {0.851f, 0.463f, 0.341f, 1.0f};
static const float DEFAULT_BOTTOM[4] = {0.361f, 0.169f, 0.110f, 1.0f};

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

/* -- text block ----------------------------------------------------------- */

static void
textblock_clear(struct ng_textblock *tb)
{
	for (int i = 0; i < tb->n_cell; i++)
		if (tb->cell[i])
			wlr_scene_node_destroy(&tb->cell[i]->node);
	free(tb->cell);
	tb->cell = NULL;
	tb->n_cell = 0;
}

static void
textblock_init(struct ng_textblock *tb, struct wlr_scene_tree *parent, const float color[4])
{
	tb->tree = wlr_scene_tree_create(parent);
	memcpy(tb->color, color, sizeof(tb->color));
	tb->cell = NULL;
	tb->n_cell = 0;
	tb->text = NULL;
}

/* (re)render tb->text with `font`, fitted and centred inside `box`. */
static void
textblock_render(struct ng_textblock *tb, const struct flf_font *font, const struct wlr_box *box)
{
	textblock_clear(tb);
	if (!font || !tb->text || !tb->text[0] || box->width < 4 || box->height < 4)
		return;

	struct flf_render *r = flf_render_text(font, tb->text);
	if (!r)
		return;

	/* cell size: fit the rendered grid into the box (~78%), keep square cells */
	int cw = (box->width * 78 / 100) / (r->cols > 0 ? r->cols : 1);
	int ch = (box->height * 78 / 100) / (r->rows > 0 ? r->rows : 1);
	int cell = cw < ch ? cw : ch;
	if (cell < 1)
		cell = 1;

	int total_w = cell * r->cols;
	int total_h = cell * r->rows;
	int ox = box->x + (box->width - total_w) / 2;
	int oy = box->y + (box->height - total_h) / 2;

	int ink = 0;
	for (int i = 0; i < r->rows * r->cols; i++)
		ink += r->cell[i] ? 1 : 0;

	tb->cell = calloc(ink > 0 ? ink : 1, sizeof(*tb->cell));
	if (!tb->cell) {
		flf_render_free(r);
		return;
	}

	int n = 0;
	for (int y = 0; y < r->rows; y++) {
		for (int x = 0; x < r->cols; x++) {
			if (!r->cell[y * r->cols + x])
				continue;
			struct wlr_scene_rect *px = wlr_scene_rect_create(tb->tree, cell, cell, tb->color);
			if (!px)
				continue;
			wlr_scene_node_set_position(&px->node, ox + x * cell, oy + y * cell);
			tb->cell[n++] = px;
		}
	}
	tb->n_cell = n;
	flf_render_free(r);
}

static void
textblock_set_text(struct ng_textblock *tb, const char *text)
{
	free(tb->text);
	tb->text = text ? strdup(text) : NULL;
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

	textblock_init(&shell->agent, shell->tree, TEXT_COLOR);
	textblock_init(&shell->status, shell->tree, TEXT_COLOR);
	textblock_set_text(&shell->agent, "NeurOS");
	textblock_set_text(&shell->status, "Idle");

	ng_shell_set_colors(shell, DEFAULT_TOP, DEFAULT_BOTTOM);

	wlr_log(WLR_INFO, "ng_shell: created");
	return shell;
}

void
ng_shell_destroy(struct ng_shell *shell)
{
	if (!shell)
		return;
	textblock_clear(&shell->agent);
	textblock_clear(&shell->status);
	free(shell->agent.text);
	free(shell->status.text);
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
	textblock_set_text(&shell->agent, name);
	textblock_render(&shell->agent, shell->font, &shell->top_box);
}

void
ng_shell_set_status(struct ng_shell *shell, const char *state)
{
	textblock_set_text(&shell->status, state);
	textblock_render(&shell->status, shell->font, &shell->bottom_box);
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

	int strip_h = height * 3 / 100;
	if (strip_h < 16)
		strip_h = 16;
	int top_h = height * 15 / 100;
	int bottom_h = height * 15 / 100;

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

	textblock_render(&shell->agent, shell->font, &shell->top_box);
	textblock_render(&shell->status, shell->font, &shell->bottom_box);

	wlr_log(WLR_INFO, "ng_shell: layout %dx%d, centre pane %d,%d %dx%d", width, height, shell->center_box.x,
		shell->center_box.y, shell->center_box.width, shell->center_box.height);
}
