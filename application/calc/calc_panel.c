/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	calc_panel.c
 *	基本表計算: the 操作パネル (design 11.13)
 *
 *	The parts that work on the cells are in a small window of their own,
 *	made a subordinate window of the sheet's ("sub" in its metadata):
 *	the window manager keeps it just in front of the sheet's window,
 *	carries it when that window is carried, and raises the two together.
 *	It has no band; its frame carries it. It stands where the object
 *	says (window.panelpos against window.pos), or else over the top edge
 *	of the sheet's window, and the 表示 menu puts it away and brings it
 *	back.
 *
 *	Three rows of parts, all drawn here:
 *
 *	  書体 ▼  文字サイズ ▼  B I U S x² x₂  A[文字色]  ■[地の色]  横の配置 ▼  縦の配置 ▼
 *	  格子 外枠 上 下 左 右 なし  ━[線の色]  線種 ▼  線幅 ▼  集合縦棒 積上縦棒 折れ線 積上折線
 *	  セルの名前  fx  [値と数式の欄]  ✓ ✕
 *
 *	A button does what the same item of the menu does (command()); a
 *	button with ▼, and a colour, opens under it the menu the sheet's own
 *	menu has for it (書体, 文字サイズ, 文字色, セルの背景色, 横の配置,
 *	縦の配置, 線種, 線幅, 線の色). The pictures the system's face has no
 *	letters for -- the frames, the charts, the placings, the check and
 *	the cross -- are drawn as small pictures. The parts show the cell in
 *	hand: its decorations pressed in, its face, size, colours and
 *	placings on the buttons, and the line the next frame is drawn with.
 *
 *	The box in the third row shows what the cell in hand holds (its
 *	formula, or its words), and while a cell is typed into, what is
 *	typed. Pressed, or begun with fx (which puts "=" in front), the cell
 *	is typed into in the box, through かな漢字変換 as in the cell
 *	(calc_edit.c); ✓ or Enter puts it in the cell, ✕ or Esc leaves the
 *	cell as it was.
 *
 *	The panel is drawn into memory of the program's own and laid in its
 *	window in one piece, the letters over it.
 */

#include <stdint.h>
#include "calc.h"
#include <ts/wm.h>

#define PW		520			/* the work area */
#define PH		84
#define ROW_H		27
#define ROW_T(i)	( 2 + ( i ) * ROW_H )
#define PART_H		22

/* What a part is */
#define K_PUSH		0			/* a button: a command */
#define K_DROP		1			/* a button with ▼: a menu */
#define K_COLOUR	2			/* a mark and a colour: a menu */
#define K_CHART		3
#define K_REF		4			/* the cell's name */
#define K_FIELD		5			/* what the cell holds, typed into */
#define K_OK		6
#define K_CANCEL	7

/* The parts, by their place in the table */
#define TP_FACE		0
#define TP_SIZE		1
#define TP_BOLD		2
#define TP_ITALIC	3
#define TP_UNDER		4
#define TP_STRIKE	5
#define TP_SUPER		6
#define TP_SUB		7
#define TP_COLOUR	8
#define TP_GROUND	9
#define TP_ALIGN		10
#define TP_VALIGN	11
#define TP_ALL		12
#define TP_OUTLINE	13
#define TP_TOP		14
#define TP_BOTTOM	15
#define TP_LEFT		16
#define TP_RIGHT		17
#define TP_NOFRAME	18
#define TP_LCOLOUR	19
#define TP_LINE		20
#define TP_LWIDTH	21
#define TP_BAR		22
#define TP_SBAR		23
#define TP_PLOT		24
#define TP_SPLOT		25
#define TP_REF		26
#define TP_FX		27
#define TP_FIELD		28
#define TP_OK		29
#define TP_CANCEL	30
#define TP_N		31

typedef struct {
	INT		kind;
	INT		row;
	INT		l, w;
	CONST char	*code;			/* the command, or the menu's definition */
} PART;

static CONST PART parts[TP_N] = {
	{ K_DROP,   0,   6, 80, SYSDEF_MENU_CALC_FACE },
	{ K_DROP,   0,  90, 45, SYSDEF_MENU_CALC_SIZE },
	{ K_PUSH,   0, 139, 24, "style.bold" },
	{ K_PUSH,   0, 167, 24, "style.italic" },
	{ K_PUSH,   0, 195, 24, "style.underline" },
	{ K_PUSH,   0, 223, 24, "style.strike" },
	{ K_PUSH,   0, 251, 24, "style.super" },
	{ K_PUSH,   0, 279, 24, "style.sub" },
	{ K_COLOUR, 0, 307, 52, SYSDEF_MENU_CALC_COLOUR },
	{ K_COLOUR, 0, 363, 52, SYSDEF_MENU_CALC_GROUND },
	{ K_DROP,   0, 419, 45, SYSDEF_MENU_CALC_ALIGN },
	{ K_DROP,   0, 468, 45, SYSDEF_MENU_CALC_VALIGN },
	{ K_PUSH,   1,   6, 24, "frame.all" },
	{ K_PUSH,   1,  34, 24, "frame.outline" },
	{ K_PUSH,   1,  62, 24, "frame.top" },
	{ K_PUSH,   1,  90, 24, "frame.bottom" },
	{ K_PUSH,   1, 118, 24, "frame.left" },
	{ K_PUSH,   1, 146, 24, "frame.right" },
	{ K_PUSH,   1, 174, 24, "frame.none" },
	{ K_COLOUR, 1, 202, 52, SYSDEF_MENU_CALC_LCOLOUR },
	{ K_DROP,   1, 258, 50, SYSDEF_MENU_CALC_LINE },
	{ K_DROP,   1, 312, 50, SYSDEF_MENU_CALC_LWIDTH },
	{ K_CHART,  1, 366, 28, "chart.bar" },
	{ K_CHART,  1, 398, 28, "chart.sbar" },
	{ K_CHART,  1, 430, 28, "chart.line" },
	{ K_CHART,  1, 462, 28, "chart.sline" },
	{ K_REF,    2,   6, 60, NULL },
	{ K_PUSH,   2,  70, 24, NULL },
	{ K_FIELD,  2,  98, 360, NULL },
	{ K_OK,     2, 462, 24, NULL },
	{ K_CANCEL, 2, 490, 24, NULL },
};

static void part_box( INT k, T_DPRECT *r )
{
	CONST PART	*p = &parts[k];

	r->left = p->l;
	r->right = p->l + p->w;
	if ( p->kind == K_CHART ) {
		r->top = ROW_T(p->row) + 1;
		r->bottom = r->top + 24;
	} else {
		r->top = ROW_T(p->row) + 2;
		r->bottom = r->top + PART_H;
	}
}

/* The part at a place of the work area, or -1 */
static INT part_at( INT x, INT y )
{
	INT		k;
	T_DPRECT	r;

	for ( k = 0; k < TP_N; k++ ) {
		part_box(k, &r);
		if ( x >= r.left && x < r.right && y >= r.top && y < r.bottom ) return k;
	}
	return -1;
}

/* ---------------------------------------------------------------- what the parts show */

typedef struct {
	UINT		has;			/* the cell's decorations, CX_S_* */
	CONST char	*face;			/* as the button says it */
	INT		size;
	UW		colour, ground;
	char		align[16], valign[16];
} LOOK;

static CONST char *CONST face_names[3][2] = {
	{ "MS Gothic", "ゴシック" }, { "MS Mincho", "明朝" }, { "Meiryo", "メイリオ" }
};
static CONST char *CONST line_names[4] = { "実線", "破線", "点線", "二重線" };

static void look_of( LOOK *k )
{
	CONST CXCELL	*c = cx_get(&g_s, g_col, g_row);
	CONST CXSTYLE	*st = ( c != NULL ) ? &c->st : NULL;
	INT		i;

	memset(k, 0, sizeof(*k));
	k->face = face_names[0][1];
	k->size = 12;
	k->colour = 0x00000000U;
	k->ground = 0x00FFFFFFU;
	snprintf(k->align, sizeof(k->align), "left");
	snprintf(k->valign, sizeof(k->valign), "middle");
	if ( st == NULL ) return;
	k->has = st->has;
	if ( st->has & CX_S_FACE ) {
		k->face = st->face;
		for ( i = 0; i < 3; i++ ) {
			if ( strcmp(st->face, face_names[i][0]) == 0 ) k->face = face_names[i][1];
		}
	}
	if ( ( st->has & CX_S_SIZE ) && st->size > 0 ) k->size = st->size;
	if ( st->has & CX_S_COLOUR ) k->colour = colour_value(st->colour, k->colour);
	if ( st->has & CX_S_BG ) k->ground = colour_value(st->bg, k->ground);
	if ( ( st->has & CX_S_ALIGN ) && st->align[0] != 0 ) snprintf(k->align, sizeof(k->align), "%s", st->align);
	if ( ( st->has & CX_S_VALIGN ) && st->valign[0] != 0 ) snprintf(k->valign, sizeof(k->valign), "%s", st->valign);
}

/* ---------------------------------------------------------------- the pixels */

#define C_INK		0x00333333U		/* the marks on the buttons */
#define C_ARROW		0x00666666U
#define C_EDGE		0x00AAAAAAU
#define C_EDGE_ON	0x006080A0U
#define C_ROWLINE	0x00CCCCCCU
#define C_SWATCH	0x00999999U
#define C_FOCUS		0x000066CCU
#define C_FAINT		0x00B8B8B8U
#define NOT_LAID	0xFFFFFFFFU

/* 32 bits a pixel, as the kernel lays them (UW is wider in a process) */
static uint32_t	pix[PW * PH];

static void dot( INT x, INT y, UW c )
{
	if ( x >= 0 && x < PW && y >= 0 && y < PH ) pix[y * PW + x] = (uint32_t)c;
}

static void box( INT l, INT t, INT r, INT b, UW c )
{
	INT	x, y;

	for ( y = t; y < b; y++ ) {
		for ( x = l; x < r; x++ ) dot(x, y, c);
	}
}

static UW mix( UW a, UW b, INT n, INT d )
{
	UW	r = ( ( a >> 16 ) & 0xFF ) * (UW)( d - n ) / (UW)d + ( ( b >> 16 ) & 0xFF ) * (UW)n / (UW)d;
	UW	g = ( ( a >> 8 ) & 0xFF ) * (UW)( d - n ) / (UW)d + ( ( b >> 8 ) & 0xFF ) * (UW)n / (UW)d;
	UW	l = ( a & 0xFF ) * (UW)( d - n ) / (UW)d + ( b & 0xFF ) * (UW)n / (UW)d;

	return ( r << 16 ) | ( g << 8 ) | l;
}

/* From one colour at the top to another at the foot */
static void shade( INT l, INT t, INT r, INT b, UW top, UW foot )
{
	INT	y;

	for ( y = t; y < b; y++ ) box(l, y, r, y + 1, mix(top, foot, y - t, ( b - t > 1 ) ? b - t - 1 : 1));
}

/* A line a pixel wide round a box, its corners rounded off */
static void edge( INT l, INT t, INT r, INT b, UW c )
{
	box(l + 1, t, r - 1, t + 1, c);
	box(l + 1, b - 1, r - 1, b, c);
	box(l, t + 1, l + 1, b - 1, c);
	box(r - 1, t + 1, r, b - 1, c);
}

static void line( INT x0, INT y0, INT x1, INT y1, UW c )
{
	INT	dx = ( x1 > x0 ) ? x1 - x0 : x0 - x1, dy = ( y1 > y0 ) ? y0 - y1 : y1 - y0;
	INT	sx = ( x0 < x1 ) ? 1 : -1, sy = ( y0 < y1 ) ? 1 : -1, e = dx + dy;

	for ( ;; ) {
		INT	e2;

		dot(x0, y0, c);
		if ( x0 == x1 && y0 == y1 ) break;
		e2 = 2 * e;
		if ( e2 >= dy ) {
			e += dy;
			x0 += sx;
		}
		if ( e2 <= dx ) {
			e += dx;
			y0 += sy;
		}
	}
}

/* A line two pixels thick */
static void stroke( INT x0, INT y0, INT x1, INT y1, UW c )
{
	line(x0, y0, x1, y1, c);
	line(x0, y0 + 1, x1, y1 + 1, c);
}

#define FACE_PLAIN	0
#define FACE_ON		1			/* pressed in: the cell has it */
#define FACE_OK		2
#define FACE_CANCEL	3

static void button( CONST T_DPRECT *r, INT how )
{
	static CONST UW	top[4] = { 0x00FFFFFFU, 0x00B0C4DEU, 0x00D0FFD0U, 0x00FFD0D0U };
	static CONST UW	foot[4] = { 0x00E8E8E8U, 0x008FB0D0U, 0x0090D090U, 0x00D09090U };

	shade(r->left + 1, r->top + 1, r->right - 1, r->bottom - 1, top[how], foot[how]);
	edge(r->left, r->top, r->right, r->bottom, ( how == FACE_ON ) ? C_EDGE_ON : C_EDGE);
}

/* ▼, its point at the foot */
static void arrow( INT cx, INT cy )
{
	box(cx - 2, cy - 1, cx + 3, cy, C_ARROW);
	box(cx - 1, cy, cx + 2, cy + 1, C_ARROW);
	box(cx, cy + 1, cx + 1, cy + 2, C_ARROW);
}

/* ---------------------------------------------------------------- the marks */

/* A cell's square, 11 dots, and a side of it drawn solid: ⎺ ⎽ ⎸ ⎹ */
static void side_mark( INT cx, INT cy, INT side )
{
	INT	i, l = cx - 5, t = cy - 5, r = cx + 5, b = cy + 5;

	for ( i = 0; i <= 10; i += 2 ) {
		dot(l + i, t, C_FAINT);
		dot(l + i, b, C_FAINT);
		dot(l, t + i, C_FAINT);
		dot(r, t + i, C_FAINT);
	}
	switch ( side ) {
	case CX_TOP:	box(l, t, r + 1, t + 2, C_INK);		break;
	case CX_BOTTOM:	box(l, b - 1, r + 1, b + 1, C_INK);	break;
	case CX_LEFT:	box(l, t, l + 2, b + 1, C_INK);		break;
	default:	box(r - 1, t, r + 1, b + 1, C_INK);	break;
	}
}

static void square( INT cx, INT cy )
{
	box(cx - 5, cy - 5, cx + 6, cy - 4, C_INK);
	box(cx - 5, cy + 5, cx + 6, cy + 6, C_INK);
	box(cx - 5, cy - 5, cx - 4, cy + 6, C_INK);
	box(cx + 5, cy - 5, cx + 6, cy + 6, C_INK);
}

/* 斜体: the I leaning */
static void italic_mark( INT cx, INT cy )
{
	box(cx - 1, cy - 6, cx + 5, cy - 4, C_INK);
	box(cx - 5, cy + 4, cx + 1, cy + 6, C_INK);
	line(cx + 2, cy - 4, cx - 1, cy + 4, C_INK);
	line(cx + 3, cy - 4, cx, cy + 4, C_INK);
	line(cx + 1, cy - 4, cx - 2, cy + 4, C_INK);
}

/* ☰, the lines laid as the cell places its words */
static void align_mark( INT cx, INT cy, CONST char *how )
{
	INT	i, w[3] = { 12, 7, 10 };

	for ( i = 0; i < 3; i++ ) {
		INT	l = ( how[0] == 'c' ) ? cx - w[i] / 2 : ( how[0] == 'r' ) ? cx + 6 - w[i] : cx - 6;

		box(l, cy - 5 + i * 4, l + w[i], cy - 3 + i * 4, C_INK);
	}
}

/* ↕, with a bar where the cell places its words */
static void valign_mark( INT cx, INT cy, CONST char *how )
{
	INT	y = ( how[0] == 't' ) ? cy - 6 : ( how[0] == 'b' ) ? cy + 5 : cy;

	box(cx - 3, cy - 5, cx - 2, cy + 6, C_INK);
	box(cx - 5, cy - 3, cx, cy - 2, C_INK);
	box(cx - 4, cy - 4, cx - 1, cy - 3, C_INK);
	box(cx - 5, cy + 3, cx, cy + 4, C_INK);
	box(cx - 4, cy + 4, cx - 1, cy + 5, C_INK);
	box(cx + 1, y, cx + 7, y + 2, C_INK);
}

/* A card for a chart: white, framed */
static void card( INT cx, INT cy, UW frame )
{
	box(cx - 7, cy - 7, cx + 8, cy + 8, 0x00FFFFFFU);
	edge(cx - 8, cy - 8, cx + 9, cy + 9, frame);
}

/* 📊 */
static void bars_mark( INT cx, INT cy )
{
	card(cx, cy, 0x009E9E9EU);
	box(cx - 5, cy + 1, cx - 2, cy + 6, 0x00E53935U);
	box(cx - 1, cy - 5, cx + 2, cy + 6, 0x001E88E5U);
	box(cx + 3, cy - 2, cx + 6, cy + 6, 0x0043A047U);
	box(cx - 6, cy + 6, cx + 7, cy + 7, 0x00757575U);
}

/* 📶 */
static void steps_mark( INT cx, INT cy )
{
	INT	i;

	box(cx - 7, cy - 8, cx + 8, cy + 9, 0x001E88E5U);
	box(cx - 8, cy - 7, cx + 9, cy + 8, 0x001E88E5U);
	for ( i = 0; i < 4; i++ ) box(cx - 6 + i * 3 + i, cy + 5 - ( 3 + i * 3 ), cx - 4 + i * 3 + i, cy + 6, 0x00FFFFFFU);
}

/* 📈, in its own colours or another tint of them */
static void rise_mark( INT cx, INT cy, UW ink, UW grid )
{
	card(cx, cy, 0x009E9E9EU);
	box(cx - 3, cy - 6, cx - 2, cy + 7, grid);
	box(cx + 2, cy - 6, cx + 3, cy + 7, grid);
	box(cx - 6, cy - 2, cx + 7, cy - 1, grid);
	box(cx - 6, cy + 3, cx + 7, cy + 4, grid);
	stroke(cx - 6, cy + 4, cx - 2, cy - 1, ink);
	stroke(cx - 2, cy - 1, cx + 1, cy + 2, ink);
	stroke(cx + 1, cy + 2, cx + 6, cy - 5, ink);
}

static void check_mark( INT cx, INT cy )
{
	stroke(cx - 5, cy - 1, cx - 2, cy + 2, C_INK);
	stroke(cx - 2, cy + 2, cx + 5, cy - 5, C_INK);
}

static void cross_mark( INT cx, INT cy )
{
	line(cx - 4, cy - 4, cx + 4, cy + 4, C_INK);
	line(cx - 3, cy - 4, cx + 5, cy + 4, C_INK);
	line(cx + 4, cy - 4, cx - 4, cy + 4, C_INK);
	line(cx + 5, cy - 4, cx - 3, cy + 4, C_INK);
}

/* A colour's part: the mark, and the colour in a small box */
static void swatch( CONST T_DPRECT *r, UW c )
{
	button(r, FACE_PLAIN);
	box(r->right - 37, r->top + 2, r->right - 5, r->top + 20, C_SWATCH);
	box(r->right - 36, r->top + 3, r->right - 6, r->top + 19, c & 0x00FFFFFFU);
}

/* ---------------------------------------------------------------- the window */

static TS_UUID	p_win;
static ID	p_kw;
static INT	p_gid = -1;
static BOOL	p_open;

BOOL panel_on( void )
{
	return p_open;
}

BOOL panel_is( CONST TS_UUID *u )
{
	return (BOOL)( p_open && memcmp(u, &p_win, sizeof(TS_UUID)) == 0 );
}

static void uuid_text( CONST TS_UUID *u, char *s )
{
	static CONST char	hex[] = "0123456789abcdef";
	INT			i, n = 0;

	for ( i = 0; i < 16; i++ ) {
		if ( i == 4 || i == 6 || i == 8 || i == 10 ) s[n++] = '-';
		s[n++] = hex[u->b[i] >> 4];
		s[n++] = hex[u->b[i] & 0x0F];
	}
	s[n] = 0;
}

void panel_open( void )
{
	T_OBWPOS	wp, pp;
	T_OBCRE		c;
	T_OBNTF		req;
	SZ		asz = 0;
	char		json[200], par[40], s[80];
	INT		l, t, ow = PW + 12, oh = PH + 12;

	if ( p_open || g_kw <= 0 ) return;
	if ( ob_rea_rec(g_kw, OB_WR_PLACE, 0, &wp, sizeof(wp), &asz) < E_OK ) return;
	memset(&pp, 0, sizeof(pp));

	/* where the object says, against the window; else over the window's top edge */
	l = wp.left + ( g_panel_said ? g_panel_dx : 0 );
	t = wp.top + ( g_panel_said ? g_panel_dy : -( oh + 2 ) );
	if ( wp.sw > 0 && l + ow > wp.sw ) l = wp.sw - ow;
	if ( wp.sh > 0 && t + oh > wp.sh ) t = wp.sh - oh;
	if ( l < 0 ) l = 0;
	if ( t < 0 ) t = 0;
	uuid_text(&g_win, par);
	snprintf(json, sizeof(json), "{\"rect\":[%d,%d,%d,%d],\"attr\":%d,\"sub\":\"%s\"}",
		 l, t, l + ow, t + oh, WM_ATTR_FRAME, par);
	memset(&c, 0, sizeof(c));
	c.type = OB_T_WINDOW;
	c.sub = OB_S_WINDOW;
	c.name = (CONST UB *)"操作パネル";
	c.json = (CONST UB *)json;
	c.jsonsz = (SZ)strlen(json);
	if ( ob_cre_obj(&c, &p_win) < E_OK ) {
		note("the panel cannot be opened");
		return;
	}
	p_kw = ob_opn_obj(&p_win, OB_OP_R | OB_OP_W | OB_OP_PROT);
	p_gid = ( p_kw > 0 ) ? wm_obj_gid(p_kw) : E_ID;
	memset(&req, 0, sizeof(req));
	req.events = OB_E_PRESS | OB_E_KEY | OB_E_REDRAW | OB_E_CLOSE | OB_E_DELETE;
	if ( p_kw <= 0 || p_gid < 0 || ob_ntf_evt(p_kw, OB_REC_ANY, &req, g_port) <= 0 ) {
		if ( p_kw > 0 ) ob_cls_obj(p_kw);
		(void)ob_del_obj(&p_win);
		p_kw = 0;
		p_gid = -1;
		note("the panel cannot be opened");
		return;
	}
	/* the work area as large as the parts need, whatever the frame takes */
	if ( ob_rea_rec(p_kw, OB_WR_PLACE, 0, &pp, sizeof(pp), &asz) >= E_OK ) {
		pp.right = pp.left + PW + ( ( pp.right - pp.left ) - ( pp.wright - pp.wleft ) );
		pp.bottom = pp.top + PH + ( ( pp.bottom - pp.top ) - ( pp.wbottom - pp.wtop ) );
		(void)ob_wri_rec(p_kw, OB_WR_PLACE, 0, &pp, sizeof(pp), &asz);
		(void)ob_rea_rec(p_kw, OB_WR_PLACE, 0, &pp, sizeof(pp), &asz);
	}
	p_open = TRUE;
	panel_draw();
	snprintf(s, sizeof(s), "panel at %d %d %d %d", pp.left, pp.top, pp.right, pp.bottom);
	note(s);
}

void panel_close( void )
{
	if ( !p_open ) return;
	p_open = FALSE;
	if ( p_kw > 0 ) ob_cls_obj(p_kw);
	(void)ob_del_obj(&p_win);
	p_kw = 0;
	p_gid = -1;
}

/* Where the box's candidates stand: under the box, in the sheet's window */
static void field_put( void )
{
	T_OBWPOS	wp, pp;
	T_DPRECT	r;
	SZ		asz = 0;
	INT		x, y;

	if ( ob_rea_rec(g_kw, OB_WR_PLACE, 0, &wp, sizeof(wp), &asz) < E_OK
	  || ob_rea_rec(p_kw, OB_WR_PLACE, 0, &pp, sizeof(pp), &asz) < E_OK ) return;
	part_box(TP_FIELD, &r);
	x = pp.wleft + r.left - wp.wleft;
	y = pp.wtop + r.bottom + 2 - wp.wtop;
	if ( x < 0 ) x = 0;
	if ( y < 0 ) y = 0;
	ed_field_put(x, y);
}

/* ---------------------------------------------------------------- drawing */

/* Letters centred in a part's box, bold drawn twice a dot apart */
static void letters( CONST T_DPRECT *r, CONST char *s, INT px, BOOL bold, INT dy )
{
	INT	w = dp_text_width((CONST UB *)s, px), x = r->left + ( r->right - r->left - w ) / 2;
	INT	y = r->top + ( r->bottom - r->top + px ) / 2 - 1 + dy;

	(void)dp_text(p_gid, x, y, (CONST UB *)s, C_INK, px);
	if ( bold ) (void)dp_text(p_gid, x + 1, y, (CONST UB *)s, C_INK, px);
}

/* A ▼ part's words, from its left */
static void drop_words( CONST T_DPRECT *r, CONST char *s )
{
	(void)dp_text(p_gid, r->left + 5, r->top + 15, (CONST UB *)s, C_TEXT, 11);
}

/* What the cell in hand holds, as the box shows it when nothing is typed */
static void held( char *s, INT size )
{
	CONST CXCELL	*c = cx_get(&g_s, g_col, g_row);

	if ( c == NULL ) s[0] = 0;
	else if ( c->kind == CX_LINK ) snprintf(s, (size_t)size, "[仮身]");
	else if ( c->kind == CX_IMAGE ) snprintf(s, (size_t)size, "[画像]");
	else snprintf(s, (size_t)size, "%s", (CONST char *)c->text);
}

void panel_draw( void )
{
	LOOK		k;
	CONST CXSIDE	*ln = line_side();
	T_DPRECT	r, f;
	INT		i, cx, cy;
	char		s[200], nm[24], w[8];

	if ( !p_open || p_gid < 0 ) return;
	look_of(&k);

	/* the ground, and a line under each row but the last */
	shade(0, 0, PW, PH, 0x00F8F8F8U, 0x00E0E0E0U);
	box(0, ROW_T(1) - 1, PW, ROW_T(1), C_ROWLINE);
	box(0, ROW_T(2) - 1, PW, ROW_T(2), C_ROWLINE);

	for ( i = 0; i < TP_N; i++ ) {
		CONST PART	*p = &parts[i];
		BOOL		on = FALSE;

		part_box(i, &r);
		cx = ( r.left + r.right ) / 2;
		cy = ( r.top + r.bottom ) / 2;
		switch ( i ) {
		case TP_BOLD:	on = (BOOL)( ( k.has & CX_S_BOLD ) != 0 );	break;
		case TP_ITALIC:	on = (BOOL)( ( k.has & CX_S_ITALIC ) != 0 );	break;
		case TP_UNDER:	on = (BOOL)( ( k.has & CX_S_UNDER ) != 0 );	break;
		case TP_STRIKE:	on = (BOOL)( ( k.has & CX_S_STRIKE ) != 0 );	break;
		case TP_SUPER:	on = (BOOL)( ( k.has & CX_S_SUPER ) != 0 );	break;
		case TP_SUB:	on = (BOOL)( ( k.has & CX_S_SUB ) != 0 );	break;
		default:	break;
		}
		switch ( p->kind ) {
		case K_REF:
		case K_FIELD:
			box(r.left + 1, r.top + 1, r.right - 1, r.bottom - 1, 0x00FFFFFFU);
			edge(r.left, r.top, r.right, r.bottom, ( p->kind == K_FIELD && ed_in_field() ) ? C_FOCUS : C_EDGE);
			if ( p->kind == K_FIELD && ed_in_field() ) {
				box(r.left, r.top + 1, r.left + 1, r.bottom - 1, C_FOCUS);
			}
			break;
		case K_COLOUR:
			swatch(&r, ( i == TP_COLOUR ) ? k.colour : ( i == TP_GROUND ) ? k.ground
				   : colour_value(ln->colour, 0x00000000U));
			break;
		case K_OK:
			button(&r, FACE_OK);
			check_mark(cx, cy);
			break;
		case K_CANCEL:
			button(&r, FACE_CANCEL);
			cross_mark(cx, cy);
			break;
		default:
			button(&r, on ? FACE_ON : FACE_PLAIN);
			break;
		}
		if ( p->kind == K_DROP ) arrow(r.right - 8, cy);
		switch ( i ) {
		case TP_ITALIC:	italic_mark(cx, cy);					break;
		case TP_UNDER:	box(cx - 4, cy + 6, cx + 5, cy + 7, C_INK);		break;
		case TP_ALIGN:	align_mark(cx - 6, cy, k.align);			break;
		case TP_VALIGN:	valign_mark(cx - 5, cy, k.valign);			break;
		case TP_GROUND:	box(r.left + 4, cy - 5, r.left + 14, cy + 5, 0x00000000U);	break;
		case TP_LCOLOUR:	box(r.left + 3, cy - 1, r.left + 13, cy + 2, C_INK);	break;
		case TP_ALL:
			square(cx, cy);
			box(cx, cy - 5, cx + 1, cy + 6, C_INK);
			box(cx - 5, cy, cx + 6, cy + 1, C_INK);
			break;
		case TP_OUTLINE:	square(cx, cy);						break;
		case TP_TOP:	side_mark(cx, cy, CX_TOP);				break;
		case TP_BOTTOM:	side_mark(cx, cy, CX_BOTTOM);				break;
		case TP_LEFT:	side_mark(cx, cy, CX_LEFT);				break;
		case TP_RIGHT:	side_mark(cx, cy, CX_RIGHT);				break;
		case TP_NOFRAME:
			square(cx, cy);
			line(cx - 5, cy - 5, cx + 5, cy + 5, C_INK);
			line(cx + 5, cy - 5, cx - 5, cy + 5, C_INK);
			break;
		case TP_BAR:	bars_mark(cx, cy);					break;
		case TP_SBAR:	steps_mark(cx, cy);					break;
		case TP_PLOT:	rise_mark(cx, cy, 0x00E53935U, 0x00D0D8E0U);		break;
		case TP_SPLOT:	rise_mark(cx, cy, 0x00E0359EU, 0x00CDE6DEU);		break;
		default:	break;
		}
	}
	(void)dp_put_argb(p_gid, 0, 0, (CONST UW *)(void *)pix, PW, PW, PH, NOT_LAID);

	/* the letters, over the pictures */
	part_box(TP_FACE, &r);
	drop_words(&r, k.face);
	part_box(TP_SIZE, &r);
	snprintf(w, sizeof(w), "%d", k.size);
	drop_words(&r, w);
	part_box(TP_BOLD, &r);
	letters(&r, "B", 12, TRUE, 0);
	part_box(TP_UNDER, &r);
	letters(&r, "U", 12, TRUE, -1);
	part_box(TP_STRIKE, &r);
	letters(&r, "S", 12, TRUE, 0);
	f.left = r.left + 6;
	f.right = r.right - 6;
	f.top = ( r.top + r.bottom ) / 2 - 1;
	f.bottom = f.top + 1;
	(void)dp_fill_rect(p_gid, &f, C_INK);
	part_box(TP_SUPER, &r);
	(void)dp_text(p_gid, r.left + 6, r.top + 16, (CONST UB *)"x", C_INK, 12);
	(void)dp_text(p_gid, r.left + 13, r.top + 10, (CONST UB *)"2", C_INK, 8);
	part_box(TP_SUB, &r);
	(void)dp_text(p_gid, r.left + 6, r.top + 14, (CONST UB *)"x", C_INK, 12);
	(void)dp_text(p_gid, r.left + 13, r.top + 18, (CONST UB *)"2", C_INK, 8);
	part_box(TP_COLOUR, &r);
	(void)dp_text(p_gid, r.left + 5, r.top + 16, (CONST UB *)"A", C_INK, 12);
	(void)dp_text(p_gid, r.left + 6, r.top + 16, (CONST UB *)"A", C_INK, 12);
	part_box(TP_LINE, &r);
	drop_words(&r, ( ln->type >= 0 && ln->type < 4 ) ? line_names[ln->type] : line_names[0]);
	part_box(TP_LWIDTH, &r);
	snprintf(w, sizeof(w), "%d", ln->width);
	drop_words(&r, ( ln->width == 1 ) ? "細" : ( ln->width == 2 ) ? "中" : ( ln->width == 3 ) ? "太" : w);
	part_box(TP_FX, &r);
	letters(&r, "fx", 12, TRUE, 0);
	part_box(TP_REF, &r);
	(void)cx_cell_name(g_col, g_row, nm);
	(void)dp_text(p_gid, r.left + 5, r.top + 15, (CONST UB *)nm, C_TEXT, 11);

	/* the box: what is typed, or what the cell holds */
	part_box(TP_FIELD, &r);
	if ( ed_on() ) {
		if ( ed_in_field() ) field_put();
		ed_draw_field(p_gid, &r, TRUE);
	} else {
		held(s, sizeof(s));
		if ( s[0] != 0 ) {
			INT	n = (INT)strlen(s);

			/* as much as the box holds */
			while ( n > 0 && dp_text_width((CONST UB *)s, 12) > r.right - r.left - 8 ) {
				do {
					n--;
				} while ( n > 0 && ( (UB)s[n] & 0xC0 ) == 0x80 );
				s[n] = 0;
			}
			(void)dp_text(p_gid, r.left + 4, r.top + 16, (CONST UB *)s, C_TEXT, 12);
		}
	}
	(void)wm_obj_flush(p_kw, NULL);
}

/* ---------------------------------------------------------------- what the parts do */

/* The menu a ▼ part or a colour opens under it, what is in use marked */
static void drop( INT k, UD when )
{
	TS_UUID		def;
	T_MNSEL		sel;
	T_DPRECT	r;
	LOOK		lk;
	CONST CXSIDE	*ln = line_side();
	ID		mid;
	char		code[MN_CODE_MAX];
	BOOL		chosen;

	if ( ui_uuid(parts[k].code, &def) < E_OK || mn_cre_men(&def, &mid) < E_OK ) return;
	look_of(&lk);
	code[0] = 0;
	switch ( k ) {
	case TP_FACE:
		snprintf(code, sizeof(code), "font.%s", ( strcmp(lk.face, "明朝") == 0 ) ? "mincho"
			 : ( strcmp(lk.face, "メイリオ") == 0 ) ? "meiryo" : "gothic");
		break;
	case TP_ALIGN:	snprintf(code, sizeof(code), "align.%s", lk.align);	break;
	case TP_VALIGN:	snprintf(code, sizeof(code), "valign.%s", lk.valign);	break;
	case TP_LINE:
		{
			static CONST char *CONST type[4] = { "solid", "dashed", "dotted", "double" };

			snprintf(code, sizeof(code), "line.%s", type[( ln->type >= 0 && ln->type < 4 ) ? ln->type : 0]);
		}
		break;
	default:
		break;
	}
	if ( code[0] != 0 ) (void)mn_chg_atr(mid, NULL, code, MN_TICK);
	part_box(k, &r);
	chosen = (BOOL)( mn_pop_men(mid, p_kw, r.left, r.bottom + 2, when, &sel) >= E_OK );
	(void)mn_del_men(mid);
	if ( chosen ) {
		char	s[80];

		snprintf(s, sizeof(s), "panel %s %d", (CONST char *)sel.code, (INT)sel.value);
		note(s);
		command((CONST char *)sel.code, sel.index, sel.value);
	}
}

/* Whether the cell in hand can be typed into: not a link or an image */
static BOOL typable( void )
{
	CONST CXCELL	*c = cx_get(&g_s, g_col, g_row);

	if ( c != NULL && c->kind != CX_TEXT ) {
		say("仮身や画像のセルには入力できません");
		return FALSE;
	}
	return TRUE;
}

/* A part worked: x is where in it it was pressed */
static void work( INT k, INT x, UD when )
{
	char	s[80];

	switch ( parts[k].kind ) {
	case K_DROP:
	case K_COLOUR:
		ed_commit();
		if ( p_kw > 0 ) drop(k, when);
		break;
	case K_PUSH:
	case K_CHART:
		if ( k == TP_FX ) {
			if ( !typable() ) break;
			ed_to_field(TRUE);
			note("panel fx");
			break;
		}
		ed_commit();
		snprintf(s, sizeof(s), "panel %s", parts[k].code);
		note(s);
		command(parts[k].code, 0, 0);
		break;
	case K_FIELD:
		if ( !ed_in_field() ) {
			if ( !ed_on() && !typable() ) break;
			ed_to_field(FALSE);
			note("panel field");
		}
		ed_field_caret(x);
		break;
	case K_OK:
		if ( ed_on() ) {
			ed_commit();
			note("panel ok");
		}
		break;
	case K_CANCEL:
		if ( ed_on() ) {
			ed_cancel();
			note("panel cancel");
		}
		break;
	default:
		break;
	}
	g_dirty = TRUE;
}

void panel_event( CONST T_OBNTM *m )
{
	INT		k;
	T_DPRECT	r;

	switch ( m->event ) {
	case OB_E_PRESS:
		if ( m->code != 0 ) break;
		k = part_at(m->x, m->y);
		if ( k < 0 ) break;
		part_box(k, &r);
		work(k, m->x - r.left - 4, m->when);
		break;
	case OB_E_REDRAW:
		panel_draw();
		break;
	case OB_E_CLOSE:
		ed_commit();
		panel_close();
		note("panel off");
		g_dirty = TRUE;
		break;
	case OB_E_DELETE:
		p_open = FALSE;
		p_kw = 0;
		p_gid = -1;
		break;
	default:
		break;
	}
}

/* ---------------------------------------------------------------- the program's own test */

static INT	t_pass, t_all;

static void t_check( BOOL ok, CONST char *what )
{
	char	s[120];

	t_all++;
	if ( ok ) {
		t_pass++;
	} else {
		snprintf(s, sizeof(s), "selftest panel: %s", what);
		note(s);
	}
}

/*
 * Every part found where it is drawn and nowhere between them; and the
 * buttons worked on a sheet of their own: the decorations, the frames,
 * fx with ✓ and ✕, and what the parts show of the cell in hand.
 */
INT panel_test( INT *p_all )
{
	T_DPRECT	r;
	LOOK		lk;
	CONST CXCELL	*c;
	INT		k;

	t_pass = t_all = 0;
	for ( k = 0; k < TP_N; k++ ) {
		part_box(k, &r);
		t_check((BOOL)( part_at(( r.left + r.right ) / 2, ( r.top + r.bottom ) / 2 ) == k
				&& r.right <= PW && r.bottom <= PH ), "a part not where it is drawn");
		if ( k > 0 && parts[k].row == parts[k - 1].row ) {
			t_check((BOOL)( parts[k].l >= parts[k - 1].l + parts[k - 1].w + 4 ), "parts too near");
		}
		if ( parts[k].kind == K_PUSH || parts[k].kind == K_CHART || parts[k].kind == K_DROP
		  || parts[k].kind == K_COLOUR ) {
			t_check((BOOL)( k == TP_FX || parts[k].code != NULL ), "a part does nothing");
		}
	}
	t_check((BOOL)( part_at(2, 10) < 0 && part_at(PW - 2, 10) < 0 && part_at(100, ROW_T(1) - 1) < 0 ),
		"a part between the parts");

	cx_init(&g_s);
	g_col = g_acol = 1;
	g_row = g_arow = 1;
	cell_text(1, 1, (CONST UB *)"5");
	work(TP_BOLD, 0, 0);
	work(TP_SUPER, 0, 0);
	work(TP_SUB, 0, 0);
	look_of(&lk);
	t_check((BOOL)( ( lk.has & CX_S_BOLD ) != 0 ), "bold");
	t_check((BOOL)( ( lk.has & ( CX_S_SUPER | CX_S_SUB ) ) == CX_S_SUB ), "上付き and 下付き both");
	command("valign.top", 0, 0);
	command("align.right", 0, 0);
	look_of(&lk);
	t_check((BOOL)( strcmp(lk.valign, "top") == 0 && strcmp(lk.align, "right") == 0 ), "the placing");
	command("linewidth", 0, 3);
	command("line.dotted", 0, 0);
	work(TP_TOP, 0, 0);
	c = cx_get(&g_s, 1, 1);
	t_check((BOOL)( c != NULL && c->st.side[CX_TOP].on && c->st.side[CX_TOP].width == 3
			&& c->st.side[CX_TOP].type == CX_DOTTED && !c->st.side[CX_BOTTOM].on ), "the top frame");
	work(TP_FX, 0, 0);
	t_check((BOOL)( ed_in_field() ), "fx");
	work(TP_OK, 0, 0);
	c = cx_get(&g_s, 1, 1);
	t_check((BOOL)( !ed_on() && c != NULL && strcmp((CONST char *)c->text, "=5") == 0 ), "fx and ✓");
	g_col = g_acol = 2;
	work(TP_FX, 0, 0);
	work(TP_CANCEL, 0, 0);
	t_check((BOOL)( !ed_on() && cx_get(&g_s, 2, 1) == NULL ), "fx and ✕");
	command("line.solid", 0, 0);
	command("linewidth", 0, 1);
	ed_end();
	cx_free_all(&g_s);
	g_col = g_acol = 1;
	*p_all = t_all;
	return t_pass;
}
