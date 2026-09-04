/*
 * NeurOS compositor - the on-screen shell. Fork of cage; NeurOS-specific. MIT.
 *
 * iOS-style glassmorphism (Claude Design pass, docs/DESIGN-IMPL.md):
 *   strip     time/date left, battery right - JetBrains Mono, near-white
 *   dots      6-agent scaffold row (v1: only the active one lit)
 *   top panel  glass - agent name (Doto) + model pill
 *   centre     glass (dark) - the embedded client
 *   bottom     glass - status word (Doto) + "using <tool>"
 *   home       a short rounded bar in the accent colour
 * Wallpaper: a per-agent vertical 2-stop gradient.
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

#define NG_BIG_FONT  "Doto:weight=210"
#define NG_MONO_FONT "JetBrains Mono"

/* text colours are straight alpha (ng_text_render builds a pixman fill) */
static const float TEXT_COLOR[4] = {0.992f, 0.984f, 0.973f, 1.0f};  /* #fdfbf8 */
static const float DIM_COLOR[4] = {0.992f, 0.984f, 0.973f, 0.66f};
static const float PILL_COLOR[4] = {1.0f, 1.0f, 1.0f, 0.14f};

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

/* attach `buf` (may be NULL) to `node`, drop our ref, position top-left */
static void
node_set(struct wlr_scene_buffer *node, struct wlr_buffer *buf, int x, int y)
{
	if (!node)
		return;
	wlr_scene_buffer_set_buffer(node, buf);
	if (buf)
		wlr_buffer_drop(buf);
	wlr_scene_buffer_set_dest_size(node, buf ? node->buffer->width : 0, buf ? node->buffer->height : 0);
	wlr_scene_node_set_position(&node->node, x, y);
}

/* -- big text: the Doto display font, sized to the pane ----------------- */

static void
ng_shell_size_big_font(struct ng_shell *shell, int pane_h)
{
	int sz = pane_h * 42 / 100;
	if (sz < 10)
		sz = 10;
	if (sz > 130)
		sz = 130;
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

/* render `text` in Doto, fit to `box` (~86%), centre; anchor `cy` overrides the
 * vertical centre when >= 0 */
static void
bigtext_render(struct ng_shell *shell, struct wlr_scene_buffer *node, const char *text, const struct wlr_box *box,
	       int cy)
{
	if (!node)
		return;
	if (!shell->big_font || !text || !text[0] || box->width < 8 || box->height < 8) {
		node_set(node, NULL, 0, 0);
		return;
	}
	char *up = upper_dup(text);
	int w = 0, h = 0;
	struct wlr_buffer *buf = ng_text_render(shell->big_font, up ? up : text, TEXT_COLOR, &w, &h);
	free(up);
	if (!buf || w < 1 || h < 1) {
		node_set(node, NULL, 0, 0);
		return;
	}
	int fitw = box->width * 86 / 100, fith = box->height * 86 / 100;
	double s = (double) fitw / w < (double) fith / h ? (double) fitw / w : (double) fith / h;
	if (s > 1.0)
		s = 1.0;
	int dw = (int) (w * s), dh = (int) (h * s);
	if (dw < 1)
		dw = 1;
	if (dh < 1)
		dh = 1;
	int x = box->x + (box->width - dw) / 2;
	int y = (cy >= 0 ? cy : box->y + box->height / 2) - dh / 2;
	wlr_scene_buffer_set_buffer(node, buf);
	wlr_scene_buffer_set_dest_size(node, dw, dh);
	wlr_buffer_drop(buf);
	wlr_scene_node_set_position(&node->node, x, y);
}

/* -- small mono text (strip / model / activity) ------------------------ */

static void
monotext(struct ng_shell *shell, struct wlr_scene_buffer *node, const char *text, const float color[4])
{
	if (!node)
		return;
	if (!shell->strip_font || !text || !text[0]) {
		node_set(node, NULL, 0, 0);
		return;
	}
	struct wlr_buffer *buf = ng_text_render(shell->strip_font, text, color, NULL, NULL);
	node_set(node, buf, node->node.x, node->node.y);
}

/* -- shell -------------------------------------------------------------- */

struct ng_shell *
ng_shell_create(struct cg_server *server)
{
	struct ng_shell *shell = calloc(1, sizeof(*shell));
	if (!shell)
		return NULL;
	shell->server = server;
	shell->active_dot = 0;

	shell->tree = wlr_scene_tree_create(&server->scene->tree);
	if (!shell->tree) {
		free(shell);
		return NULL;
	}

	for (int i = 0; i < NG_GRADIENT_BANDS; i++)
		shell->band[i] = wlr_scene_rect_create(shell->tree, 1, 1, DEFAULT_TOP);

	shell->top_panel = wlr_scene_buffer_create(shell->tree, NULL);
	shell->center_panel = wlr_scene_buffer_create(shell->tree, NULL);
	shell->bottom_panel = wlr_scene_buffer_create(shell->tree, NULL);

	static bool fcft_ready = false;
	if (!fcft_ready)
		fcft_ready = fcft_init(FCFT_LOG_COLORIZE_NEVER, false, FCFT_LOG_CLASS_ERROR);
	const char *mono[] = {NG_MONO_FONT};
	shell->strip_font = fcft_from_name(1, mono, "size=13");
	if (!shell->strip_font)
		wlr_log(WLR_ERROR, "ng_shell: no %s", NG_MONO_FONT);

	shell->agent_node = wlr_scene_buffer_create(shell->tree, NULL);
	shell->status_node = wlr_scene_buffer_create(shell->tree, NULL);
	shell->model_node = wlr_scene_buffer_create(shell->tree, NULL);
	shell->strip_node = wlr_scene_buffer_create(shell->tree, NULL);
	shell->strip_right_node = wlr_scene_buffer_create(shell->tree, NULL);
	shell->activity_node = wlr_scene_buffer_create(shell->tree, NULL);
	for (int i = 0; i < 6; i++)
		shell->dot[i] = wlr_scene_buffer_create(shell->tree, NULL);
	shell->home_node = wlr_scene_buffer_create(shell->tree, NULL);

	shell->agent_text = strdup("NeurOS");
	shell->status_text = strdup("Idle");
	shell->model_text = NULL;

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
	free(shell->model_text);
	free(shell->strip_text);
	free(shell->strip_right_text);
	free(shell->activity_text);
	if (shell->strip_font)
		fcft_destroy(shell->strip_font);
	if (shell->big_font)
		fcft_destroy(shell->big_font);
	if (shell->tree)
		wlr_scene_node_destroy(&shell->tree->node);
	free(shell);
}

/* 6 agent brand colours, straight alpha */
static const float DOT_COLORS[6][4] = {
	{0.851f, 0.463f, 0.341f, 1.0f}, /* claude   #D97757 */
	{0.671f, 0.408f, 1.0f, 1.0f},   /* chatgpt  #AB68FF */
	{0.259f, 0.522f, 0.957f, 1.0f}, /* gemini   #4285F4 */
	{0.102f, 0.102f, 0.102f, 1.0f}, /* kimi     #1A1A1A */
	{0.302f, 0.420f, 0.996f, 1.0f}, /* deepseek #4D6BFE */
	{0.482f, 0.380f, 1.0f, 1.0f},   /* qwen     #7B61FF */
};
static const float DOT_RING[4] = {1.0f, 1.0f, 1.0f, 0.6f};

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
	/* light the matching scaffold dot */
	static const char *keys[6] = {"claude", "chatgpt", "gemini", "kimi", "deepseek", "qwen"};
	if (name)
		for (int i = 0; i < 6; i++) {
			char lc[32];
			size_t n = 0;
			for (; name[n] && n < sizeof(lc) - 1; n++)
				lc[n] = (name[n] >= 'A' && name[n] <= 'Z') ? name[n] + 32 : name[n];
			lc[n] = 0;
			if (strcmp(lc, keys[i]) == 0)
				shell->active_dot = i;
		}
	ng_shell_layout(shell, shell->width, shell->height);
}

void
ng_shell_set_status(struct ng_shell *shell, const char *state)
{
	free(shell->status_text);
	shell->status_text = state ? strdup(state) : NULL;
	struct wlr_box b = shell->bottom_box;
	b.height = b.height * 66 / 100;
	ng_shell_size_big_font(shell, b.height);
	bigtext_render(shell, shell->status_node, shell->status_text, &b, -1);
}

void
ng_shell_set_model(struct ng_shell *shell, const char *model)
{
	free(shell->model_text);
	shell->model_text = model ? strdup(model) : NULL;
	ng_shell_layout(shell, shell->width, shell->height);
}

void
ng_shell_set_strip(struct ng_shell *shell, const char *text)
{
	free(shell->strip_text);
	shell->strip_text = text ? strdup(text) : NULL;
	monotext(shell, shell->strip_node, shell->strip_text, TEXT_COLOR);
	if (shell->strip_node->buffer)
		wlr_scene_node_set_position(&shell->strip_node->node, shell->strip_box.x,
					   shell->strip_box.y +
						   (shell->strip_box.height - shell->strip_node->buffer->height) / 2);
}

void
ng_shell_set_strip_right(struct ng_shell *shell, const char *text)
{
	free(shell->strip_right_text);
	shell->strip_right_text = text ? strdup(text) : NULL;
	monotext(shell, shell->strip_right_node, shell->strip_right_text, DIM_COLOR);
	if (shell->strip_right_node->buffer)
		wlr_scene_node_set_position(
			&shell->strip_right_node->node,
			shell->strip_box.x + shell->strip_box.width - shell->strip_right_node->buffer->width,
			shell->strip_box.y +
				(shell->strip_box.height - shell->strip_right_node->buffer->height) / 2);
}

void
ng_shell_set_activity(struct ng_shell *shell, const char *text)
{
	free(shell->activity_text);
	shell->activity_text = text ? strdup(text) : NULL;
	char buf[128];
	if (text && text[0]) {
		snprintf(buf, sizeof(buf), "using %s", text);
		monotext(shell, shell->activity_node, buf, DIM_COLOR);
	} else {
		monotext(shell, shell->activity_node, NULL, DIM_COLOR);
	}
	if (shell->activity_node->buffer) {
		int bw = shell->activity_node->buffer->width;
		int bh = shell->activity_node->buffer->height;
		wlr_scene_node_set_position(&shell->activity_node->node,
					   shell->bottom_box.x + (shell->bottom_box.width - bw) / 2,
					   shell->bottom_box.y + shell->bottom_box.height - bh -
						   shell->bottom_box.height / 12);
	}
}

void
ng_shell_layout(struct ng_shell *shell, int width, int height)
{
	if (width < 16 || height < 16)
		return;
	shell->width = width;
	shell->height = height;

	int band_h = (height + NG_GRADIENT_BANDS - 1) / NG_GRADIENT_BANDS;
	for (int i = 0; i < NG_GRADIENT_BANDS; i++)
		place(shell->band[i], 0, i * band_h, width, band_h);

	int margin = height / 46;
	int gap = height / 60;
	int rad = width / 20;
	if (rad > 34)
		rad = 34;

	int strip_h = height * 34 / 1000;
	if (strip_h < 20)
		strip_h = 20;
	int dots_h = height / 44;
	int pane_h = height * 175 / 1000;

	int x = margin, w = width - 2 * margin, y = margin;

	shell->strip_box = (struct wlr_box){x + rad / 2, y, w - rad, strip_h};
	y += strip_h + gap / 2;

	/* dots row */
	int dot_d = dots_h * 6 / 10;
	if (dot_d < 6)
		dot_d = 6;
	int dot_gap = dot_d;
	int dots_w = 6 * dot_d + 5 * dot_gap;
	int dx = (width - dots_w) / 2;
	int dy = y + (dots_h - dot_d) / 2;
	for (int i = 0; i < 6; i++) {
		struct wlr_buffer *b = ng_dot_render(dot_d, DOT_COLORS[i], i == shell->active_dot ? 2 : 0, DOT_RING);
		node_set(shell->dot[i], b, dx + i * (dot_d + dot_gap), dy);
		wlr_scene_buffer_set_opacity(shell->dot[i], i == shell->active_dot ? 1.0f : 0.34f);
	}
	y += dots_h + gap / 2;

	shell->top_box = (struct wlr_box){x, y, w, pane_h};
	y += pane_h + gap;

	int bottom_y = height - margin - pane_h - height / 40; /* leave room for home bar */
	int center_y = y;
	int center_h = bottom_y - gap - center_y;
	if (center_h < 40)
		center_h = 40;
	shell->center_box = (struct wlr_box){x, center_y, w, center_h};
	shell->bottom_box = (struct wlr_box){x, bottom_y, w, pane_h};

	node_set(shell->top_panel, ng_panel_render(shell->top_box.width, shell->top_box.height, rad, 0),
		 shell->top_box.x, shell->top_box.y);
	node_set(shell->center_panel, ng_panel_render(shell->center_box.width, shell->center_box.height, rad, 1),
		 shell->center_box.x, shell->center_box.y);
	node_set(shell->bottom_panel, ng_panel_render(shell->bottom_box.width, shell->bottom_box.height, rad, 0),
		 shell->bottom_box.x, shell->bottom_box.y);

	/* top pane: agent name, with the model line tucked under it when set */
	int has_model = shell->model_text && shell->model_text[0];
	struct wlr_box name_box = shell->top_box;
	if (has_model)
		name_box.height = name_box.height * 68 / 100;
	ng_shell_size_big_font(shell, name_box.height);
	bigtext_render(shell, shell->agent_node, shell->agent_text, &name_box, -1);

	if (has_model) {
		struct wlr_buffer *tb = ng_pill_text_render(shell->strip_font, shell->model_text, TEXT_COLOR,
							   PILL_COLOR, strip_h / 2, strip_h / 4);
		int px = shell->top_box.x + (shell->top_box.width - (tb ? tb->width : 0)) / 2;
		int py = shell->top_box.y + shell->top_box.height - (tb ? tb->height : 0) - shell->top_box.height / 10;
		node_set(shell->model_node, tb, px, py);
	} else {
		node_set(shell->model_node, NULL, 0, 0);
	}

	struct wlr_box status_box = shell->bottom_box;
	status_box.height = status_box.height * 66 / 100;
	ng_shell_size_big_font(shell, status_box.height);
	bigtext_render(shell, shell->status_node, shell->status_text, &status_box, -1);

	/* home indicator */
	int hb_w = width / 6, hb_h = height / 180;
	if (hb_h < 3)
		hb_h = 3;
	float home_col[4] = {shell->top_color[0], shell->top_color[1], shell->top_color[2], 0.55f};
	node_set(shell->home_node, ng_pill_render(hb_w, hb_h, hb_h / 2, home_col), (width - hb_w) / 2,
		 height - margin / 2 - hb_h);

	/* reposition the small texts for the new boxes */
	ng_shell_set_strip(shell, shell->strip_text);
	ng_shell_set_strip_right(shell, shell->strip_right_text);
	ng_shell_set_activity(shell, shell->activity_text);
}
