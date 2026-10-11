/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	km_main.c
 *	キー配列: the keyboard's layout shown, typed on, and chosen
 *	(include/ts/keymap.h, design 11.15)
 *
 *	A window with the keyboard drawn in it -- the Japanese one, the
 *	English one as the machine's settings say, or the TRON keyboard --
 *	each key written with what it types in the layout in use: the
 *	latin letters with or without Shift as it is held, or the kana. The
 *	window never has the keys (WM_ATTR_NOFOCUS): a press on it leaves
 *	them with the window that had them, and a drawn key pressed is
 *	typed there (wm_key_put). A modifier or a thumb key pressed on the
 *	picture holds for the next key only. The keys held on the keyboard
 *	are lit, as the window is told every key (OB_N_KEYSEEN).
 *
 *	Above the keyboard the latin layout, the kana layout, the shape of
 *	the keyboard and what the keys are written with are chosen by
 *	buttons, each turning to the next; the menu of the second button
 *	chooses them by name, a mark on those in use. 付け替え gives a key
 *	another key's number: the key pressed on the picture first, then the
 *	one it is to be (the same key again gives it back its own); the keys
 *	given others are listed under the keyboard, each with a mark that
 *	gives it back. The layouts and the keys given others are the
 *	machine's settings, read and set with km_get and km_set; the shape
 *	and what the keys are written with last while the window is open.
 *
 *	What is done is said on the console ("keymap: ..."), which the tests
 *	read. Started with "selftest", the program checks the calls it is
 *	made of and ends.
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <ts/ui.h>
#include <ts/sysdef.h>
#include <ts/hid.h>
#include <ts/wm.h>
#include <ts/keymap.h>

#define TICK_MS		20
#define LOOK_MS		1000		/* the settings looked at again, for a change made elsewhere */

/* ---------------------------------------------------------------- the keyboards */

typedef struct {
	UB	code;			/* the key's number; KB_GAP a space, KB_ROW the end of a row */
	UB	w;			/* its width in quarters of a key */
} KBKEY;

#define KB_GAP		0
#define KB_ROW		0xFF

static CONST KBKEY	kb_jp[] = {
	{ 0x35, 4 }, { 0x1E, 4 }, { 0x1F, 4 }, { 0x20, 4 }, { 0x21, 4 }, { 0x22, 4 }, { 0x23, 4 },
	{ 0x24, 4 }, { 0x25, 4 }, { 0x26, 4 }, { 0x27, 4 }, { 0x2D, 4 }, { 0x2E, 4 }, { 0x89, 4 },
	{ 0x2A, 4 }, { KB_ROW, 0 },
	{ 0x2B, 6 }, { 0x14, 4 }, { 0x1A, 4 }, { 0x08, 4 }, { 0x15, 4 }, { 0x17, 4 }, { 0x1C, 4 },
	{ 0x18, 4 }, { 0x0C, 4 }, { 0x12, 4 }, { 0x13, 4 }, { 0x2F, 4 }, { 0x30, 4 }, { 0x28, 6 },
	{ KB_ROW, 0 },
	{ 0x39, 7 }, { 0x04, 4 }, { 0x16, 4 }, { 0x07, 4 }, { 0x09, 4 }, { 0x0A, 4 }, { 0x0B, 4 },
	{ 0x0D, 4 }, { 0x0E, 4 }, { 0x0F, 4 }, { 0x33, 4 }, { 0x34, 4 }, { 0x32, 4 }, { KB_ROW, 0 },
	{ 0xE1, 9 }, { 0x1D, 4 }, { 0x1B, 4 }, { 0x06, 4 }, { 0x19, 4 }, { 0x05, 4 }, { 0x11, 4 },
	{ 0x10, 4 }, { 0x36, 4 }, { 0x37, 4 }, { 0x38, 4 }, { 0x87, 4 }, { 0xE5, 7 }, { KB_ROW, 0 },
	{ 0xE0, 6 }, { 0xE3, 5 }, { 0xE2, 5 }, { 0x8B, 5 }, { 0x2C, 16 }, { 0x8A, 5 }, { 0x88, 5 },
	{ 0xE6, 5 }, { 0x65, 4 }, { 0xE4, 4 }, { KB_ROW, 0 },
};

static CONST KBKEY	kb_us[] = {
	{ 0x35, 4 }, { 0x1E, 4 }, { 0x1F, 4 }, { 0x20, 4 }, { 0x21, 4 }, { 0x22, 4 }, { 0x23, 4 },
	{ 0x24, 4 }, { 0x25, 4 }, { 0x26, 4 }, { 0x27, 4 }, { 0x2D, 4 }, { 0x2E, 4 }, { 0x2A, 8 },
	{ KB_ROW, 0 },
	{ 0x2B, 6 }, { 0x14, 4 }, { 0x1A, 4 }, { 0x08, 4 }, { 0x15, 4 }, { 0x17, 4 }, { 0x1C, 4 },
	{ 0x18, 4 }, { 0x0C, 4 }, { 0x12, 4 }, { 0x13, 4 }, { 0x2F, 4 }, { 0x30, 4 }, { 0x31, 6 },
	{ KB_ROW, 0 },
	{ 0x39, 7 }, { 0x04, 4 }, { 0x16, 4 }, { 0x07, 4 }, { 0x09, 4 }, { 0x0A, 4 }, { 0x0B, 4 },
	{ 0x0D, 4 }, { 0x0E, 4 }, { 0x0F, 4 }, { 0x33, 4 }, { 0x34, 4 }, { 0x28, 9 }, { KB_ROW, 0 },
	{ 0xE1, 9 }, { 0x1D, 4 }, { 0x1B, 4 }, { 0x06, 4 }, { 0x19, 4 }, { 0x05, 4 }, { 0x11, 4 },
	{ 0x10, 4 }, { 0x36, 4 }, { 0x37, 4 }, { 0x38, 4 }, { 0xE5, 11 }, { KB_ROW, 0 },
	{ 0xE0, 6 }, { 0xE3, 5 }, { 0xE2, 5 }, { 0x2C, 25 }, { 0xE6, 5 }, { 0xE7, 5 }, { 0x65, 4 },
	{ 0xE4, 5 }, { KB_ROW, 0 },
};

/*
 * The TRON keyboard (μTRON): every key of one size, in two halves apart,
 * the letters Dvorak's in the places of the keys under them on an
 * ordinary keyboard, so that the key numbers are those of the same
 * places; the thumbs have Shift (red on the left, blue on the right),
 * 逆変換 and 変換, the space, 一字消 and the arrows. Places in pixels from
 * the keyboard's corner; a key with a name of its own is written with
 * it, and one of number 0 is written only, its key not being known to
 * type anything here.
 */
typedef struct {
	UB		code;
	H		x, y, w, h;
	CONST char	*name;		/* NULL: what it types */
} KBPLACE;

#define TP		36		/* the pitch of the TRON keyboard's keys */
#define TH		33		/* and the height of its rows */
#define TR		( 8 * TP + 24 )	/* where its right half begins */

static CONST KBPLACE	kb_tron[] = {
	/* left half */
	{ 0x29, 0 * TP, 0, TP, TH, "Esc" },
	{ 0x1E, 1 * TP, 0, TP, TH, NULL },
	{ 0x1F, 2 * TP, 0, TP, TH, NULL },
	{ 0x20, 3 * TP, 0, TP, TH, NULL },
	{ 0x21, 4 * TP, 0, TP, TH, NULL },
	{ 0x22, 5 * TP, 0, TP, TH, NULL },
	{ 0x53, 0 * TP, TH, TP, TH, "NumLk" },
	{ 0x14, 1 * TP, TH, TP, TH, NULL },
	{ 0x1A, 2 * TP, TH, TP, TH, NULL },
	{ 0x08, 3 * TP, TH, TP, TH, NULL },
	{ 0x15, 4 * TP, TH, TP, TH, NULL },
	{ 0x17, 5 * TP, TH, TP, TH, NULL },
	{ 0x00, 7 * TP, TH, TP, TH, "×" },
	{ 0x49, 0 * TP, 2 * TH, TP, TH, "INS" },
	{ 0xE2, 1 * TP, 2 * TH, TP, TH, "拡張" },
	{ 0x04, 2 * TP, 2 * TH, TP, TH, NULL },
	{ 0x16, 3 * TP, 2 * TH, TP, TH, NULL },
	{ 0x07, 4 * TP, 2 * TH, TP, TH, NULL },
	{ 0x09, 5 * TP, 2 * TH, TP, TH, NULL },
	{ 0x0A, 6 * TP, 2 * TH, TP, TH, NULL },
	{ 0x2B, 7 * TP, 2 * TH, TP, TH, "tab" },
	{ 0xE0, 1 * TP, 3 * TH, TP, TH, "命令" },
	{ 0x1D, 2 * TP, 3 * TH, TP, TH, NULL },
	{ 0x1B, 3 * TP, 3 * TH, TP, TH, NULL },
	{ 0x06, 4 * TP, 3 * TH, TP, TH, NULL },
	{ 0x19, 5 * TP, 3 * TH, TP, TH, NULL },
	{ 0x05, 6 * TP, 3 * TH, TP, TH, NULL },
	{ 0x52, 2 * TP, 4 * TH, TP, TH, "↑" },
	{ 0x2C, 4 * TP, 4 * TH, TP, TH, "空白" },
	{ 0xE1, 5 * TP, 4 * TH, TP, TH, "Shift" },
	{ 0x8B, 6 * TP, 4 * TH, TP, TH, "逆変換" },
	{ 0x00, 0 * TP, 5 * TH, TP, TH, "Fn" },
	{ 0x50, 1 * TP, 5 * TH, TP, TH, "←" },
	{ 0x51, 2 * TP, 5 * TH, TP, TH, "↓" },
	{ 0x4F, 3 * TP, 5 * TH, TP, TH, "→" },
	{ 0x39, 6 * TP, 5 * TH, TP, TH, "英語" },
	/* right half */
	{ 0x23, TR + 1 * TP, 0, TP, TH, NULL },
	{ 0x24, TR + 2 * TP, 0, TP, TH, NULL },
	{ 0x25, TR + 3 * TP, 0, TP, TH, NULL },
	{ 0x26, TR + 4 * TP, 0, TP, TH, NULL },
	{ 0x27, TR + 5 * TP, 0, TP, TH, NULL },
	{ 0x35, TR + 7 * TP, 0, TP, TH, "半/全" },
	{ 0x00, TR + 0 * TP, TH, TP, TH, "○" },
	{ 0x1C, TR + 1 * TP, TH, TP, TH, NULL },
	{ 0x18, TR + 2 * TP, TH, TP, TH, NULL },
	{ 0x0C, TR + 3 * TP, TH, TP, TH, NULL },
	{ 0x12, TR + 4 * TP, TH, TP, TH, NULL },
	{ 0x13, TR + 5 * TP, TH, TP, TH, NULL },
	{ 0x28, TR + 0 * TP, 2 * TH, TP, TH, "改行" },
	{ 0x0B, TR + 1 * TP, 2 * TH, TP, TH, NULL },
	{ 0x0D, TR + 2 * TP, 2 * TH, TP, TH, NULL },
	{ 0x0E, TR + 3 * TP, 2 * TH, TP, TH, NULL },
	{ 0x0F, TR + 4 * TP, 2 * TH, TP, TH, NULL },
	{ 0x33, TR + 5 * TP, 2 * TH, TP, TH, NULL },
	{ 0xE6, TR + 6 * TP, 2 * TH, TP, TH, "拡張" },
	{ 0x4C, TR + 7 * TP, 2 * TH, TP, TH, "削除" },
	{ 0x11, TR + 1 * TP, 3 * TH, TP, TH, NULL },
	{ 0x10, TR + 2 * TP, 3 * TH, TP, TH, NULL },
	{ 0x36, TR + 3 * TP, 3 * TH, TP, TH, NULL },
	{ 0x37, TR + 4 * TP, 3 * TH, TP, TH, NULL },
	{ 0x38, TR + 5 * TP, 3 * TH, TP, TH, NULL },
	{ 0xE4, TR + 6 * TP, 3 * TH, TP, TH, "命令" },
	{ 0x8A, TR + 1 * TP, 4 * TH, TP, TH, "変換" },
	{ 0xE5, TR + 2 * TP, 4 * TH, TP, TH, "Shift" },
	{ 0x2A, TR + 3 * TP, 4 * TH, TP, TH, "一字消" },
	{ 0x52, TR + 6 * TP, 4 * TH, TP, TH, "↑" },
	{ 0x88, TR + 1 * TP, 5 * TH, TP, TH, "日本語" },
	{ 0x50, TR + 5 * TP, 5 * TH, TP, TH, "←" },
	{ 0x51, TR + 6 * TP, 5 * TH, TP, TH, "↓" },
	{ 0x4F, TR + 7 * TP, 5 * TH, TP, TH, "→" },
	{ 0, 0, 0, 0, 0, NULL }
};

/* The keyboard's shapes */
#define KB_SHAPE_JP	0
#define KB_SHAPE_US	1
#define KB_SHAPE_TRON	2
#define KB_NSHAPE	3

/* ---------------------------------------------------------------- the window */

#define KB_Q		10		/* a quarter of a key, in pixels */
#define KB_M		8		/* the margin */
#define KB_TOP		66		/* where the keyboard starts */
#define KB_RH		40		/* a row's height */
#define KB_LIST		( KB_TOP + 5 * KB_RH + 14 )	/* the keys given others */
#define KB_LH		20		/* a line of the list */
#define KB_W		( KB_M * 2 + 60 * KB_Q )
#define KB_LCOL		3		/* the list's columns */
#define KB_LW		( 60 * KB_Q / KB_LCOL )
#define KB_H		( KB_LIST + ( KM_MAP_MAX / KB_LCOL ) * KB_LH + KB_M + 4 )

#define KB_B_LATIN	0		/* the buttons above the keyboard */
#define KB_B_KANA	1
#define KB_B_SHAPE	2
#define KB_B_VIEW	3
#define KB_B_PICK	4
#define KB_NB		5

static CONST T_DPRECT	kb_btn[KB_NB] = {
	{ KB_M, 8, KB_M + 170, 34 },
	{ KB_M + 176, 8, KB_M + 326, 34 },
	{ KB_M + 332, 8, KB_M + 422, 34 },
	{ KB_M + 428, 8, KB_M + 508, 34 },
	{ KB_M + 514, 8, KB_M + 600, 34 },
};

#define C_GROUND	0x00E8E8E8U
#define C_KEY		0x00FFFFFFU
#define C_EDGE		0x00808080U
#define C_TEXT		0x00000000U
#define C_DIM		0x00909090U
#define C_HELD		0x0090C8FFU
#define C_LATCH		0x00FFE070U
#define C_PICK		0x00FFB060U
#define C_NOTE		0x00C05000U
#define C_RED		0x00D00000U
#define C_BLUE		0x000040D0U

#define M_SHIFT		( HID_MOD_LSHIFT | HID_MOD_RSHIFT )
#define M_CTRL		( HID_MOD_LCTRL | HID_MOD_RCTRL )

static TS_UUID		g_ch, g_win;
static ID		g_port = 0, g_kw = 0;
static INT		g_gid = -1;
static BOOL		g_closing = FALSE;
static BOOL		g_dirty = TRUE;

static T_KMSTATE	g_st;			/* the layouts and the keys given others, as last read */
static UB		g_down[32];		/* the keys held, a bit each */
static UINT		g_mods = 0;		/* the modifiers held, as last told */
static UINT		g_latch = 0;		/* modifiers pressed on the picture, for the next key */
static BOOL		g_kana_view = FALSE;
static INT		g_pick = 0;		/* 付け替え: 0 not, 1 the key to give another, 2 the one it becomes */
static UB		g_from = 0;
static CONST char	*g_said = NULL;		/* the line under the buttons */
static INT		g_shape = -1;		/* KB_SHAPE_*; -1 as the machine's settings say */

/* ---------------------------------------------------------------- words */

/* A line to the console, which the tests read */
static void say( CONST char *fmt, ... )
{
	char	s[160];
	va_list	ap;
	INT	n;

	n = snprintf(s, sizeof(s), "keymap: ");
	va_start(ap, fmt);
	vsnprintf(s + n, sizeof(s) - (size_t)n, fmt, ap);
	va_end(ap);
	strncat(s, "\n", sizeof(s) - strlen(s) - 1);
	tm_putstring((CONST UB *)s);
}

static BOOL same_uuid( CONST TS_UUID *a, CONST TS_UUID *b )
{
	return (BOOL)( memcmp(a, b, sizeof(*a)) == 0 );
}

/* ---------------------------------------------------------------- the settings */

static void state_read( void )
{
	T_KMSTATE	s;

	if ( km_get(&s) >= E_OK ) {
		if ( memcmp(&s, &g_st, sizeof(s)) != 0 ) g_dirty = TRUE;
		g_st = s;
	}
}

static INT shape( void )
{
	if ( g_shape >= 0 ) return g_shape;
	return g_st.us ? KB_SHAPE_US : KB_SHAPE_JP;
}

/* A key's number as the keys given others make it */
static UINT remap( UINT code )
{
	INT	i;

	for ( i = 0; i < g_st.nmap; i++ ) {
		if ( g_st.from[i] == code ) return g_st.to[i];
	}
	return code;
}

/* A thumb of a thumb-shift kana layout pressed on the picture: its mark, or 0 */
static UINT thumb_of( UINT code )
{
	if ( g_st.kana != KM_KANA_TRON && g_st.kana != KM_KANA_NICOLA ) return 0;
	return ( code == KM_THUMB_L_KEY ) ? KM_MOD_THUMB_L : ( code == KM_THUMB_R_KEY ) ? KM_MOD_THUMB_R : 0;
}

static BOOL held( UINT code )
{
	return (BOOL)( code < 256 && ( g_down[code >> 3] & ( 1U << ( code & 7 ) ) ) != 0 );
}

/* ---------------------------------------------------------------- drawing */

static void fill( CONST T_DPRECT *r, UW c )
{
	(void)dp_fill_rect(g_gid, r, c);
}

static void frame( CONST T_DPRECT *r, UW c )
{
	(void)dp_frame_rect(g_gid, r, c, 1);
}

static INT text_w( CONST char *s, INT px )
{
	return dp_text_width((CONST UB *)s, px);
}

static void text( INT x, INT y, CONST char *s, UW c, INT px )
{
	(void)dp_text(g_gid, x, y, (CONST UB *)s, c, px);
}

/* Words in the middle of a box */
static void centre( CONST T_DPRECT *r, CONST char *s, UW c, INT px, INT dy )
{
	INT	w = text_w(s, px);

	text(( r->left + r->right - w ) / 2, ( r->top + r->bottom ) / 2 + px / 3 + dy, s, c, px);
}

/* What a key is written with: what it types now, or its name */
static CONST char *key_label( UINT code, char *buf, INT max )
{
	UINT	mods = g_mods | g_latch;
	BOOL	shift = (BOOL)( ( mods & M_SHIFT ) != 0 );

	if ( g_kana_view ) {
		if ( g_st.kana == KM_KANA_JIS ) {
			if ( km_label(KM_L_JIS, code, shift ? HID_MOD_LSHIFT : 0, buf, max) > 0 ) return buf;
		} else if ( km_label(KM_L_KANA, code, mods, buf, max) > 0 ) {
			return buf;
		}
	}
	if ( code < 0x28 || ( code >= 0x2D && code <= 0x38 && code != 0x35 ) || code == 0x87 || code == 0x89 ) {
		if ( km_label(KM_L_CHAR, code, shift ? HID_MOD_LSHIFT : 0, buf, max) > 0 ) return buf;
	}
	/* the long names shortened to fit a key */
	if ( code == 0x35 && !g_st.us ) return "半/全";
	if ( code == 0x88 ) return "かな";
	if ( km_label(KM_L_NAME, code, 0, buf, max) <= 0 ) buf[0] = 0;
	return buf;
}

/* Each key's place, called for each with its box */
typedef void (*KBEACH)( CONST KBKEY *k, CONST T_DPRECT *r, void *arg );

static void kb_each( KBEACH f, void *arg )
{
	CONST KBKEY	*k = ( shape() == KB_SHAPE_US ) ? kb_us : kb_jp;
	INT		x = KB_M, y = KB_TOP;

	if ( shape() == KB_SHAPE_TRON ) {
		CONST KBPLACE	*t;

		for ( t = kb_tron; t->w != 0; t++ ) {
			KBKEY		one;
			T_DPRECT	r;

			one.code = t->code;
			one.w = (UB)( t - kb_tron );		/* its place in the table, for its name */
			r.left = KB_M + 4 + t->x + 1;
			r.top = KB_TOP + t->y + 1;
			r.right = KB_M + 4 + t->x + t->w - 2;
			r.bottom = KB_TOP + t->y + t->h - 2;
			f(&one, &r, arg);
		}
		return;
	}

	for ( ; ; k++ ) {
		if ( k->code == KB_ROW ) {
			x = KB_M;
			y += KB_RH;
			if ( y >= KB_TOP + 5 * KB_RH ) break;
			continue;
		}
		if ( k->code != KB_GAP ) {
			T_DPRECT	r;

			r.left = x + 1;
			r.top = y + 1;
			r.right = x + k->w * KB_Q - 2;
			r.bottom = y + KB_RH - 3;
			f(k, &r, arg);
		}
		x += k->w * KB_Q;
	}
}

static void draw_key( CONST KBKEY *k, CONST T_DPRECT *r, void *arg )
{
	UINT		to = remap(k->code);
	UW		c = C_KEY;
	char		b[64];
	CONST char	*s;
	INT		px;

	(void)arg;
	if ( g_pick == 2 && k->code == g_from ) {
		c = C_PICK;
	} else if ( held(to) || held(k->code) ) {
		c = C_HELD;
	} else if ( to >= 0xE0 && to <= 0xE7 && ( g_latch & ( 1U << ( to - 0xE0 ) ) ) != 0 ) {
		c = C_LATCH;
	} else if ( thumb_of(to) != 0 && ( ( g_latch | g_mods ) & thumb_of(to) ) != 0 ) {
		c = ( g_mods & thumb_of(to) ) ? C_HELD : C_LATCH;
	}
	fill(r, c);
	frame(r, C_EDGE);
	if ( to != k->code ) {
		/* given another: its own name, and what it is now under it */
		char	own[32], note[40];

		if ( km_label(KM_L_NAME, k->code, 0, own, sizeof(own)) <= 0 ) own[0] = 0;
		if ( km_label(KM_L_NAME, to, 0, b, sizeof(b)) <= 0 ) b[0] = 0;
		snprintf(note, sizeof(note), "→%s", b);
		centre(r, own, C_TEXT, 11, -7);
		centre(r, note, C_NOTE, 10, 8);
		return;
	}
	s = key_label(k->code, b, sizeof(b));
	if ( shape() == KB_SHAPE_TRON && kb_tron[k->w].name != NULL ) {
		s = kb_tron[k->w].name;
	}
	px = ( s[0] != 0 && s[1] == 0 ) ? 16 : 12;
	/* a name wider than the key, smaller */
	while ( px > 8 && text_w(s, px) > r->right - r->left - 4 ) px--;
	/* the TRON keyboard's Shift keys in their colours */
	if ( shape() == KB_SHAPE_TRON && ( k->code == 0xE1 || k->code == 0xE5 ) ) {
		centre(r, s, ( k->code == 0xE1 ) ? C_RED : C_BLUE, px, 0);
		return;
	}
	centre(r, s, ( k->code == 0 ) ? C_DIM : C_TEXT, px, 0);
}

static CONST char *latin_name( void )
{
	return ( g_st.latin == KM_LATIN_DVORAK ) ? "英字: Dvorak" : "英字: キーの刻印どおり";
}

static CONST char *kana_name( void )
{
	switch ( g_st.kana ) {
	case KM_KANA_TRON:	return "かな: TRONかな配列";
	case KM_KANA_NICOLA:	return "かな: 親指シフト";
	default:		return "かな: JISかな";
	}
}

static void paint( void )
{
	T_DPRECT	all;
	CONST char	*lb[KB_NB];
	INT		i;

	if ( g_gid < 0 ) return;
	all.left = 0;
	all.top = 0;
	all.right = KB_W;
	all.bottom = KB_H;
	fill(&all, C_GROUND);

	lb[KB_B_LATIN] = latin_name();
	lb[KB_B_KANA] = kana_name();
	lb[KB_B_SHAPE] = ( shape() == KB_SHAPE_TRON ) ? "形: TRON" : ( shape() == KB_SHAPE_US ) ? "形: 英語" : "形: 日本語";
	lb[KB_B_VIEW] = g_kana_view ? "表示: かな" : "表示: 英字";
	lb[KB_B_PICK] = ( g_pick != 0 ) ? "付け替えをやめる" : "付け替え";
	for ( i = 0; i < KB_NB; i++ ) {
		fill(&kb_btn[i], ( i == KB_B_PICK && g_pick != 0 ) ? C_PICK : C_KEY);
		frame(&kb_btn[i], C_EDGE);
		centre(&kb_btn[i], lb[i], C_TEXT, 13, 0);
	}
	if ( g_said != NULL ) {
		text(KB_M, 56, g_said, C_NOTE, 13);
	}

	kb_each(draw_key, NULL);

	text(KB_M, KB_LIST - 2, ( g_st.nmap == 0 ) ? "付け替えたキーはありません"
						   : "付け替えたキー(× で元に戻す)", C_DIM, 12);
	for ( i = 0; i < g_st.nmap; i++ ) {
		INT		x = KB_M + ( i % KB_LCOL ) * KB_LW, y = KB_LIST + 4 + ( i / KB_LCOL ) * KB_LH;
		char		a[32], b[32], line[72];
		T_DPRECT	xr;

		if ( km_label(KM_L_NAME, g_st.from[i], 0, a, sizeof(a)) <= 0 ) a[0] = 0;
		if ( km_label(KM_L_NAME, g_st.to[i], 0, b, sizeof(b)) <= 0 ) b[0] = 0;
		snprintf(line, sizeof(line), "%s → %s", a, b);
		xr.left = x;
		xr.top = y;
		xr.right = x + 18;
		xr.bottom = y + 18;
		frame(&xr, C_EDGE);
		centre(&xr, "×", C_TEXT, 12, 0);
		text(x + 24, y + 14, line, C_TEXT, 13);
	}
	(void)wm_obj_flush(g_kw, NULL);
	g_dirty = FALSE;
}

/* ---------------------------------------------------------------- what is pressed */

typedef struct {
	INT	x, y;
	UINT	code;
} KBHIT;

static void hit_key( CONST KBKEY *k, CONST T_DPRECT *r, void *arg )
{
	KBHIT	*h = (KBHIT *)arg;

	if ( h->x >= r->left && h->x < r->right && h->y >= r->top && h->y < r->bottom ) {
		h->code = k->code;
	}
}

static UINT key_at( INT x, INT y )
{
	KBHIT	h;

	h.x = x;
	h.y = y;
	h.code = 0;
	kb_each(hit_key, &h);
	return h.code;
}

/* What the machine's settings could not take, on the line under the buttons */
static void refused( ER er )
{
	static char	s[64];

	snprintf(s, sizeof(s), ( er == E_OACV ) ? "キー配列を変える権限がありません"
						 : "キー配列を変えられません(エラー %d)", (INT)er);
	g_said = s;
	say("refused %d", (INT)er);
}

/* A key given another number, or given its own back, and kept; FALSE, said why, when it was not */
static BOOL map_change( UB from, UB to )
{
	T_KMSTATE	s = g_st;
	INT		i, k = 0;
	ER		er;

	for ( i = 0; i < g_st.nmap; i++ ) {
		if ( g_st.from[i] != from ) {
			s.from[k] = g_st.from[i];
			s.to[k] = g_st.to[i];
			k++;
		}
	}
	if ( to != from ) {
		if ( k >= KM_MAP_MAX ) {
			g_said = "これ以上キーを付け替えられません";
			return FALSE;
		}
		s.from[k] = from;
		s.to[k] = to;
		k++;
	}
	s.nmap = k;
	er = km_set(&s, KM_SET_MAP);
	if ( er < E_OK ) {
		refused(er);
		return FALSE;
	}
	state_read();
	if ( to != from ) say("map %02x to %02x", from, to);
	else say("map %02x back", from);
	return TRUE;
}

static void map_reset( void )
{
	T_KMSTATE	s = g_st;
	ER		er;

	s.nmap = 0;
	er = km_set(&s, KM_SET_MAP);
	if ( er < E_OK ) {
		refused(er);
		return;
	}
	state_read();
	say("map reset");
}

static void set_latin( INT v )
{
	T_KMSTATE	s = g_st;
	ER		er;

	s.latin = v;
	er = km_set(&s, KM_SET_LATIN);
	if ( er < E_OK ) {
		refused(er);
		return;
	}
	state_read();
	g_said = NULL;
	say("latin %d", v);
}

static void set_kana( INT v )
{
	T_KMSTATE	s = g_st;
	ER		er;

	s.kana = v;
	er = km_set(&s, KM_SET_KANA);
	if ( er < E_OK ) {
		refused(er);
		return;
	}
	state_read();
	g_latch &= ~( KM_MOD_THUMB_L | KM_MOD_THUMB_R );
	g_said = ( v == KM_KANA_TRON ) ? "左 Shift(赤)・右 Shift(青)か、無変換・変換と同時に押す"
	       : ( v == KM_KANA_NICOLA ) ? "親指は 無変換(左)と 変換(右)のキー" : NULL;
	say("kana %d", v);
}

static void set_shape( INT v )
{
	g_shape = v;
	say("shape %d", v);
}

static void set_view( BOOL kana )
{
	g_kana_view = kana;
	say("view %s", kana ? "kana" : "latin");
}

static void set_pick( BOOL on )
{
	g_pick = on ? 1 : 0;
	g_said = on ? "付け替えるキーを押してください" : NULL;
	say("pick %s", on ? "on" : "off");
}

/* A key pressed on the picture, typed into the window that has the keys */
static void type_key( UINT code )
{
	UINT	to = remap(code);
	ER	er;

	if ( to >= 0xE0 && to <= 0xE7 ) {
		g_latch ^= 1U << ( to - 0xE0 );		/* for the next key */
		say("latch %x", g_latch);
		return;
	}
	if ( thumb_of(to) != 0 ) {
		g_latch ^= thumb_of(to);		/* the next key, with that thumb */
		say("latch %x", g_latch);
		return;
	}
	er = wm_key_put(g_kw, to, g_latch);
	if ( er < E_OK ) {
		say("not typed %02x (%d)", to, (INT)er);
	} else {
		say("typed %02x mods %x", to, g_latch);
	}
	g_latch = 0;
}

static void press( INT x, INT y )
{
	INT	i;
	UINT	code;

	for ( i = 0; i < KB_NB; i++ ) {
		CONST T_DPRECT	*r = &kb_btn[i];

		if ( x < r->left || x >= r->right || y < r->top || y >= r->bottom ) continue;
		switch ( i ) {
		case KB_B_LATIN:	set_latin(( g_st.latin + 1 ) % KM_LATIN_N);	break;
		case KB_B_KANA:		set_kana(( g_st.kana + 1 ) % KM_KANA_N);	break;
		case KB_B_VIEW:		set_view((BOOL)!g_kana_view);			break;
		case KB_B_SHAPE:	set_shape(( shape() + 1 ) % KB_NSHAPE);		break;
		case KB_B_PICK:		set_pick((BOOL)( g_pick == 0 ));		break;
		default:		break;
		}
		g_dirty = TRUE;
		return;
	}
	if ( y >= KB_LIST ) {
		i = ( ( y - KB_LIST - 4 ) / KB_LH ) * KB_LCOL + ( ( x - KB_M ) / KB_LW );
		if ( x >= KB_M && i >= 0 && i < g_st.nmap && ( x - KB_M ) % KB_LW < 18 ) {
			(void)map_change(g_st.from[i], g_st.from[i]);	/* × : its own number back */
			g_dirty = TRUE;
		}
		return;
	}
	code = key_at(x, y);
	if ( code == 0 ) return;
	if ( g_pick == 1 ) {
		g_from = (UB)code;
		g_pick = 2;
		g_said = "何のキーにするか押してください(同じキーで元に戻す)";
		say("pick from %02x", code);
	} else if ( g_pick == 2 ) {
		g_pick = 0;
		if ( map_change(g_from, (UB)code) ) g_said = NULL;
	} else {
		type_key(code);
	}
	g_dirty = TRUE;
}

/* A key of the keyboard, as it came: the keys held lit */
static void seen( CONST T_OBNTM *m )
{
	UINT	c = m->code;

	if ( m->lost != 0 ) {
		memset(g_down, 0, sizeof(g_down));	/* the let goes may be among what was lost */
	}
	if ( c < 256 ) {
		UB	bit = (UB)( 1U << ( c & 7 ) );

		if ( ( m->mods & OB_KEY_UP ) != 0 ) g_down[c >> 3] &= (UB)~bit;
		else g_down[c >> 3] |= bit;
	}
	g_mods = m->mods & ~( OB_KEY_UP | OB_KEY_SEEN );
	g_dirty = TRUE;
}

/* ---------------------------------------------------------------- the menu */

static void command( CONST char *code )
{
	if ( strcmp(code, "close") == 0 ) g_closing = TRUE;
	else if ( strcmp(code, "latin.keys") == 0 ) set_latin(KM_LATIN_KEYS);
	else if ( strcmp(code, "latin.dvorak") == 0 ) set_latin(KM_LATIN_DVORAK);
	else if ( strcmp(code, "kana.jis") == 0 ) set_kana(KM_KANA_JIS);
	else if ( strcmp(code, "kana.tron") == 0 ) set_kana(KM_KANA_TRON);
	else if ( strcmp(code, "kana.nicola") == 0 ) set_kana(KM_KANA_NICOLA);
	else if ( strcmp(code, "shape.jp") == 0 ) set_shape(KB_SHAPE_JP);
	else if ( strcmp(code, "shape.us") == 0 ) set_shape(KB_SHAPE_US);
	else if ( strcmp(code, "shape.tron") == 0 ) set_shape(KB_SHAPE_TRON);
	else if ( strcmp(code, "view.latin") == 0 ) set_view(FALSE);
	else if ( strcmp(code, "view.kana") == 0 ) set_view(TRUE);
	else if ( strcmp(code, "pick") == 0 ) set_pick(TRUE);
	else if ( strcmp(code, "unpick") == 0 ) set_pick(FALSE);
	else if ( strcmp(code, "reset") == 0 ) map_reset();
	g_dirty = TRUE;
}

static void tick( ID mid, CONST char *code, BOOL on )
{
	(void)mn_chg_atr(mid, NULL, code, on ? MN_TICK : 0);
}

static void menu( CONST T_OBNTM *m )
{
	TS_UUID	def;
	T_MNSEL	sel;
	ID	mid;
	BOOL	chosen;

	if ( ui_uuid(SYSDEF_MENU_KEYMAP, &def) < E_OK || mn_cre_men(&def, &mid) < E_OK ) return;
	state_read();
	tick(mid, "latin.keys", g_st.latin == KM_LATIN_KEYS);
	tick(mid, "latin.dvorak", g_st.latin == KM_LATIN_DVORAK);
	tick(mid, "kana.jis", g_st.kana == KM_KANA_JIS);
	tick(mid, "kana.tron", g_st.kana == KM_KANA_TRON);
	tick(mid, "kana.nicola", g_st.kana == KM_KANA_NICOLA);
	tick(mid, "shape.jp", shape() == KB_SHAPE_JP);
	tick(mid, "shape.us", shape() == KB_SHAPE_US);
	tick(mid, "shape.tron", shape() == KB_SHAPE_TRON);
	tick(mid, "view.latin", !g_kana_view);
	tick(mid, "view.kana", g_kana_view);
	(void)mn_chg_atr(mid, NULL, "pick", ( g_pick != 0 ) ? MN_GREY : 0);
	(void)mn_chg_atr(mid, NULL, "unpick", ( g_pick == 0 ) ? MN_GREY : 0);
	(void)mn_chg_atr(mid, NULL, "reset", ( g_st.nmap == 0 ) ? MN_GREY : 0);
	say("menu");
	chosen = (BOOL)( mn_pop_men(mid, g_kw, m->x, m->y, m->when, &sel) >= E_OK );
	(void)mn_del_men(mid);
	if ( chosen ) {
		say("chosen %s", (CONST char *)sel.code);
		command((CONST char *)sel.code);
	}
}

/* ---------------------------------------------------------------- the window */

static void event( CONST T_OBNTM *m )
{
	if ( !same_uuid(&m->uuid, &g_win) ) return;
	switch ( m->event ) {
	case OB_E_CLOSE:
	case OB_E_DELETE:
		g_closing = TRUE;
		break;
	case OB_E_PRESS:
		if ( m->code == 1 ) {
			menu(m);
		} else if ( m->code == 0 && m->x >= 0 && m->y >= 0 && m->x < KB_W && m->y < KB_H ) {
			press(m->x, m->y);
		}
		break;
	case OB_E_KEY:
		if ( ( m->mods & OB_KEY_SEEN ) != 0 ) {
			seen(m);
		} else if ( ( m->mods & OB_KEY_UP ) == 0 && ( m->mods & M_CTRL ) != 0 && m->code == 0x08 ) {
			g_closing = TRUE;			/* Ctrl+E, if ever the keys come here */
		}
		break;
	case OB_E_REDRAW:
		g_dirty = TRUE;
		break;
	default:
		break;
	}
}

/*
 * The window, with the keyboard's size of work area, never having the
 * keys, and asking to be told every key of the keyboard
 */
static ER win_open( INT x, INT y, BOOL plain )
{
	T_OBCRE		c;
	T_OBNTF		req;
	T_OBWPOS	wp;
	SZ		asz = 0;
	char		json[200];

	snprintf(json, sizeof(json), "{\"rect\":[%d,%d,%d,%d],\"attr\":%u,\"icon\":\"%s\"}",
		 x, y, x + KB_W + 16, y + KB_H + 34,
		 (UINT)( WM_ATTR_FRAME | WM_ATTR_TITLE | ( plain ? 0 : WM_ATTR_NOFOCUS ) ), SYSDEF_PROG_KEYMAP);
	memset(&c, 0, sizeof(c));
	c.type = OB_T_WINDOW;
	c.sub = OB_S_WINDOW;
	c.name = (CONST UB *)"キー配列";
	c.json = (CONST UB *)json;
	c.jsonsz = (SZ)strlen(json);
	if ( ob_cre_obj(&c, &g_win) < E_OK ) return E_OBJ;
	g_kw = ob_opn_obj(&g_win, OB_OP_R | OB_OP_W | OB_OP_PROT);
	g_gid = ( g_kw > 0 ) ? wm_obj_gid(g_kw) : E_ID;
	memset(&req, 0, sizeof(req));
	req.events = OB_E_CLOSE | OB_E_PRESS | OB_E_KEY | OB_E_REDRAW | OB_E_DELETE;
	req.id = 7;
	req.flags = OB_N_KEYSEEN;
	if ( g_kw <= 0 || g_gid < 0 || ob_ntf_evt(g_kw, OB_REC_ANY, &req, g_port) <= 0 ) {
		if ( g_kw > 0 ) ob_cls_obj(g_kw);
		(void)ob_del_obj(&g_win);
		g_kw = 0;
		return E_OBJ;
	}
	/* the frame round a work area of the keyboard's size */
	if ( ob_rea_rec(g_kw, OB_WR_PLACE, 0, &wp, sizeof(wp), &asz) >= E_OK ) {
		wp.right = wp.left + KB_W + ( ( wp.right - wp.left ) - ( wp.wright - wp.wleft ) );
		wp.bottom = wp.top + KB_H + ( ( wp.bottom - wp.top ) - ( wp.wbottom - wp.wtop ) );
		(void)ob_wri_rec(g_kw, OB_WR_PLACE, 0, &wp, sizeof(wp), &asz);
	}
	return E_OK;
}

static void win_close( void )
{
	if ( g_kw > 0 ) ob_cls_obj(g_kw);
	g_kw = 0;
	(void)ob_del_obj(&g_win);
}

static ER port_open( void )
{
	T_OBCRE	c;

	memset(&c, 0, sizeof(c));
	c.type = OB_T_CHANNEL;
	if ( ob_cre_obj(&c, &g_ch) < E_OK ) return E_NOMEM;
	g_port = ob_opn_obj(&g_ch, OB_OP_READ | OB_OP_WRITE | OB_O_NOWAIT);
	if ( g_port <= 0 ) {
		(void)ob_del_obj(&g_ch);
		return E_OBJ;
	}
	return E_OK;
}

static void port_close( void )
{
	if ( g_port > 0 ) ob_cls_obj(g_port);
	g_port = 0;
	(void)ob_del_obj(&g_ch);
}

/* ---------------------------------------------------------------- the selftest */

static INT st_fail;

static void st_check( BOOL ok, CONST char *what )
{
	if ( ok ) return;
	st_fail++;
	say("selftest: failed: %s", what);
}

static INT selftest( void )
{
	T_KMSTATE	st, bad;
	char		b[64];
	ER		er;

	say("selftest");
	st_check(km_get(&st) >= E_OK, "km_get");
	st_check(st.latin >= 0 && st.latin < KM_LATIN_N && st.kana >= 0 && st.kana < KM_KANA_N
		 && st.nmap >= 0 && st.nmap <= KM_MAP_MAX, "the state's values");
	st_check(km_get((T_KMSTATE *)8) == E_MACV, "km_get into memory not the process's");
	st_check(km_label(KM_L_NAME, 0x28, 0, b, sizeof(b)) == 5 && strcmp(b, "Enter") == 0, "the name of Enter");
	st_check(km_label(KM_L_NAME, 0x28, 0, b, 3) == 2 && strcmp(b, "En") == 0, "a name cut to the room");
	st_check(km_label(KM_L_JIS, 0x04, 0, b, sizeof(b)) > 0 && strcmp(b, "ち") == 0, "the JIS kana of A");
	st_check(km_label(KM_L_JIS, 0x1D, HID_MOD_LSHIFT, b, sizeof(b)) > 0 && strcmp(b, "っ") == 0,
		 "the small kana of Z");
	st_check(km_label(KM_L_JIS, 0x2C, 0, b, sizeof(b)) == 0, "no kana on the space bar");
	st_check(km_label(KM_L_CHAR, 0x1E, 0, b, sizeof(b)) == 1 && b[0] == '1', "the letter of 1");
	st_check(km_label(KM_L_CHAR, 0x2C, 0, b, sizeof(b)) == 0, "no letter on the space bar");
	st_check(km_label(9, 0x04, 0, b, sizeof(b)) == E_PAR, "an unknown kind of words");
	st_check(km_label(KM_L_NAME, 0x04, 0, (char *)8, 16) == E_MACV, "words into memory not the process's");

	/* set as they are: kept, and read back the same */
	er = km_set(&st, KM_SET_LATIN | KM_SET_KANA | KM_SET_MAP);
	st_check(er == E_OK, "the state set as it is");
	bad = st;
	bad.latin = KM_LATIN_N;
	st_check(km_set(&bad, KM_SET_LATIN) == E_PAR, "a latin layout out of range");
	bad = st;
	bad.nmap = 1;
	bad.from[0] = 0;
	bad.to[0] = 0x04;
	st_check(km_set(&bad, KM_SET_MAP | KM_SET_KANA) == E_PAR, "a key given 0");
	st_check(km_get(&bad) >= E_OK && memcmp(&bad, &st, sizeof(st)) == 0, "nothing set by what was refused");

	/* a window that may have the keys types nothing; this one, only after a press */
	st_check(wm_key_put(0, 0x04, 0) < E_OK, "a key put through no window");
	if ( port_open() >= E_OK ) {
		if ( win_open(40, 40, TRUE) >= E_OK ) {
			st_check(wm_key_put(g_kw, 0x04, 0) == E_OBJ, "a window that may have the keys");
			win_close();
		} else {
			st_check(FALSE, "a plain window");
		}
		if ( win_open(40, 40, FALSE) >= E_OK ) {
			st_check(wm_key_put(g_kw, 0x04, 0) == E_OACV, "a key before any press");
			st_check(wm_key_put(g_kw, 0, 0) == E_PAR, "key 0");
			st_check(wm_key_put(g_kw, 0x04, 0x10000) == E_PAR, "modifiers there are not");
			win_close();
		} else {
			st_check(FALSE, "a window that never has the keys");
		}
		port_close();
	}
	say("selftest %s (%d failed)", ( st_fail == 0 ) ? "ok" : "failed", st_fail);
	return ( st_fail == 0 ) ? 0 : 1;
}

/* ---------------------------------------------------------------- start and end */

int main( void )
{
	T_OBNTM	m;
	SZ	asz = 0;
	char	arg[16];
	INT	since = 0;

	memset(arg, 0, sizeof(arg));
	if ( ts_get_arg(arg, sizeof(arg) - 1) >= 8 && memcmp(arg, "selftest", 8) == 0 ) return selftest();

	if ( port_open() < E_OK ) {
		say("no channel");
		return 2;
	}
	state_read();
	if ( win_open(160, 200, FALSE) < E_OK ) {
		say("no window");
		port_close();
		return 3;
	}
	paint();
	say("ready latin %d kana %d map %d", g_st.latin, g_st.kana, g_st.nmap);

	while ( !g_closing ) {
		while ( !g_closing && ob_rea_rec(g_port, 0, 0, &m, sizeof(m), &asz) >= E_OK ) event(&m);
		if ( g_closing ) break;
		since += TICK_MS;
		if ( since >= LOOK_MS ) {
			since = 0;
			state_read();			/* changed elsewhere: システム環境設定, another of these */
		}
		if ( g_dirty ) paint();
		(void)tk_dly_tsk(TICK_MS);
	}
	win_close();
	port_close();
	say("closed");
	return 0;
}
