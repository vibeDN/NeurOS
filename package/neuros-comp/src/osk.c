/*
 * NeurOS on-screen keyboard. See osk.h. MIT.
 *
 * Four layouts on one synthetic keyboard: English (group 1), Russian YCUKEN
 * (group 2), symbols, and an emoji grid (group 3, Noto Emoji). The globe key
 * toggles EN<->RU; ?123 -> symbols; the smiley -> emoji.
 */
#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

enum layer { LY_EN, LY_RU, LY_SYM, LY_SYM2, LY_EMOJI };
enum kk { KK_CHAR, KK_SHIFT, KK_SYM, KK_SYM2, KK_LANG, KK_EMOJI, KK_ABC, KK_BKSP, KK_ENTER, KK_SPACE, KK_HIDE };

struct key {
	const char *lbl;     /* display (UTF-8) */
	enum kk kind;
	uint16_t code;       /* evdev keycode (KK_CHAR) */
	uint8_t group;       /* xkb group to send in (0 EN, 1 RU, 2 emoji) */
	bool shift;          /* KK_CHAR: hold shift */
	float units;
	struct wlr_box box;
};

#define ROWMAX 14

/* physical keycodes for the 3 letter rows (YCUKEN needs the [ ] ; ' , . keys) */
static const uint16_t AD_C[] = {KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T,          KEY_Y,
				KEY_U, KEY_I, KEY_O, KEY_P, KEY_LEFTBRACE, KEY_RIGHTBRACE};
static const uint16_t AC_C[] = {KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_SEMICOLON,
				KEY_APOSTROPHE};
static const uint16_t AB_C[] = {KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_COMMA, KEY_DOT};
static const uint16_t AE_C[] = {KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0};

/* --- xkb keysym names + labels, per position ------------------------- */
/* English letters (also the group-1 keysym for the ascii keys) */
static const char EN_AD[] = "qwertyuiop[]";
static const char EN_AC[] = "asdfghjkl;'";
static const char EN_AB[] = "zxcvbnm,.";

/* Russian YCUKEN - xkb keysym names, then display glyphs (same order) */
static const char *RU_AD_SYM[] = {"Cyrillic_shorti", "Cyrillic_tse",  "Cyrillic_u",   "Cyrillic_ka",
				  "Cyrillic_ie",     "Cyrillic_en",   "Cyrillic_ghe", "Cyrillic_sha",
				  "Cyrillic_shcha",  "Cyrillic_ze",   "Cyrillic_ha",  "Cyrillic_hardsign"};
static const char *RU_AC_SYM[] = {"Cyrillic_ef", "Cyrillic_yeru", "Cyrillic_ve", "Cyrillic_a",  "Cyrillic_pe",
				  "Cyrillic_er", "Cyrillic_o",    "Cyrillic_el", "Cyrillic_de", "Cyrillic_zhe",
				  "Cyrillic_e"};
static const char *RU_AB_SYM[] = {"Cyrillic_ya", "Cyrillic_che", "Cyrillic_es",       "Cyrillic_em", "Cyrillic_i",
				  "Cyrillic_te", "Cyrillic_softsign", "Cyrillic_be", "Cyrillic_yu"};

/* emoji - codepoints assigned to the 32 letter keycodes + 10 digit keycodes */
static const uint32_t EMOJI[42] = {
	0x1F600, 0x1F603, 0x1F604, 0x1F601, 0x1F606, 0x1F602, 0x1F979, 0x1F60A, 0x1F60D, 0x1F60E, 0x1F914,
	0x1F634, /* AD 12 */
	0x1F62D, 0x1F624, 0x1F973, 0x1F92F, 0x1FAE1, 0x1F972, 0x1F44D, 0x1F44E, 0x1F44F, 0x1F64F,
	0x1F91D, /* AC 11 */
	0x1F44B, 0x1F9E1, 0x1F525, 0x2728, 0x1F389, 0x1F4AF, 0x1F440, 0x1F4BB, 0x1F4F1, /* AB 9 */
	0x1F50B, 0x1F4A1, 0x1F512, 0x1F916, 0x1F680, 0x2615, 0x1F3A7, 0x2705, 0x2B50, 0x26A1, /* digits 10 */
};

static char *
km_line(char *p, char *end, const char *xkname, const char *g1, const char *g2, uint32_t emoji)
{
	return p + snprintf(p, end - p, "  override key <%s> { [ %s ], [ %s ], [ U%04X ] };\n", xkname, g1, g2,
			    emoji);
}

/* Build the 3-group keymap. Returns a malloc'd string. */
static char *
build_keymap(void)
{
	size_t cap = 16384;
	char *s = malloc(cap), *p = s, *end = s + cap;
	if (!s)
		return NULL;
	p += snprintf(p, end - p,
		      "xkb_keymap {\n"
		      "xkb_keycodes { include \"evdev+aliases(qwerty)\" };\n"
		      "xkb_types    { include \"complete\" };\n"
		      "xkb_compat   { include \"complete\" };\n"
		      "xkb_symbols {\n"
		      "  name[Group1]=\"English\";\n  name[Group2]=\"Russian\";\n  name[Group3]=\"Emoji\";\n"
		      "  include \"pc+us+inet(evdev)\"\n");
	static const char *AD_XK[] = {"AD01", "AD02", "AD03", "AD04", "AD05", "AD06",
				      "AD07", "AD08", "AD09", "AD10", "AD11", "AD12"};
	static const char *AC_XK[] = {"AC01", "AC02", "AC03", "AC04", "AC05", "AC06",
				      "AC07", "AC08", "AC09", "AC10", "AC11"};
	static const char *AB_XK[] = {"AB01", "AB02", "AB03", "AB04", "AB05", "AB06", "AB07", "AB08", "AB09"};
	static const char *AE_XK[] = {"AE01", "AE02", "AE03", "AE04", "AE05",
				      "AE06", "AE07", "AE08", "AE09", "AE10"};
	int e = 0;
	char g1[4];
	for (int i = 0; i < 12; i++) {
		g1[0] = EN_AD[i];
		g1[1] = 0;
		const char *sym = (EN_AD[i] == '[') ? "bracketleft" : (EN_AD[i] == ']') ? "bracketright" : g1;
		p = km_line(p, end, AD_XK[i], sym, RU_AD_SYM[i], EMOJI[e++]);
	}
	for (int i = 0; i < 11; i++) {
		g1[0] = EN_AC[i];
		g1[1] = 0;
		const char *sym = (EN_AC[i] == ';') ? "semicolon" : (EN_AC[i] == '\'') ? "apostrophe" : g1;
		p = km_line(p, end, AC_XK[i], sym, RU_AC_SYM[i], EMOJI[e++]);
	}
	for (int i = 0; i < 9; i++) {
		g1[0] = EN_AB[i];
		g1[1] = 0;
		const char *sym = (EN_AB[i] == ',') ? "comma" : (EN_AB[i] == '.') ? "period" : g1;
		p = km_line(p, end, AB_XK[i], sym, RU_AB_SYM[i], EMOJI[e++]);
	}
	for (int i = 0; i < 10; i++) {
		char d[2] = {(char) ('1' + (i == 9 ? -1 : i)), 0}; /* 1..9,0 */
		if (i == 9)
			d[0] = '0';
		p += snprintf(p, end - p, "  override key <%s> { [ %s ], [ %s ], [ U%04X ] };\n", AE_XK[i], d, d,
			      EMOJI[e++]);
	}
	/* extra symbols on the F-keys, group 3 (the #+= layer reaches these) */
	static const char *FK_XK[] = {"FK01", "FK02", "FK03", "FK04", "FK05",
				      "FK06", "FK07", "FK08", "FK09", "FK10"};
	static const char *FK_SYM[] = {"EuroSign", "sterling",   "yen",      "periodcentered", "section",
				       "degree",   "multiply",   "division", "U2022",          "U2026"};
	for (int i = 0; i < 10; i++)
		p += snprintf(p, end - p, "  override key <%s> { [ NoSymbol ], [ NoSymbol ], [ %s ] };\n", FK_XK[i],
			      FK_SYM[i]);
	p += snprintf(p, end - p, "};\n};\n");
	return s;
}

/* --- layout tables -------------------------------------------------- */

#define K0(l, c) {(l), KK_CHAR, (c), 0, false, 1.0f, {0}}   /* group 0 (EN/sym) */
#define K1(l, c) {(l), KK_CHAR, (c), 1, false, 1.0f, {0}}   /* group 1 (RU) */
#define K2(l, c) {(l), KK_CHAR, (c), 2, false, 1.0f, {0}}   /* group 2 (emoji) */
#define KSH(l, c) {(l), KK_CHAR, (c), 0, true, 1.0f, {0}}   /* shifted symbol */
#define SHIFT {"shift", KK_SHIFT, 0, 0, false, 1.5f, {0}}
#define BKSP {"del", KK_BKSP, 0, 0, false, 1.5f, {0}}
#define ENTER {"ret", KK_ENTER, 0, 0, false, 1.7f, {0}}
#define SPACE {"space", KK_SPACE, 0, 0, false, 4.0f, {0}}
#define HIDE {"v", KK_HIDE, 0, 0, false, 1.0f, {0}}
#define SYMK {"?123", KK_SYM, 0, 0, false, 1.5f, {0}}
#define SYM2K {"#+=", KK_SYM2, 0, 0, false, 1.5f, {0}}
#define ABCK {"ABC", KK_ABC, 0, 0, false, 1.5f, {0}}
#define KE(l, c) {(l), KK_CHAR, (c), 2, false, 1.0f, {0}} /* group 2 special (F-keys) */
#define LANGK {"글", KK_LANG, 0, 0, false, 1.2f, {0}}
#define EMOK {"^_^", KK_EMOJI, 0, 0, false, 1.2f, {0}}
#define END {0}

static struct key g_en[4][ROWMAX] = {
	{K0("q", KEY_Q), K0("w", KEY_W), K0("e", KEY_E), K0("r", KEY_R), K0("t", KEY_T), K0("y", KEY_Y),
	 K0("u", KEY_U), K0("i", KEY_I), K0("o", KEY_O), K0("p", KEY_P), END},
	{K0("a", KEY_A), K0("s", KEY_S), K0("d", KEY_D), K0("f", KEY_F), K0("g", KEY_G), K0("h", KEY_H),
	 K0("j", KEY_J), K0("k", KEY_K), K0("l", KEY_L), END},
	{SHIFT, K0("z", KEY_Z), K0("x", KEY_X), K0("c", KEY_C), K0("v", KEY_V), K0("b", KEY_B), K0("n", KEY_N),
	 K0("m", KEY_M), BKSP, END},
	{SYMK, LANGK, EMOK, SPACE, K0(",", KEY_COMMA), K0(".", KEY_DOT), ENTER, HIDE, END},
};

static struct key g_ru[4][ROWMAX] = {
	{K1("й", KEY_Q), K1("ц", KEY_W), K1("у", KEY_E), K1("к", KEY_R), K1("е", KEY_T), K1("н", KEY_Y),
	 K1("г", KEY_U), K1("ш", KEY_I), K1("щ", KEY_O), K1("з", KEY_P), K1("х", KEY_LEFTBRACE),
	 K1("ъ", KEY_RIGHTBRACE), END},
	{K1("ф", KEY_A), K1("ы", KEY_S), K1("в", KEY_D), K1("а", KEY_F), K1("п", KEY_G), K1("р", KEY_H),
	 K1("о", KEY_J), K1("л", KEY_K), K1("д", KEY_L), K1("ж", KEY_SEMICOLON), K1("э", KEY_APOSTROPHE), END},
	{SHIFT, K1("я", KEY_Z), K1("ч", KEY_X), K1("с", KEY_C), K1("м", KEY_V), K1("и", KEY_B), K1("т", KEY_N),
	 K1("ь", KEY_M), K1("б", KEY_COMMA), K1("ю", KEY_DOT), BKSP, END},
	{SYMK, LANGK, EMOK, SPACE, K0(",", KEY_COMMA), K0(".", KEY_DOT), ENTER, HIDE, END},
};

static struct key g_sym[4][ROWMAX] = {
	{K0("1", KEY_1), K0("2", KEY_2), K0("3", KEY_3), K0("4", KEY_4), K0("5", KEY_5), K0("6", KEY_6),
	 K0("7", KEY_7), K0("8", KEY_8), K0("9", KEY_9), K0("0", KEY_0), END},
	{KSH("@", KEY_2), KSH("#", KEY_3), KSH("$", KEY_4), KSH("&", KEY_7), K0("-", KEY_MINUS), KSH("+", KEY_EQUAL),
	 KSH("(", KEY_9), KSH(")", KEY_0), K0("/", KEY_SLASH), KSH("*", KEY_8), END},
	{SYM2K, KSH("\"", KEY_APOSTROPHE), K0("'", KEY_APOSTROPHE), KSH(":", KEY_SEMICOLON), K0(";", KEY_SEMICOLON),
	 KSH("!", KEY_1), KSH("?", KEY_SLASH), K0(".", KEY_DOT), K0(",", KEY_COMMA), BKSP, END},
	{ABCK, LANGK, EMOK, SPACE, ENTER, HIDE, END},
};

static struct key g_sym2[4][ROWMAX] = {
	{K0("[", KEY_LEFTBRACE), K0("]", KEY_RIGHTBRACE), KSH("{", KEY_LEFTBRACE), KSH("}", KEY_RIGHTBRACE),
	 KSH("#", KEY_3), KSH("%", KEY_5), KSH("^", KEY_6), KSH("*", KEY_8), KSH("+", KEY_EQUAL), K0("=", KEY_EQUAL),
	 END},
	{KSH("_", KEY_MINUS), K0("\\", KEY_BACKSLASH), KSH("|", KEY_BACKSLASH), KSH("~", KEY_GRAVE),
	 KSH("<", KEY_COMMA), KSH(">", KEY_DOT), KE("€", KEY_F1), KE("£", KEY_F2), KE("¥", KEY_F3), KE("•", KEY_F9),
	 END},
	{SYMK, KE("§", KEY_F5), KE("°", KEY_F6), KE("·", KEY_F4), KE("×", KEY_F7), KE("÷", KEY_F8), KE("…", KEY_F10),
	 K0("`", KEY_GRAVE), K0("/", KEY_SLASH), BKSP, END},
	{ABCK, LANGK, EMOK, SPACE, ENTER, HIDE, END},
};

/* emoji grid: 42 emoji over the same 42 keycodes as the keymap (group 2) */
static struct key g_emoji[5][ROWMAX];
static char g_emoji_lbl[42][8];

static void
build_emoji_layer(void)
{
	const uint16_t *codes[] = {AD_C, AC_C, AB_C, AE_C};
	int counts[] = {12, 11, 9, 10};
	int e = 0;
	for (int r = 0; r < 4; r++) {
		int n = counts[r];
		for (int i = 0; i < n && e < 42; i++, e++) {
			uint32_t cp = EMOJI[e];
			char *o = g_emoji_lbl[e];
			/* encode cp as UTF-8 */
			if (cp < 0x80) {
				o[0] = cp;
				o[1] = 0;
			} else if (cp < 0x800) {
				o[0] = 0xC0 | (cp >> 6);
				o[1] = 0x80 | (cp & 0x3F);
				o[2] = 0;
			} else if (cp < 0x10000) {
				o[0] = 0xE0 | (cp >> 12);
				o[1] = 0x80 | ((cp >> 6) & 0x3F);
				o[2] = 0x80 | (cp & 0x3F);
				o[3] = 0;
			} else {
				o[0] = 0xF0 | (cp >> 18);
				o[1] = 0x80 | ((cp >> 12) & 0x3F);
				o[2] = 0x80 | ((cp >> 6) & 0x3F);
				o[3] = 0x80 | (cp & 0x3F);
				o[4] = 0;
			}
			g_emoji[r][i] = (struct key){o, KK_CHAR, codes[r][i], 2, false, 1.0f, {0}};
		}
		g_emoji[r][n] = (struct key) END;
	}
	g_emoji[4][0] = (struct key) ABCK;
	g_emoji[4][1] = (struct key) LANGK;
	g_emoji[4][2] = (struct key){"space", KK_SPACE, 0, 0, false, 5.0f, {0}};
	g_emoji[4][3] = (struct key) BKSP;
	g_emoji[4][4] = (struct key) ENTER;
	g_emoji[4][5] = (struct key) HIDE;
	g_emoji[4][6] = (struct key) END;
}

struct ng_osk {
	struct cg_server *server;
	struct wlr_scene_tree *tree;
	struct wlr_scene_buffer *node;
	struct wlr_keyboard kb;
	bool kb_ready;

	int w, h;
	struct wlr_box area;
	enum layer layer;
	enum layer letter; /* LY_EN or LY_RU - what ?123 / emoji return to */
	bool shift;
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

static struct key (*cur_rows(struct ng_osk *osk))[ROWMAX]
{
	switch (osk->layer) {
	case LY_RU:
		return g_ru;
	case LY_SYM:
		return g_sym;
	case LY_SYM2:
		return g_sym2;
	case LY_EMOJI:
		return g_emoji;
	default:
		return g_en;
	}
}

static int
cur_nrows(struct ng_osk *osk)
{
	return osk->layer == LY_EMOJI ? 5 : 4;
}

/* -- key injection --------------------------------------------------- */

static void
raw_key(struct ng_osk *osk, uint32_t code, bool pressed)
{
	uint32_t t = now_ms();
	enum wl_keyboard_key_state st = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED;
	struct wlr_keyboard_key_event ev = {.time_msec = t, .keycode = code, .update_state = true, .state = st};
	wlr_keyboard_notify_key(&osk->kb, &ev);
	wlr_seat_keyboard_notify_key(osk->server->seat->seat, t, code, st);
}

static void
osk_send(struct ng_osk *osk, uint16_t code, bool shift, uint8_t group)
{
	if (!osk->kb_ready)
		return;
	struct wlr_seat *seat = osk->server->seat->seat;
	wlr_seat_set_keyboard(seat, &osk->kb);

	uint32_t dep = shift ? 0x1u : 0u; /* Shift = modifier bit 0 */
	wlr_keyboard_notify_modifiers(&osk->kb, dep, 0, 0, group);
	wlr_seat_keyboard_notify_modifiers(seat, &osk->kb.modifiers);

	raw_key(osk, code, true);
	raw_key(osk, code, false);

	wlr_keyboard_notify_modifiers(&osk->kb, 0, 0, 0, 0);
	wlr_seat_keyboard_notify_modifiers(seat, &osk->kb.modifiers);
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
	return (A << 24) | ((uint32_t) (r * a * 255 + 0.5f) << 16) | ((uint32_t) (g * a * 255 + 0.5f) << 8) |
	       (uint32_t) (b * a * 255 + 0.5f);
}

static void
blend(uint32_t *px, float r, float g, float b, float a)
{
	if (a <= 0)
		return;
	uint32_t d = *px;
	float da = ((d >> 24) & 0xff) / 255.0f, dr = ((d >> 16) & 0xff) / 255.0f, dg = ((d >> 8) & 0xff) / 255.0f,
	      db = (d & 0xff) / 255.0f;
	float o = 1.0f - a;
	*px = ((uint32_t) ((a + da * o) * 255 + 0.5f) << 24) | ((uint32_t) ((r * a + dr * o) * 255 + 0.5f) << 16) |
	      ((uint32_t) ((g * a + dg * o) * 255 + 0.5f) << 8) | (uint32_t) ((b * a + db * o) * 255 + 0.5f);
}

static void
fill_rr(uint32_t *data, int W, int H, struct wlr_box b, int rad, float a)
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
			blend(&data[y * W + x], 1, 1, 1, a * cov);
		}
	}
}

static size_t
u8dec(const char *s, uint32_t *cp)
{
	const unsigned char *p = (const unsigned char *) s;
	if (*p < 0x80) {
		*cp = *p;
		return 1;
	}
	if ((*p & 0xE0) == 0xC0) {
		*cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
		return 2;
	}
	if ((*p & 0xF0) == 0xE0) {
		*cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
		return 3;
	}
	*cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
	return 4;
}

static void
draw_label(uint32_t *data, int W, int H, struct fcft_font *font, const char *s, struct wlr_box key, float a)
{
	if (!font || !s || !s[0])
		return;
	uint32_t cps[24];
	int n = 0;
	for (const char *p = s; *p && n < 24;) {
		uint32_t cp;
		p += u8dec(p, &cp);
		cps[n++] = cp;
	}
	int tw = 0;
	for (int i = 0; i < n; i++) {
		const struct fcft_glyph *gl = fcft_rasterize_char_utf32(font, cps[i], FCFT_SUBPIXEL_NONE);
		if (gl)
			tw += gl->advance.x;
	}
	int th = font->ascent + font->descent;
	int pen = key.x + (key.width - tw) / 2, base = key.y + (key.height - th) / 2 + font->ascent;

	pixman_image_t *dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, W, H, data, W * 4);
	pixman_color_t pc = {0xfcfc, 0xfbfb, 0xf8f8, (uint16_t) (a * 0xffff)};
	pixman_image_t *src = pixman_image_create_solid_fill(&pc);
	for (int i = 0; i < n; i++) {
		const struct fcft_glyph *gl = fcft_rasterize_char_utf32(font, cps[i], FCFT_SUBPIXEL_NONE);
		if (!gl)
			continue;
		if (gl->pix) {
			/* Noto Emoji is monochrome (A8 mask); tint with the label colour */
			pixman_image_composite32(PIXMAN_OP_OVER, src, gl->pix, dst, 0, 0, 0, 0, pen + gl->x,
						 base - gl->y, gl->width, gl->height);
		}
		pen += gl->advance.x;
	}
	pixman_image_unref(src);
	pixman_image_unref(dst);
}

static struct fcft_font *
osk_font(int keyh)
{
	int sz = keyh * 36 / 100;
	if (sz < 10)
		sz = 10;
	if (sz > 34)
		sz = 34;
	char attr[32];
	snprintf(attr, sizeof(attr), "size=%d", sz);
	const char *n[] = {"JetBrains Mono", "Noto Emoji"};
	return fcft_from_name(2, n, attr);
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

	for (int i = 0; i < W * H; i++)
		data[i] = premul(0.05f, 0.04f, 0.03f, 0.74f);
	for (int x = 0; x < W; x++)
		blend(&data[x], 1, 1, 1, 0.10f);

	int rows = cur_nrows(osk);
	int pad = H / 44;
	int gap = H / 100;
	int rowh = (H - 2 * pad - (rows - 1) * gap) / rows;
	int krad = rowh / 8; /* subtle rounded-rect, not a pill */
	if (krad < 4)
		krad = 4;
	if (krad > 12)
		krad = 12;
	struct fcft_font *font = osk_font(rowh);

	struct key(*L)[ROWMAX] = cur_rows(osk);
	for (int r = 0; r < rows; r++) {
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

			bool hot = (k->kind == KK_SHIFT && osk->shift) || (k->kind == KK_LANG && osk->layer == LY_RU);
			fill_rr(data, W, H, local, krad, hot ? 0.30f : 0.13f);

			const char *lbl = k->lbl;
			char up[8];
			if (k->kind == KK_CHAR && osk->layer == LY_EN && osk->shift && k->lbl[0] >= 'a' &&
			    k->lbl[0] <= 'z' && !k->lbl[1]) {
				up[0] = k->lbl[0] - 32;
				up[1] = 0;
				lbl = up;
			}
			if (k->kind == KK_SHIFT)
				lbl = osk->shift ? "SHIFT" : "shift";
			else if (k->kind == KK_LANG)
				lbl = (osk->layer == LY_RU) ? "RU" : "EN";
			else if (k->kind == KK_EMOJI)
				lbl = ":)";
			else if (k->kind == KK_SPACE)
				lbl = "";
			draw_label(data, W, H, font, lbl, local, 0.92f);
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
	osk->layer = LY_EN;
	osk->letter = LY_EN;
	build_emoji_layer();

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
		char *kmstr = build_keymap();
		struct xkb_keymap *km =
			kmstr ? xkb_keymap_new_from_string(ctx, kmstr, XKB_KEYMAP_FORMAT_TEXT_V1,
							  XKB_KEYMAP_COMPILE_NO_FLAGS)
			      : NULL;
		free(kmstr);
		if (!km) /* fall back to a plain us keymap - RU/emoji won't work */
			km = xkb_keymap_new_from_names(ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
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
	int kbh = h > w ? h * 36 / 100 : h * 46 / 100;
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

	struct key(*L)[ROWMAX] = cur_rows(osk);
	int rows = cur_nrows(osk);
	for (int r = 0; r < rows; r++) {
		for (struct key *k = L[r]; k->lbl; k++) {
			struct wlr_box b = k->box;
			if (lx < b.x || lx >= b.x + b.width || ly < b.y || ly >= b.y + b.height)
				continue;
			switch (k->kind) {
			case KK_CHAR: {
				bool sh = k->shift || (osk->layer == LY_EN && osk->shift);
				osk_send(osk, k->code, sh, k->group);
				if (osk->shift && osk->layer == LY_EN) {
					osk->shift = false;
					osk_render(osk);
				}
				break;
			}
			case KK_SHIFT:
				osk->shift = !osk->shift;
				osk_render(osk);
				break;
			case KK_SYM:
				osk->layer = LY_SYM;
				osk->shift = false;
				osk_render(osk);
				break;
			case KK_SYM2:
				osk->layer = LY_SYM2;
				osk->shift = false;
				osk_render(osk);
				break;
			case KK_ABC:
				osk->layer = osk->letter;
				osk_render(osk);
				break;
			case KK_LANG:
				osk->letter = (osk->letter == LY_EN) ? LY_RU : LY_EN;
				osk->layer = osk->letter;
				osk->shift = false;
				osk_render(osk);
				break;
			case KK_EMOJI:
				osk->layer = LY_EMOJI;
				osk_render(osk);
				break;
			case KK_BKSP:
				osk_send(osk, KEY_BACKSPACE, false, 0);
				break;
			case KK_ENTER:
				osk_send(osk, KEY_ENTER, false, 0);
				break;
			case KK_SPACE:
				osk_send(osk, KEY_SPACE, false, 0);
				break;
			case KK_HIDE:
				ng_osk_set_visible(osk, false);
				break;
			}
			return true;
		}
	}
	return true;
}
