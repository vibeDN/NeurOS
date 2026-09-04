/*
 * NeurOS on-screen keyboard. See osk.h. MIT.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <drm_fourcc.h>
#include <fcft/fcft.h>
#include <linux/input-event-codes.h>
#include <pixman.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>

#include "osk.h"
#include "seat.h"
#include "server.h"
#include "textbuf.h"

#define OSK_MONO "JetBrains Mono"

enum kk { KK_CHAR, KK_SHIFT, KK_LAYER, KK_BKSP, KK_ENTER, KK_SPACE, KK_HIDE };

struct key {
	const char *lbl;
	enum kk kind;
	uint16_t code; /* evdev keycode for KK_CHAR */
	bool shift;    /* KK_CHAR: needs the shift modifier */
	float units;   /* width, in key-units */
	struct wlr_box box; /* screen coords, filled by layout */
};

#define K(l, c) {(l), KK_CHAR, (c), false, 1.0f, {0}}
#define KS(l, c) {(l), KK_CHAR, (c), true, 1.0f, {0}}

/* two layers, four rows each; row terminated by a {0}-label sentinel */
static struct key g_layer0[4][14] = {
	{K("q", KEY_Q), K("w", KEY_W), K("e", KEY_E), K("r", KEY_R), K("t", KEY_T), K("y", KEY_Y), K("u", KEY_U),
	 K("i", KEY_I), K("o", KEY_O), K("p", KEY_P), {0}},
	{K("a", KEY_A), K("s", KEY_S), K("d", KEY_D), K("f", KEY_F), K("g", KEY_G), K("h", KEY_H), K("j", KEY_J),
	 K("k", KEY_K), K("l", KEY_L), {0}},
	{{"shift", KK_SHIFT, 0, false, 1.5f, {0}}, K("z", KEY_Z), K("x", KEY_X), K("c", KEY_C), K("v", KEY_V),
	 K("b", KEY_B), K("n", KEY_N), K("m", KEY_M), {"del", KK_BKSP, 0, false, 1.5f, {0}}, {0}},
	{{"?123", KK_LAYER, 0, false, 1.6f, {0}}, K(",", KEY_COMMA), {"space", KK_SPACE, 0, false, 4.3f, {0}},
	 K(".", KEY_DOT), {"ret", KK_ENTER, 0, false, 1.6f, {0}}, {"v", KK_HIDE, 0, false, 1.0f, {0}}, {0}},
};

static struct key g_layer1[4][14] = {
	{K("1", KEY_1), K("2", KEY_2), K("3", KEY_3), K("4", KEY_4), K("5", KEY_5), K("6", KEY_6), K("7", KEY_7),
	 K("8", KEY_8), K("9", KEY_9), K("0", KEY_0), {0}},
	{KS("@", KEY_2), KS("#", KEY_3), KS("$", KEY_4), KS("_", KEY_MINUS), KS("&", KEY_7), K("-", KEY_MINUS),
	 KS("+", KEY_EQUAL), KS("(", KEY_9), KS(")", KEY_0), K("/", KEY_SLASH), {0}},
	{{"=\\<", KK_LAYER, 0, false, 1.5f, {0}}, KS("*", KEY_8), KS("\"", KEY_APOSTROPHE), K("'", KEY_APOSTROPHE),
	 KS(":", KEY_SEMICOLON), K(";", KEY_SEMICOLON), KS("!", KEY_1), KS("?", KEY_SLASH), K("\\", KEY_BACKSLASH),
	 {"del", KK_BKSP, 0, false, 1.5f, {0}}, {0}},
	{{"ABC", KK_LAYER, 0, false, 1.6f, {0}}, K(",", KEY_COMMA), {"space", KK_SPACE, 0, false, 4.3f, {0}},
	 K(".", KEY_DOT), {"ret", KK_ENTER, 0, false, 1.6f, {0}}, {"v", KK_HIDE, 0, false, 1.0f, {0}}, {0}},
};

struct ng_osk {
	struct cg_server *server;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *node;

	struct wlr_keyboard kb; /* synthetic - so it works with no physical kb */
	bool kb_ready;

	int w, h;      /* screen */
	struct wlr_box area; /* keyboard rect */
	int layer;     /* 0 letters, 1 symbols */
	bool shift;    /* one-shot */
	bool visible;
};

static const struct wlr_keyboard_impl kb_impl = {.name = "neuros-osk"};

static uint32_t
now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (uint32_t) (t.tv_sec * 1000 + t.tv_nsec / 1000000);
}

static struct key (*cur_rows(struct ng_osk *osk))[14]
{
	return osk->layer == 0 ? g_layer0 : g_layer1;
}

/* -- key injection --------------------------------------------------- */

/* one key transition: update the synthetic keyboard's xkb state, then forward
 * the raw key + the resulting modifier state to the focused client */
static void
kb_key(struct ng_osk *osk, uint32_t code, bool pressed)
{
	uint32_t t = now_ms();
	enum wl_keyboard_key_state st = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;
	struct wlr_keyboard_key_event ev = {.time_msec = t, .keycode = code, .update_state = true, .state = st};
	wlr_keyboard_notify_key(&osk->kb, &ev);

	struct wlr_seat *seat = osk->server->seat->seat;
	wlr_seat_keyboard_notify_key(seat, t, code, st);
	wlr_seat_keyboard_notify_modifiers(seat, &osk->kb.modifiers);
}

static void
osk_send(struct ng_osk *osk, uint16_t code, bool shift)
{
	if (!osk->kb_ready)
		return;
	struct wlr_seat *seat = osk->server->seat->seat;
	wlr_seat_set_keyboard(seat, &osk->kb);
	if (shift)
		kb_key(osk, KEY_LEFTSHIFT, true);
	kb_key(osk, code, true);
	kb_key(osk, code, false);
	if (shift)
		kb_key(osk, KEY_LEFTSHIFT, false);
	wlr_idle_notifier_v1_notify_activity(osk->server->idle, seat);
}

/* -- rendering ----------------------------------------------------------- */

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

/* alpha-blend a straight-alpha colour over a premultiplied pixel */
static void
blend(uint32_t *px, float r, float g, float b, float a)
{
	if (a <= 0)
		return;
	uint32_t d = *px;
	float da = ((d >> 24) & 0xff) / 255.0f;
	float dr = ((d >> 16) & 0xff) / 255.0f;
	float dg = ((d >> 8) & 0xff) / 255.0f;
	float db = (d & 0xff) / 255.0f;
	float o = 1.0f - a;
	float oa = a + da * o;
	float orr = r * a + dr * o, og = g * a + dg * o, ob = b * a + db * o;
	*px = ((uint32_t) (oa * 255 + 0.5f) << 24) | ((uint32_t) (orr * 255 + 0.5f) << 16) |
	      ((uint32_t) (og * 255 + 0.5f) << 8) | (uint32_t) (ob * 255 + 0.5f);
}

static void
fill_rr(uint32_t *data, int W, int H, struct wlr_box b, int rad, float r, float g, float bl, float a)
{
	for (int y = b.y; y < b.y + b.height && y < H; y++) {
		if (y < 0)
			continue;
		for (int x = b.x; x < b.x + b.width && x < W; x++) {
			if (x < 0)
				continue;
			float dx = 0, dy = 0;
			if (x < b.x + rad)
				dx = b.x + rad - x;
			else if (x > b.x + b.width - rad)
				dx = x - (b.x + b.width - rad);
			if (y < b.y + rad)
				dy = b.y + rad - y;
			else if (y > b.y + b.height - rad)
				dy = y - (b.y + b.height - rad);
			float cov = 1.0f;
			if (dx > 0 || dy > 0) {
				float d = sqrtf(dx * dx + dy * dy);
				cov = (float) rad - d;
				if (cov <= 0)
					continue;
				if (cov > 1)
					cov = 1;
			}
			blend(&data[y * W + x], r, g, bl, a * cov);
		}
	}
}

static void
draw_label(uint32_t *data, int W, int H, struct fcft_font *font, const char *s, struct wlr_box key, float a)
{
	if (!font || !s || !s[0])
		return;
	/* measure */
	int tw = 0;
	for (const char *p = s; *p; p++) {
		const struct fcft_glyph *gl = fcft_rasterize_char_utf32(font, (uint32_t) *p, FCFT_SUBPIXEL_NONE);
		if (gl)
			tw += gl->advance.x;
	}
	int th = font->ascent + font->descent;
	int pen = key.x + (key.width - tw) / 2;
	int base = key.y + (key.height - th) / 2 + font->ascent;

	pixman_image_t *dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, W, H, data, W * 4);
	pixman_color_t pc = {0xfcfc, 0xfbfb, 0xf8f8, (uint16_t) (a * 0xffff)};
	pixman_image_t *src = pixman_image_create_solid_fill(&pc);
	for (const char *p = s; *p; p++) {
		const struct fcft_glyph *gl = fcft_rasterize_char_utf32(font, (uint32_t) *p, FCFT_SUBPIXEL_NONE);
		if (!gl)
			continue;
		if (gl->pix)
			pixman_image_composite32(PIXMAN_OP_OVER, src, gl->pix, dst, 0, 0, 0, 0, pen + gl->x,
						 base - gl->y, gl->width, gl->height);
		pen += gl->advance.x;
	}
	pixman_image_unref(src);
	pixman_image_unref(dst);
}

static struct fcft_font *
osk_font(int keyh)
{
	int sz = keyh * 34 / 100;
	if (sz < 10)
		sz = 10;
	if (sz > 34)
		sz = 34;
	char attr[32];
	snprintf(attr, sizeof(attr), "size=%d", sz);
	const char *n[] = {OSK_MONO};
	return fcft_from_name(1, n, attr);
}

static void
osk_render(struct ng_osk *osk)
{
	if (!osk->node || osk->area.width < 16 || osk->area.height < 16)
		return;
	int W = osk->area.width, H = osk->area.height;
	uint32_t *data = calloc((size_t) W * H, 4);
	if (!data)
		return;

	/* glass backdrop */
	for (int i = 0; i < W * H; i++)
		data[i] = premul(0.05f, 0.04f, 0.03f, 0.72f);
	for (int x = 0; x < W; x++)
		blend(&data[x], 1, 1, 1, 0.10f); /* hairline top */

	int pad = H / 40;
	int rows = 4;
	int gap = H / 90;
	int rowh = (H - 2 * pad - (rows - 1) * gap) / rows;
	struct fcft_font *font = osk_font(rowh);

	struct key(*L)[14] = cur_rows(osk);
	for (int r = 0; r < rows; r++) {
		/* total units in this row */
		float units = 0;
		int nk = 0;
		for (struct key *k = L[r]; k->lbl; k++, nk++)
			units += k->units;
		if (nk == 0)
			continue;
		float keyw = (W - 2 * pad - (nk - 1) * gap) / units;
		int y = pad + r * (rowh + gap);
		float x = pad;
		for (struct key *k = L[r]; k->lbl; k++) {
			int kw = (int) (keyw * k->units);
			k->box = (struct wlr_box){(int) x + osk->area.x, y + osk->area.y, kw, rowh};
			struct wlr_box local = {(int) x, y, kw, rowh};

			bool hot = (k->kind == KK_SHIFT && osk->shift) ||
				   (k->kind == KK_LAYER && osk->layer == 1);
			float ka = hot ? 0.30f : 0.14f;
			fill_rr(data, W, H, local, rowh / 5, 1, 1, 1, ka);

			const char *lbl = k->lbl;
			char up[8];
			if (k->kind == KK_CHAR && osk->layer == 0 && osk->shift && k->code >= KEY_Q &&
			    k->lbl[0] >= 'a' && k->lbl[0] <= 'z' && !k->lbl[1]) {
				up[0] = k->lbl[0] - 32;
				up[1] = 0;
				lbl = up;
			}
			const char *sym = NULL;
			if (k->kind == KK_SHIFT)
				sym = osk->shift ? "SHIFT" : "shift";
			else if (k->kind == KK_BKSP)
				sym = "del";
			else if (k->kind == KK_ENTER)
				sym = "ret";
			else if (k->kind == KK_HIDE)
				sym = "hide";
			else if (k->kind == KK_SPACE)
				sym = "";
			draw_label(data, W, H, font, sym ? sym : lbl, local, 0.92f);
			x += kw + gap;
		}
	}
	if (font)
		fcft_destroy(font);

	struct wlr_buffer *buf = ng_argb_buffer(data, W, H);
	wlr_scene_buffer_set_buffer(osk->node, buf);
	if (buf) {
		wlr_scene_buffer_set_dest_size(osk->node, W, H);
		wlr_buffer_drop(buf);
	}
	wlr_scene_node_set_position(&osk->node->node, osk->area.x, osk->area.y);
}

/* -- public ------------------------------------------------------------- */

struct ng_osk *
ng_osk_create(struct cg_server *server)
{
	struct ng_osk *osk = calloc(1, sizeof(*osk));
	if (!osk)
		return NULL;
	osk->server = server;
	osk->tree = wlr_scene_tree_create(&server->scene->tree);
	if (!osk->tree) {
		free(osk);
		return NULL;
	}
	osk->node = wlr_scene_buffer_create(osk->tree, NULL);
	wlr_scene_node_set_enabled(&osk->tree->node, false);

	wlr_keyboard_init(&osk->kb, &kb_impl, "neuros-osk");
	struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (ctx) {
		struct xkb_keymap *km = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
		if (km) {
			wlr_keyboard_set_keymap(&osk->kb, km);
			xkb_keymap_unref(km);
			osk->kb_ready = true;
		}
		xkb_context_unref(ctx);
	}
	if (!osk->kb_ready) {
		wlr_log(WLR_ERROR, "ng_osk: no keymap - key injection disabled");
		return osk;
	}

	/* Register as the seat keyboard now, before any client maps - many phone
	 * builds have no physical keyboard, so without this the client never gets
	 * a keymap and ignores every key we send. */
	if (server->seat)
		seat_set_osk_keyboard(server->seat, &osk->kb);
	return osk;
}

void
ng_osk_destroy(struct ng_osk *osk)
{
	if (!osk)
		return;
	if (osk->kb_ready)
		wlr_keyboard_finish(&osk->kb);
	if (osk->tree)
		wlr_scene_node_destroy(&osk->tree->node);
	free(osk);
}

void
ng_osk_layout(struct ng_osk *osk, int w, int h)
{
	if (!osk || w < 32 || h < 32)
		return;
	osk->w = w;
	osk->h = h;
	int kbh = h > w ? h * 34 / 100 : h * 44 / 100; /* portrait vs landscape */
	int margin = w / 40;
	osk->area = (struct wlr_box){margin, h - kbh, w - 2 * margin, kbh - margin};
	wlr_scene_node_raise_to_top(&osk->tree->node);
	osk_render(osk);
}

void
ng_osk_set_visible(struct ng_osk *osk, bool visible)
{
	if (!osk || osk->visible == visible)
		return;
	osk->visible = visible;
	if (visible) {
		osk->shift = false;
		wlr_scene_node_raise_to_top(&osk->tree->node);
		osk_render(osk);
	}
	wlr_scene_node_set_enabled(&osk->tree->node, visible);
}

bool
ng_osk_is_visible(struct ng_osk *osk)
{
	return osk && osk->visible;
}

int
ng_osk_top(struct ng_osk *osk)
{
	if (!osk || !osk->visible)
		return osk ? osk->h : 0;
	return osk->area.y;
}

bool
ng_osk_tap(struct ng_osk *osk, double lx, double ly)
{
	if (!osk || !osk->visible)
		return false;
	if (lx < osk->area.x || lx >= osk->area.x + osk->area.width || ly < osk->area.y ||
	    ly >= osk->area.y + osk->area.height)
		return false;

	struct key(*L)[14] = cur_rows(osk);
	for (int r = 0; r < 4; r++) {
		for (struct key *k = L[r]; k->lbl; k++) {
			struct wlr_box b = k->box;
			if (lx < b.x || lx >= b.x + b.width || ly < b.y || ly >= b.y + b.height)
				continue;
			switch (k->kind) {
			case KK_CHAR:
				osk_send(osk, k->code, k->shift || (osk->layer == 0 && osk->shift));
				if (osk->shift) {
					osk->shift = false;
					osk_render(osk);
				}
				break;
			case KK_SHIFT:
				osk->shift = !osk->shift;
				osk_render(osk);
				break;
			case KK_LAYER:
				osk->layer = osk->layer == 0 ? 1 : 0;
				osk->shift = false;
				osk_render(osk);
				break;
			case KK_BKSP:
				osk_send(osk, KEY_BACKSPACE, false);
				break;
			case KK_ENTER:
				osk_send(osk, KEY_ENTER, false);
				break;
			case KK_SPACE:
				osk_send(osk, KEY_SPACE, false);
				break;
			case KK_HIDE:
				ng_osk_set_visible(osk, false);
				break;
			}
			return true;
		}
	}
	return true; /* inside the panel but between keys - still consume */
}
