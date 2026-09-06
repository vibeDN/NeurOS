/*
 * NeurOS on-screen keyboard. See osk.h. MIT.
 *
 * Five layouts on one synthetic keyboard: English (xkb group 1), Russian
 * YCUKEN (group 2), ?123 symbols, a #+= brackets/currency layer, and an emoji
 * grid. Cyrillic + emoji + EUR/GBP/JPY etc ride xkb groups 2/3 so the client
 * resolves the real character. EN<->RU via the globe key.
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

static size_t u8dec(const char *s, uint32_t *cp);

/* --- long-press alternates ------------------------------------------ *
 * Holding a key pops up its alternates - accented forms for a letter,
 * dashes / curly quotes for a symbol. xkb allows at most 4 groups/key and
 * we already use 3 (EN / RU / emoji), so there's room for exactly one
 * more: every alternate codepoint is parked in xkb Group4 on its own
 * keycode (accents on the 32 letter keycodes, symbols on the 10 digit
 * keycodes), and the popup sends that keycode in Group4. */
struct accent_set {
	char base;
	const char *alts; /* UTF-8, most-common first */
};
static const struct accent_set ACCENTS[] = {
	{'a', "àáâä"}, /* à á â ä */
	{'e', "èéêë"},     /* è é ê ë */
	{'i', "ìíîï"},     /* ì í î ï */
	{'o', "òóôö"}, /* ò ó ô ö */
	{'u', "ùúûü"},      /* ù ú û ü */
	{'y', "ýÿ"},                   /* ý ÿ */
	{'n', "ñ"},                       /* ñ */
	{'c', "çč"},                   /* ç č */
	{'s', "ß"},                       /* ß */
	{'z', "ž"},                       /* ž */
	{0, NULL},
};

/* long-press alternates on the ?123 / #+= symbol layers. Parked in Group4 on
 * the digit keycodes (AE01..), which the accent table (letter keycodes) leaves
 * free. 9 codepoints -> slots 32..40. */
static const struct accent_set SYMALTS[] = {
	{'-', "–—"},   /* en dash, em dash */
	{'.', "…"},     /* ellipsis */
	{'?', "¿"},     /* inverted question */
	{'!', "¡"},     /* inverted bang */
	{'"', "“”"}, /* curly double quotes */
	{'\'', "‘’"}, /* curly single quotes */
	{0, NULL},
};

/* Russian YCUKEN has no ё key - it hangs off a long-press of е. Parked in
 * Group4 on the last free digit keycode (slot 41 = AE10). */
#define RU_YO "ё"

/* accent/symbol codepoint parked at each of the 42 keycodes in xkb Group4 */
static uint32_t g_acc[42];

static void
fill_alts(const struct accent_set *tbl, int start)
{
	int i = start;
	for (const struct accent_set *as = tbl; as->base; as++)
		for (const char *q = as->alts; *q && i < 42;) {
			uint32_t cp;
			q += u8dec(q, &cp);
			g_acc[i++] = cp;
		}
}

static void
build_accents(void)
{
	memset(g_acc, 0, sizeof(g_acc));
	fill_alts(ACCENTS, 0);  /* letter keycodes 0..31 */
	fill_alts(SYMALTS, 32); /* digit keycodes 32..40 (9 slots) */
	uint32_t yo;
	u8dec(RU_YO, &yo);
	g_acc[41] = yo; /* AE10 - the one digit keycode SYMALTS leaves free */
}

/* keycode index (build order: AD*12, AC*11, AB*9, AE*10) -> evdev code */
static uint16_t
acc_evdev(int kci)
{
	if (kci < 12)
		return AD_C[kci];
	if (kci < 23)
		return AC_C[kci - 12];
	if (kci < 32)
		return AB_C[kci - 23];
	return AE_C[kci - 32];
}

/* the alternates string for a key on the current layer, or NULL:
 * letters a..z on the EN layer, punctuation on the ?123 / #+= layers. */
static const char *
key_alts(enum layer layer, struct key *k)
{
	if (!k || k->kind != KK_CHAR)
		return NULL;
	if (layer == LY_RU && k->group == 1 && strcmp(k->lbl, "е") == 0)
		return RU_YO; /* е -> ё */
	if (k->lbl[1])
		return NULL;
	char c = k->lbl[0];
	const struct accent_set *tbl = NULL;
	if (layer == LY_EN && k->group == 0 && c >= 'a' && c <= 'z')
		tbl = ACCENTS;
	else if (layer == LY_SYM || layer == LY_SYM2)
		tbl = SYMALTS;
	if (!tbl)
		return NULL;
	for (const struct accent_set *p = tbl; p->base; p++)
		if (p->base == c)
			return p->alts;
	return NULL;
}

/* resolve an alternates string to Group4 (code, label) pairs; returns n */
static int
alts_resolve(const char *alts, uint16_t *code, char lbl[][8], int max)
{
	int n = 0;
	for (const char *q = alts; *q && n < max;) {
		uint32_t cp;
		size_t len = u8dec(q, &cp);
		for (int i = 0; i < 42 && n < max; i++)
			if (g_acc[i] == cp) { /* where build_accents parked it */
				memcpy(lbl[n], q, len);
				lbl[n][len] = 0;
				code[n] = acc_evdev(i);
				n++;
				break;
			}
		q += len;
	}
	return n;
}

/* xkb layout index of Group4 (0-based: EN=0, RU=1, emoji=2, accents=3) */
#define ACC_GROUP 3

static char *
km_line(char *p, char *end, const char *xkname, const char *g1, const char *g2, uint32_t emoji, uint32_t acc)
{
	char e4[12];
	if (acc)
		snprintf(e4, sizeof(e4), "U%04X", acc);
	else
		snprintf(e4, sizeof(e4), "NoSymbol");
	return p + snprintf(p, end - p, "  override key <%s> { [ %s ], [ %s ], [ U%04X ], [ %s ] };\n", xkname, g1,
			    g2, emoji, e4);
}

/* Build the 4-group keymap. Returns a malloc'd string. */
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
		      "  name[Group4]=\"Accents\";\n"
		      "  include \"pc+us+inet(evdev)\"\n");
	build_accents();
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
		p = km_line(p, end, AD_XK[i], sym, RU_AD_SYM[i], EMOJI[e++], g_acc[i]);
	}
	for (int i = 0; i < 11; i++) {
		g1[0] = EN_AC[i];
		g1[1] = 0;
		const char *sym = (EN_AC[i] == ';') ? "semicolon" : (EN_AC[i] == '\'') ? "apostrophe" : g1;
		p = km_line(p, end, AC_XK[i], sym, RU_AC_SYM[i], EMOJI[e++], g_acc[12 + i]);
	}
	for (int i = 0; i < 9; i++) {
		g1[0] = EN_AB[i];
		g1[1] = 0;
		const char *sym = (EN_AB[i] == ',') ? "comma" : (EN_AB[i] == '.') ? "period" : g1;
		p = km_line(p, end, AB_XK[i], sym, RU_AB_SYM[i], EMOJI[e++], g_acc[23 + i]);
	}
	for (int i = 0; i < 10; i++) {
		char d[2] = {(char) ('1' + (i == 9 ? -1 : i)), 0}; /* 1..9,0 */
		if (i == 9)
			d[0] = '0';
		p = km_line(p, end, AE_XK[i], d, d, EMOJI[e++], g_acc[32 + i]);
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
	bool caps;         /* caps-lock: shift stays on (double-tap shift) */
	uint32_t last_shift;
	bool visible;

	/* press/hold state */
	struct key *pressed;     /* key drawn as held (highlight), or NULL */
	uint16_t held_code;      /* char key currently held down for repeat, 0 = none */
	uint8_t held_group;
	bool held_shift;

	/* long-press popup: accent chars (a KK_CHAR held) or a layout picker
	 * (the globe held) */
	struct wl_event_source *hold_timer;
	struct key *hold_key;    /* key awaiting the long-press timer, or NULL */
	double press_lx, press_ly;
	bool popup;
	int popup_mode;          /* 0 = accent chars, 1 = layout picker */
	int popup_n, popup_hot;  /* hot = cell under the finger, -1 = none */
	struct key popup_keys[10];
	char popup_lbl[10][8];
	enum layer popup_layer[10]; /* popup_mode 1: target layer per cell */
	struct wlr_box popup_area;

	/* space-bar cursor control: hold space, drag to move the caret */
	bool space_cursor;
	double space_anchor; /* lx the last arrow key was sent at */
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

/* press a key and leave it down (client-side auto-repeat kicks in) */
static void
osk_key_down(struct ng_osk *osk, uint16_t code, bool shift, uint8_t group)
{
	if (!osk->kb_ready)
		return;
	struct wlr_seat *seat = osk->server->seat->seat;
	wlr_seat_set_keyboard(seat, &osk->kb);
	wlr_keyboard_notify_modifiers(&osk->kb, shift ? 0x1u : 0u, 0, 0, group);
	wlr_seat_keyboard_notify_modifiers(seat, &osk->kb.modifiers);
	raw_key(osk, code, true);
	osk->held_code = code;
	osk->held_group = group;
	osk->held_shift = shift;
	wlr_idle_notifier_v1_notify_activity(osk->server->idle, seat);
}

static void
osk_key_up(struct ng_osk *osk)
{
	if (!osk->kb_ready || !osk->held_code)
		return;
	struct wlr_seat *seat = osk->server->seat->seat;
	raw_key(osk, osk->held_code, false);
	wlr_keyboard_notify_modifiers(&osk->kb, 0, 0, 0, 0);
	wlr_seat_keyboard_notify_modifiers(seat, &osk->kb.modifiers);
	osk->held_code = 0;
}

/* a full tap (down+up) for control keys / discrete injection */
static void
osk_send(struct ng_osk *osk, uint16_t code, bool shift, uint8_t group)
{
	osk_key_down(osk, code, shift, group);
	osk_key_up(osk);
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

static void osk_cancel_hold(struct ng_osk *osk);
static void osk_close_popup(struct ng_osk *osk);

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
	int krad = rowh / 12; /* mild rounded-rect */
	if (krad < 3)
		krad = 3;
	if (krad > 8)
		krad = 8;
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
			bool down = (k == osk->pressed);
			fill_rr(data, W, H, local, krad, down ? 0.42f : hot ? 0.30f : 0.13f);

			const char *lbl = k->lbl;
			char up[8];
			if (k->kind == KK_CHAR && osk->layer == LY_EN && osk->shift && k->lbl[0] >= 'a' &&
			    k->lbl[0] <= 'z' && !k->lbl[1]) {
				up[0] = k->lbl[0] - 32;
				up[1] = 0;
				lbl = up;
			}
			if (k->kind == KK_SHIFT)
				lbl = osk->caps ? "CAPS" : osk->shift ? "SHIFT" : "shift";
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

	/* accent long-press popup: a dark card floating over the key row */
	if (osk->popup && osk->popup_n > 0) {
		struct wlr_box pa = osk->popup_area;
		struct wlr_box panel = {pa.x - osk->area.x - gap, pa.y - osk->area.y - gap, pa.width + 2 * gap,
					pa.height + 2 * gap};
		for (int yy = panel.y; yy < panel.y + panel.height && yy < H; yy++) {
			if (yy < 0)
				continue;
			for (int xx = panel.x; xx < panel.x + panel.width && xx < W; xx++)
				if (xx >= 0)
					data[yy * W + xx] = premul(0.05f, 0.04f, 0.03f, 0.94f);
		}
		fill_rr(data, W, H, panel, krad + 2, 0.10f); /* faint rim */
		for (int i = 0; i < osk->popup_n; i++) {
			struct wlr_box b = osk->popup_keys[i].box;
			struct wlr_box lb = {b.x - osk->area.x, b.y - osk->area.y, b.width, b.height};
			fill_rr(data, W, H, lb, krad, i == osk->popup_hot ? 0.5f : 0.14f);
			draw_label(data, W, H, font, osk->popup_keys[i].lbl, lb, 0.95f);
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
			wlr_keyboard_set_repeat_info(&osk->kb, 28, 480); /* ~28/s after 480ms */
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
	if (osk->hold_timer)
		wl_event_source_remove(osk->hold_timer);
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
	osk_cancel_hold(osk);
	osk->popup = false;
	osk->popup_n = 0;
	osk->space_cursor = false;
	osk->w = w;
	osk->h = h;
	int margin = w / 40;
	/* size the keyboard from the key size, not a flat % of the screen - on a
	 * tall phone a flat 36% gave ~2:1 portrait keys. Aim for keys a touch
	 * wider than tall (~11 columns across the usable width). */
	int rows = 4; /* letters/space; emoji layer (5) just packs a bit tighter */
	int keyw = (w - 2 * margin) / 11;
	int rowh = keyw * 6 / 5;
	int gap = h / 100;
	int pad = h / 44;
	int kbh = rows * rowh + (rows - 1) * gap + 2 * pad + margin;
	int maxh = (h > w ? h * 42 : h * 52) / 100;
	if (kbh > maxh)
		kbh = maxh;
	osk->area = (struct wlr_box){margin, h - kbh, w - 2 * margin, kbh - margin};
	wlr_scene_node_raise_to_top(&osk->tree->node);
	osk_render(osk);
}

void
ng_osk_set_visible(struct ng_osk *osk, bool visible)
{
	if (!osk || osk->visible == visible)
		return;
	if (osk->held_code)
		osk_key_up(osk);
	osk_cancel_hold(osk);
	osk->popup = false;
	osk->popup_n = 0;
	osk->space_cursor = false;
	osk->pressed = NULL;
	osk->visible = visible;
	if (visible) {
		osk->shift = osk->caps = false;
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

static struct key *
key_at(struct ng_osk *osk, double lx, double ly)
{
	if (lx < osk->area.x || lx >= osk->area.x + osk->area.width || ly < osk->area.y ||
	    ly >= osk->area.y + osk->area.height)
		return NULL;
	struct key(*L)[ROWMAX] = cur_rows(osk);
	for (int r = 0; r < cur_nrows(osk); r++)
		for (struct key *k = L[r]; k->lbl; k++) {
			struct wlr_box b = k->box;
			if (lx >= b.x && lx < b.x + b.width && ly >= b.y && ly < b.y + b.height)
				return k;
		}
	return NULL;
}

/* -- long-press alternates popup -------------------------------------- */

static void
osk_cancel_hold(struct ng_osk *osk)
{
	if (osk->hold_timer)
		wl_event_source_timer_update(osk->hold_timer, 0); /* disarm */
	osk->hold_key = NULL;
}

static void
osk_close_popup(struct ng_osk *osk)
{
	if (!osk->popup)
		return;
	osk->popup = false;
	osk->popup_n = 0;
	osk->popup_hot = -1;
	if (osk->visible)
		osk_render(osk);
}

/* lay `n` already-filled popup_keys into a row over `src`, clamp, show */
static void
osk_popup_show(struct ng_osk *osk, struct key *src, int n, int mode)
{
	osk->popup_n = n;
	osk->popup_mode = mode;

	int cw = src->box.width, ch = src->box.height;
	int gap = cw / 12;
	int total = n * cw + (n - 1) * gap;
	int x = src->box.x + cw / 2 - total / 2;
	int y = src->box.y - ch - gap;
	if (x < osk->area.x)
		x = osk->area.x;
	if (x + total > osk->area.x + osk->area.width)
		x = osk->area.x + osk->area.width - total;
	if (y < osk->area.y) /* no room above -> drop below the key */
		y = src->box.y + ch + gap;
	osk->popup_area = (struct wlr_box){x, y, total, ch};
	for (int i = 0; i < n; i++)
		osk->popup_keys[i].box = (struct wlr_box){x + i * (cw + gap), y, cw, ch};

	/* pick the cell nearest the finger as the initial selection */
	osk->popup_hot = 0;
	for (int i = 0; i < n; i++)
		if (osk->press_lx >= osk->popup_keys[i].box.x)
			osk->popup_hot = i;

	osk_key_up(osk); /* stop any repeat on the held key */
	osk->popup = true;
	osk_render(osk);
}

static void
osk_open_char_popup(struct ng_osk *osk, struct key *src)
{
	const char *alts = key_alts(osk->layer, src);
	if (!alts)
		return;
	uint16_t code[10];
	int n = alts_resolve(alts, code, osk->popup_lbl, 10);
	if (n < 1)
		return;
	for (int i = 0; i < n; i++)
		osk->popup_keys[i] = (struct key){osk->popup_lbl[i], KK_CHAR, code[i], ACC_GROUP, false, 1.0f, {0}};
	osk_popup_show(osk, src, n, 0);
}

static void
osk_open_layout_popup(struct ng_osk *osk, struct key *src)
{
	static const char *LBL[4] = {"EN", "RU", "?12", ":)"};
	static const enum layer LY[4] = {LY_EN, LY_RU, LY_SYM, LY_EMOJI};
	for (int i = 0; i < 4; i++) {
		snprintf(osk->popup_lbl[i], sizeof(osk->popup_lbl[i]), "%s", LBL[i]);
		osk->popup_keys[i] = (struct key){osk->popup_lbl[i], KK_LANG, 0, 0, false, 1.0f, {0}};
		osk->popup_layer[i] = LY[i];
	}
	osk_popup_show(osk, src, 4, 1);
}

static int
osk_hold_cb(void *data)
{
	struct ng_osk *osk = data;
	struct key *k = osk->hold_key;
	osk->hold_key = NULL;
	if (!osk->visible || !k)
		return 0;
	if (k->kind == KK_SPACE) {
		osk->space_cursor = true; /* now a caret trackpad; drag -> arrows */
		osk->space_anchor = osk->press_lx;
	} else if (k->kind == KK_LANG) {
		osk_open_layout_popup(osk, k);
	} else {
		osk_open_char_popup(osk, k);
	}
	return 0;
}

/* arm the long-press timer for a key whose tap acts on release */
static void
osk_arm_hold(struct ng_osk *osk, struct key *k)
{
	osk->hold_key = k;
	if (!osk->hold_timer && osk->server && osk->server->wl_display)
		osk->hold_timer = wl_event_loop_add_timer(wl_display_get_event_loop(osk->server->wl_display),
							 osk_hold_cb, osk);
	if (osk->hold_timer)
		wl_event_source_timer_update(osk->hold_timer, 330);
}

static int
popup_cell_at(struct ng_osk *osk, double lx, double ly)
{
	for (int i = 0; i < osk->popup_n; i++) {
		struct wlr_box b = osk->popup_keys[i].box;
		/* generous vertical band so a sloppy drag still tracks */
		if (lx >= b.x && lx < b.x + b.width && ly >= b.y - b.height && ly < b.y + 2 * b.height)
			return i;
	}
	return -1;
}

void
ng_osk_motion(struct ng_osk *osk, double lx, double ly)
{
	if (!osk || !osk->visible)
		return;
	if (osk->popup) {
		int hot = popup_cell_at(osk, lx, ly);
		if (hot >= 0 && hot != osk->popup_hot) {
			osk->popup_hot = hot;
			osk_render(osk);
		}
		return;
	}
	if (osk->space_cursor) {
		/* drag the space bar -> nudge the caret one char per ~half-key */
		int step = osk->area.width / 22;
		if (step < 8)
			step = 8;
		while (lx - osk->space_anchor >= step) {
			osk_send(osk, KEY_RIGHT, false, 0);
			osk->space_anchor += step;
		}
		while (osk->space_anchor - lx >= step) {
			osk_send(osk, KEY_LEFT, false, 0);
			osk->space_anchor -= step;
		}
		return;
	}
	/* a firm, mostly-vertical downward swipe anywhere on the keys hides it */
	if (!osk->popup && ly - osk->press_ly > osk->area.height / 3 &&
	    fabs(lx - osk->press_lx) * 2.0 < ly - osk->press_ly) {
		ng_osk_set_visible(osk, false); /* clears hold / pressed / repeat */
		return;
	}

	if (osk->hold_key && osk->hold_key->kind != KK_SPACE) {
		double dx = lx - osk->press_lx, dy = ly - osk->press_ly;
		int slop = osk->hold_key->box.height / 3 + 6;
		if (dx * dx + dy * dy > (double) slop * slop)
			osk_cancel_hold(osk); /* moved off -> not a long-press */
	}
}

bool
ng_osk_press(struct ng_osk *osk, double lx, double ly)
{
	if (!osk || !osk->visible)
		return false;
	if (lx < osk->area.x || lx >= osk->area.x + osk->area.width || ly < osk->area.y ||
	    ly >= osk->area.y + osk->area.height)
		return false;

	osk->press_lx = lx;
	osk->press_ly = ly;

	if (osk->popup) {
		int hit = popup_cell_at(osk, lx, ly);
		if (hit >= 0)
			osk->popup_hot = hit; /* committed on release */
		else
			osk_close_popup(osk);
		return true;
	}

	struct key *k = key_at(osk, lx, ly);
	if (!k)
		return true; /* inside the panel, between keys */

	osk->pressed = k;
	switch (k->kind) {
	case KK_CHAR: {
		bool sh = k->shift || (osk->layer == LY_EN && osk->shift);
		if (key_alts(osk->layer, k))
			osk_arm_hold(osk, k); /* accent-capable: commit on release */
		else
			osk_key_down(osk, k->code, sh, k->group); /* held -> client repeats */
		break;
	}
	case KK_BKSP:
		osk_key_down(osk, KEY_BACKSPACE, false, 0);
		break;
	case KK_SPACE:
		osk_arm_hold(osk, k); /* tap = space (on release); hold = caret trackpad */
		break;
	case KK_ENTER:
		osk_send(osk, KEY_ENTER, false, 0); /* discrete - no repeat on return */
		break;
	case KK_SHIFT: {
		uint32_t t = now_ms();
		if (osk->caps) {
			osk->caps = osk->shift = false;
		} else if (t - osk->last_shift < 400) {
			osk->caps = osk->shift = true; /* double-tap -> caps lock */
		} else {
			osk->shift = !osk->shift;
		}
		osk->last_shift = t;
		break;
	}
	case KK_SYM:
		osk->layer = LY_SYM;
		osk->shift = false;
		break;
	case KK_SYM2:
		osk->layer = LY_SYM2;
		osk->shift = false;
		break;
	case KK_ABC:
		osk->layer = osk->letter;
		break;
	case KK_LANG:
		osk_arm_hold(osk, k); /* tap = EN<->RU toggle (on release); hold = picker */
		break;
	case KK_EMOJI:
		osk->layer = LY_EMOJI;
		break;
	case KK_HIDE:
		break; /* acts on release */
	}
	osk_render(osk);
	return true;
}

void
ng_osk_release(struct ng_osk *osk)
{
	if (!osk)
		return;
	struct key *k = osk->pressed;
	osk->pressed = NULL;
	osk_key_up(osk);

	bool was_held = (osk->hold_key != NULL);
	osk_cancel_hold(osk);

	if (osk->space_cursor) { /* caret trackpad ended - no space emitted */
		osk->space_cursor = false;
		if (osk->visible)
			osk_render(osk);
		return;
	}

	if (osk->popup) {
		if (osk->popup_hot >= 0 && osk->popup_hot < osk->popup_n) {
			if (osk->popup_mode == 1) { /* layout picker */
				enum layer t = osk->popup_layer[osk->popup_hot];
				osk->layer = t;
				if (t == LY_EN || t == LY_RU)
					osk->letter = t;
				osk->shift = false;
			} else { /* accent char */
				struct key *pk = &osk->popup_keys[osk->popup_hot];
				osk_send(osk, pk->code, false, pk->group);
			}
		}
		osk_close_popup(osk);
		return;
	}
	if (was_held && k) {
		/* released before the hold fired -> treat as a normal tap */
		if (k->kind == KK_CHAR) {
			bool sh = k->shift || (osk->layer == LY_EN && osk->shift);
			osk_send(osk, k->code, sh, k->group);
		} else if (k->kind == KK_SPACE) {
			osk_send(osk, KEY_SPACE, false, 0);
		} else if (k->kind == KK_LANG) {
			osk->letter = (osk->letter == LY_EN) ? LY_RU : LY_EN;
			osk->layer = osk->letter;
			osk->shift = false;
		}
	}

	if (k && k->kind == KK_CHAR && osk->shift && !osk->caps && osk->layer == LY_EN)
		osk->shift = false; /* one-shot shift consumed */
	if (k && k->kind == KK_HIDE) {
		ng_osk_set_visible(osk, false);
		return;
	}
	if (osk->visible)
		osk_render(osk);
}

bool
ng_osk_tap(struct ng_osk *osk, double lx, double ly)
{
	if (!ng_osk_press(osk, lx, ly))
		return false;
	ng_osk_release(osk);
	return true;
}
