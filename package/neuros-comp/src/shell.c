/*
 * NeurOS compositor - the on-screen shell (4-zone layout + gradient wallpaper).
 *
 * Fork of cage; this file is NeurOS-specific. MIT.
 *
 * Layout (see docs/ui-mockup-v0.jpg):
 *
 *   +---------------------------------+  strip  : time/date/battery (no frame)
 *   | [ agent-name / model         ] |  top    : framed
 *   | [                            ] |  center : framed, holds the client
 *   | [                            ] |
 *   | [ state / activity           ] |  bottom : framed
 *   +---------------------------------+
 *
 * Text is not drawn yet (needs a rasterizer + the .flf parser) - M2 step 2.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdlib.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/util/log.h>

#include "server.h"
#include "shell.h"

/* near-black frame chrome */
static const float FRAME_COLOR[4] = {0.04f, 0.04f, 0.04f, 1.0f};
/* subtle backdrop behind the embedded client */
static const float CENTER_BG[4] = {0.0f, 0.0f, 0.0f, 0.28f};

/* default wallpaper = Claude accent -> darker shade */
static const float DEFAULT_TOP[4] = {0.851f, 0.463f, 0.341f, 1.0f};    /* #D97757 */
static const float DEFAULT_BOTTOM[4] = {0.361f, 0.169f, 0.110f, 1.0f}; /* ~40% of it */

static void
lerp4(float out[4], const float a[4], const float b[4], float t)
{
	for (int i = 0; i < 4; i++)
		out[i] = a[i] + (b[i] - a[i]) * t;
}

struct ng_shell *
ng_shell_create(struct cg_server *server)
{
	struct ng_shell *shell = calloc(1, sizeof(*shell));
	if (!shell)
		return NULL;
	shell->server = server;

	/* chrome tree, created first so it sits *below* client views which are
	 * added to server->scene->tree afterwards */
	shell->tree = wlr_scene_tree_create(&server->scene->tree);
	if (!shell->tree) {
		free(shell);
		return NULL;
	}

	for (int i = 0; i < NG_GRADIENT_BANDS; i++) {
		shell->band[i] = wlr_scene_rect_create(shell->tree, 1, 1, DEFAULT_TOP);
	}
	shell->top_frame = wlr_scene_rect_create(shell->tree, 1, 1, FRAME_COLOR);
	shell->center_frame = wlr_scene_rect_create(shell->tree, 1, 1, FRAME_COLOR);
	shell->bottom_frame = wlr_scene_rect_create(shell->tree, 1, 1, FRAME_COLOR);
	shell->center_inner = wlr_scene_rect_create(shell->tree, 1, 1, CENTER_BG);

	ng_shell_set_colors(shell, DEFAULT_TOP, DEFAULT_BOTTOM);

	wlr_log(WLR_INFO, "ng_shell: created");
	return shell;
}

void
ng_shell_destroy(struct ng_shell *shell)
{
	if (!shell)
		return;
	if (shell->tree)
		wlr_scene_node_destroy(&shell->tree->node);
	free(shell);
}

void
ng_shell_set_colors(struct ng_shell *shell, const float top[4], const float bottom[4])
{
	for (int i = 0; i < 4; i++) {
		shell->top_color[i] = top[i];
		shell->bottom_color[i] = bottom[i];
	}
	for (int i = 0; i < NG_GRADIENT_BANDS; i++) {
		float t = (NG_GRADIENT_BANDS == 1) ? 0.0f : (float) i / (NG_GRADIENT_BANDS - 1);
		float c[4];
		lerp4(c, shell->top_color, shell->bottom_color, t);
		wlr_scene_rect_set_color(shell->band[i], c);
	}
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

void
ng_shell_layout(struct ng_shell *shell, int width, int height)
{
	shell->width = width;
	shell->height = height;

	/* wallpaper bands cover the whole output */
	int band_h = (height + NG_GRADIENT_BANDS - 1) / NG_GRADIENT_BANDS;
	for (int i = 0; i < NG_GRADIENT_BANDS; i++)
		place(shell->band[i], 0, i * band_h, width, band_h);

	/* proportions - tuned on the mockup, clamped for tiny/huge outputs */
	int margin = height / 50;
	if (margin < 6)
		margin = 6;
	int gap = margin;

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

	int bottom_y = height - margin - bottom_h;
	shell->bottom_box = (struct wlr_box){x, bottom_y, w, bottom_h};

	/* frames: a filled rect the size of the zone; the client / future text
	 * is inset by NG_FRAME_PX so the rect reads as a border */
	place(shell->top_frame, shell->top_box.x, shell->top_box.y, shell->top_box.width, shell->top_box.height);
	place(shell->center_frame, shell->center_box.x, shell->center_box.y, shell->center_box.width,
	      shell->center_box.height);
	place(shell->bottom_frame, shell->bottom_box.x, shell->bottom_box.y, shell->bottom_box.width,
	      shell->bottom_box.height);

	place(shell->center_inner, shell->center_box.x + NG_FRAME_PX, shell->center_box.y + NG_FRAME_PX,
	      shell->center_box.width - 2 * NG_FRAME_PX, shell->center_box.height - 2 * NG_FRAME_PX);

	wlr_log(WLR_INFO, "ng_shell: layout %dx%d, center pane %d,%d %dx%d", width, height, shell->center_box.x,
		shell->center_box.y, shell->center_box.width, shell->center_box.height);
}
