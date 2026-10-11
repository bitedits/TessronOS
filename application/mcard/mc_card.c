/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	mc_card.c
 *	マイクロカード: the card window (design 11.18)
 *
 *	Along the top is a bar of parts the program draws, and under it a
 *	row for each field: its number, its name and a box with its value.
 *	The window shows one of three screens:
 *
 *	  カード画面	the card shown: the mark at the left of the bar, the
 *			box with its place and the cards' count, ◀ ▶, and
 *			検索条件 and 一覧 at the right (検索解除 in the search
 *			state, the bar coloured then); the values in the boxes
 *	  検索条件画面	the boxes hold what each field is searched with; the
 *			bar has the signs = ≦ ≧ < > ≠ ≒ / ? * that put
 *			themselves in the box in hand, and 新規検索, 絞込検索,
 *			検索解除 and カード
 *	  設計画面	a row for each field and one empty after them: its name,
 *			kind, largest size, width in the list and face; the
 *			bar has the 印刷書式 (the layout cabinet's link,
 *			dropped there) and 設定 and 中止
 *
 *	A press on a box begins typing into it (mc_edit.c); Enter or ↓ puts
 *	what is typed in and goes to the box below, Shift+Enter or ↑ to the
 *	one above, Tab and Shift+Tab the same without scrolling (on 設計画面
 *	across the row), Alt+↑ ↓ to the first and the last, left Ctrl+↑ ↓ a
 *	window further. F10 or the right Ctrl puts it in and stays; on 設計画面
 *	it is 設定. A field of 仮身 holds a link, put there by dropping it
 *	or from the tray: pressed it is taken, pressed twice opened, carried
 *	out of the window copied or moved there.
 */

#include "mc.h"
#include <ts/json.h>

#define TB_H		36			/* the bar along the top */
#define BTN_H		24
#define NUM_W		34			/* the fields' numbers */
#define MARK_SZ		18

typedef struct {
	CONST char	*code;
	CONST char	*label;
	T_DPRECT	r;
	BOOL		on;			/* drawn pressed in */
} BTN;

#define BTN_MAX		20
static BTN	btn[BTN_MAX];
static INT	nbtn;
static T_DPRECT	mark_r, pos_r, slot_r;
static INT	g_sy;				/* how far the rows are scrolled */
static UB	g_fig[256 * 1024];

/* The field's value as it was before the last typing, for 取り消し */
static MCCARD	*undo_card;
static INT	undo_f = -1;
static UB	*undo_v;
static BOOL	undo_on;

/* A press held: a link carried */
static BOOL	hold_link;
static INT	hold_x, hold_y;
static BOOL	hold_twice, hold_moved;
static UD	press_at;
static INT	press_f = -1;

static CONST char *CONST kind_name[3] = { "文字", "数値", "仮身" };
static CONST char *CONST face_label[3] = { "標準", "明朝", "ゴシック" };
static CONST char *CONST face_font[3] = { NULL, "MS Mincho", "MS Gothic" };

/* ---------------------------------------------------------------- where things are */

static INT row_h( void )
{
	return card_px() + 12;
}

static INT body_top( void )
{
	return TB_H + 4 + ( ( g_screen == SCR_DESIGN ) ? row_h() : 0 );
}

static INT rows( void )
{
	return ( g_screen == SCR_DESIGN ) ? g_dsn : g_db.nf;
}

static INT row_top( INT k )
{
	return body_top() + k * row_h() - g_sy;
}

static INT name_w( void )
{
	return MC_NAME_LETTERS * card_px() + 10;
}

/* The columns of 設計画面: name, kind, size, width, face */
static void design_cols( INT *x )
{
	INT	px = card_px();

	x[0] = NUM_W + 6;
	x[1] = x[0] + name_w();
	x[2] = x[1] + 6;
	x[3] = x[2] + 4 * px + 12;
	x[4] = x[3] + 6;
	x[5] = x[4] + 6 * px;
	x[6] = x[5] + 6;
	x[7] = x[6] + 6 * px;
	x[8] = x[7] + 6;
	x[9] = x[8] + 5 * px + 10;
}

static INT value_left( void )
{
	return g_db.view.names ? NUM_W + 6 + name_w() + 6 : NUM_W + 6;
}

BOOL card_box( INT kind, INT k, T_DPRECT *r )
{
	INT	x[10], t;

	if ( kind == ED_POS ) {
		*r = pos_r;
		return (BOOL)( g_screen == SCR_CARD );
	}
	t = row_top(k);
	r->top = t + 2;
	r->bottom = t + row_h() - 2;
	if ( kind == ED_VALUE || kind == ED_COND ) {
		r->left = value_left();
		r->right = g_cw.w - 10;
	} else {
		design_cols(x);
		switch ( kind ) {
		case ED_DNAME:	r->left = x[0]; r->right = x[1];	break;
		case ED_DMAX:	r->left = x[4]; r->right = x[5];	break;
		case ED_DWIDTH:	r->left = x[6]; r->right = x[7];	break;
		default:	return FALSE;
		}
	}
	if ( r->right <= r->left + 20 ) r->right = r->left + 20;
	return (BOOL)( r->top >= body_top() - 2 && r->bottom <= g_cw.h + row_h() );
}

void link_box( INT f, T_DPRECT *r )
{
	MCCARD		*c = cur_card();
	CONST UB	*el = mc_get(c, f);
	T_DPRECT	b;
	INT		w = LINK_W;

	(void)card_box(ED_VALUE, f, &b);
	if ( el != NULL ) {
		INT	n = (INT)strlen((CONST char *)el), lw = ct_attr_int(el, n, "width", 0);

		if ( lw <= 0 ) lw = ct_attr_int(el, n, "vobjright", 0) - ct_attr_int(el, n, "vobjleft", 0);
		if ( lw > 40 ) w = lw;
	}
	r->left = b.left + 1;
	r->top = b.top + 1;
	r->right = r->left + ( ( w < b.right - b.left - 2 ) ? w : b.right - b.left - 2 );
	r->bottom = b.bottom - 1;
}

INT field_at_link( INT x, INT y )
{
	INT	k;

	if ( g_screen != SCR_CARD ) return -1;
	for ( k = 0; k < g_db.nf; k++ ) {
		T_DPRECT	r;

		if ( g_db.f[k].type != MC_T_LINK || mc_get(cur_card(), k) == NULL ) continue;
		link_box(k, &r);
		if ( x >= r.left && x < r.right && y >= r.top && y < r.bottom ) return k;
	}
	return -1;
}

void card_scroll_to( INT k )
{
	INT	t = k * row_h(), b = t + row_h(), vh = g_cw.h - body_top();

	if ( k < 0 ) return;
	if ( t < g_sy ) g_sy = t;
	else if ( b > g_sy + vh && vh > 0 ) g_sy = b - vh;
	if ( g_sy < 0 ) g_sy = 0;
	g_cw.dirty = TRUE;
}

/* ---------------------------------------------------------------- the bar */

static void btn_add( CONST char *code, CONST char *label, INT *px_right, BOOL from_right, INT *px_left, BOOL on )
{
	BTN	*b;
	INT	w = dp_text_width((CONST UB *)label, 12) + 16;

	if ( nbtn >= BTN_MAX ) return;
	b = &btn[nbtn++];
	b->code = code;
	b->label = label;
	b->on = on;
	if ( from_right ) {
		*px_right -= w + 4;
		b->r.left = *px_right;
	} else {
		b->r.left = *px_left;
		*px_left += w + 4;
	}
	b->r.right = b->r.left + w;
	b->r.top = ( TB_H - BTN_H ) / 2;
	b->r.bottom = b->r.top + BTN_H;
}

static void bar_layout( void )
{
	INT	right = g_cw.w - 6, left = 8, k;
	static CONST char *CONST sign[10] = { "=", "≦", "≧", "<", ">", "≠", "≒", "/", "?", "*" };
	static CONST char *CONST sign_code[10] = { "sign.0", "sign.1", "sign.2", "sign.3", "sign.4",
						   "sign.5", "sign.6", "sign.7", "sign.8", "sign.9" };

	nbtn = 0;
	memset(&mark_r, 0, sizeof(mark_r));
	memset(&pos_r, 0, sizeof(pos_r));
	memset(&slot_r, 0, sizeof(slot_r));
	switch ( g_screen ) {
	case SCR_CARD:
		mark_r.left = 8;
		mark_r.top = ( TB_H - MARK_SZ ) / 2;
		mark_r.right = mark_r.left + MARK_SZ;
		mark_r.bottom = mark_r.top + MARK_SZ;
		pos_r.left = 34;
		pos_r.top = ( TB_H - BTN_H ) / 2;
		pos_r.right = 90;
		pos_r.bottom = pos_r.top + BTN_H;
		left = 210;
		btn_add("go.prev", "◀", &right, FALSE, &left, FALSE);
		btn_add("go.next", "▶", &right, FALSE, &left, FALSE);
		btn_add("list", "一覧", &right, TRUE, &left, g_lw.open);
		btn_add("scr.find", "検索条件", &right, TRUE, &left, FALSE);
		if ( g_db.searching ) btn_add("search.off", "検索解除", &right, TRUE, &left, FALSE);
		break;
	case SCR_FIND:
		for ( k = 0; k < 10; k++ ) btn_add(sign_code[k], sign[k], &right, FALSE, &left, FALSE);
		btn_add("scr.card", "カード", &right, TRUE, &left, FALSE);
		if ( g_db.searching ) btn_add("search.off", "検索解除", &right, TRUE, &left, FALSE);
		if ( g_db.searching ) btn_add("search.narrow", "絞込検索", &right, TRUE, &left, FALSE);
		btn_add("search.new", "新規検索", &right, TRUE, &left, FALSE);
		break;
	default:
		btn_add("design.cancel", "中止", &right, TRUE, &left, FALSE);
		btn_add("design.ok", "設定", &right, TRUE, &left, FALSE);
		slot_r.left = 80;
		slot_r.top = ( TB_H - BTN_H ) / 2;
		slot_r.right = ( right - 10 > 300 ) ? 300 : right - 10;
		slot_r.bottom = slot_r.top + BTN_H;
		break;
	}
}

static void fill( INT l, INT t, INT r, INT b, UW c )
{
	T_DPRECT	rc;

	rc.left = l;
	rc.top = t;
	rc.right = r;
	rc.bottom = b;
	if ( r > l && b > t ) (void)dp_fill_rect(g_cw.gid, &rc, c);
}

static void frame( CONST T_DPRECT *r, UW c, INT w )
{
	(void)dp_frame_rect(g_cw.gid, r, c, w);
}

static void text_in( CONST T_DPRECT *r, CONST char *s, UW col, INT px, BOOL centre )
{
	INT	w = dp_text_width((CONST UB *)s, px);
	INT	x = centre ? r->left + ( r->right - r->left - w ) / 2 : r->left + 4;

	(void)dp_text(g_cw.gid, x, r->top + ( r->bottom - r->top + px ) / 2 - px / 8, (CONST UB *)s, col, px);
}

static void bar_paint( void )
{
	INT	i;
	UW	ground = ( g_screen == SCR_DESIGN ) ? C_DESIGN : ( g_db.searching || g_screen == SCR_FIND ) ? C_FIND : C_BAR;
	char	s[96];

	bar_layout();
	fill(0, 0, g_cw.w, TB_H, ground);
	fill(0, TB_H - 1, g_cw.w, TB_H, C_LINE);
	for ( i = 0; i < nbtn; i++ ) {
		BTN	*b = &btn[i];

		fill(b->r.left, b->r.top, b->r.right, b->r.bottom, b->on ? C_ROW : C_BUTTON);
		frame(&b->r, C_LINE, 1);
		if ( strcmp(b->label, "≒") == 0 ) {
			/* the system's face has no ≒: an = with a dot over and under it */
			INT	cx = ( b->r.left + b->r.right ) / 2, cy = ( b->r.top + b->r.bottom ) / 2;

			fill(cx - 5, cy - 3, cx + 5, cy - 2, C_TEXT);
			fill(cx - 5, cy + 2, cx + 5, cy + 3, C_TEXT);
			fill(cx - 4, cy - 7, cx - 2, cy - 5, C_TEXT);
			fill(cx + 2, cy + 5, cx + 4, cy + 7, C_TEXT);
		} else {
			text_in(&b->r, b->label, C_TEXT, 12, TRUE);
		}
	}
	if ( g_screen == SCR_CARD ) {
		MCCARD	*c = cur_card();
		INT	total = mc_count(&g_db);
		T_DPRECT r;

		fill(mark_r.left, mark_r.top, mark_r.right, mark_r.bottom, C_PAPER);
		frame(&mark_r, C_TEXT, 1);
		if ( c != NULL && c->mark ) {
			/* ✓ drawn as two strokes */
			INT	x = mark_r.left, y = mark_r.top, k;

			for ( k = 0; k < 4; k++ ) fill(x + 3 + k, y + 8 + k, x + 5 + k, y + 10 + k, C_PICK);
			for ( k = 0; k < 8; k++ ) fill(x + 7 + k, y + 11 - k, x + 9 + k, y + 13 - k, C_PICK);
		}
		if ( ed_kind() != ED_POS ) {
			fill(pos_r.left, pos_r.top, pos_r.right, pos_r.bottom, C_PAPER);
			frame(&pos_r, C_LINE, 1);
			snprintf(s, sizeof(s), "%d", total > 0 ? g_cur + 1 : 0);
			r = pos_r;
			r.right -= 4;
			(void)dp_text(g_cw.gid, r.right - dp_text_width((CONST UB *)s, 12), r.top + 17, (CONST UB *)s, C_TEXT, 12);
		}
		if ( g_db.searching ) snprintf(s, sizeof(s), "/%d 枚 (全%d)", total, g_db.n);
		else snprintf(s, sizeof(s), "/%d 枚", total);
		(void)dp_text(g_cw.gid, pos_r.right + 6, pos_r.top + 17, (CONST UB *)s, C_TEXT, 12);
		if ( g_db.searching ) (void)dp_text(g_cw.gid, btn[0].r.right + 70, pos_r.top + 17, (CONST UB *)"検索状態", C_PICK, 12);
	} else if ( g_screen == SCR_DESIGN ) {
		UB	nm[OB_NAME_MAX];
		TS_UUID	u;

		(void)dp_text(g_cw.gid, 8, slot_r.top + 17, (CONST UB *)"印刷書式", C_TEXT, 12);
		fill(slot_r.left, slot_r.top, slot_r.right, slot_r.bottom, C_PAPER);
		frame(&slot_r, C_LINE, 1);
		nm[0] = 0;
		if ( g_dslayout != NULL && link_target(g_dslayout, &u) ) name_of(&u, nm);
		text_in(&slot_r, g_dslayout != NULL ? ( nm[0] != 0 ? (char *)nm : "(実身がありません)" ) : "(なし)",
			g_dslayout != NULL ? C_TEXT : C_GREY, 12, FALSE);
	} else {
		(void)dp_text(g_cw.gid, btn[9].r.right + 12, 23, (CONST UB *)"検索条件", C_PICK, 12);
	}
}

/* ---------------------------------------------------------------- values */

/* Letters in the field's face, in a box: the kernel's text for a face of its own */
static void face_text( CTBUF *fig, CONST T_DPRECT *r, CONST UB *s, INT px, INT face, BOOL right )
{
	INT	w = dp_text_width(s, px);
	INT	x = right ? r->right - 4 - w : r->left + 4;
	INT	y = r->top + ( r->bottom - r->top + px ) / 2 - px / 8;

	if ( face <= MC_FACE_STD || face > MC_FACE_GOTHIC || fig == NULL ) {
		if ( !g_db.view.cprop ) {
			/* fixed pitch: a full width letter as wide as it is tall, a half one half that */
			INT	i = 0, n = (INT)strlen((CONST char *)s);

			x = r->left + 4;
			while ( i < n && x < r->right ) {
				UINT	c;
				UB	one[8];
				INT	l = mc_u8_get(s + i, n - i, &c);

				memcpy(one, s + i, (size_t)l);
				one[l] = 0;
				(void)dp_text(g_cw.gid, x, y, one, C_TEXT, px);
				x += mc_wide(c) ? px : px / 2;
				i += l;
			}
			return;
		}
		(void)dp_text(g_cw.gid, x, y, s, C_TEXT, px);
		return;
	}
	ct_put(fig, "<document>\n<docView viewleft=\"");
	ct_num(fig, r->left + 4);
	ct_put(fig, "\" viewtop=\"");
	ct_num(fig, r->top + ( r->bottom - r->top - px - 4 ) / 2);
	ct_put(fig, "\" viewright=\"");
	ct_num(fig, r->right - 4);
	ct_put(fig, "\" viewbottom=\"");
	ct_num(fig, r->bottom);
	ct_put(fig, "\"/>\n<docDraw drawleft=\"");
	ct_num(fig, r->left + 4);
	ct_put(fig, "\" drawtop=\"");
	ct_num(fig, r->top + ( r->bottom - r->top - px - 4 ) / 2);
	ct_put(fig, "\" drawright=\"");
	ct_num(fig, r->right - 4);
	ct_put(fig, "\" drawbottom=\"");
	ct_num(fig, r->bottom);
	ct_put(fig, "\"/>\n<docScale hunit=\"-72\" vunit=\"-72\"/>\n<text lang=\"0\" bpat=\"0\"/>\n<font size=\"");
	ct_num(fig, px);
	ct_put(fig, "\"/>\n<font color=\"#000000\"/>\n<font face=\"");
	ct_put(fig, face_font[face]);
	ct_put(fig, "\"/>\n");
	ct_esc(fig, s, (INT)strlen((CONST char *)s));
	ct_put(fig, "\n</document>\n");
}

/* Words cut at a letter's edge to fit width, with "…" when they were cut */
static void fit( CONST UB *s, INT px, INT width, UB *out, INT max )
{
	INT	n;

	snprintf((char *)out, (size_t)max - 4, "%s", (CONST char *)s);
	n = (INT)strlen((char *)out);
	if ( dp_text_width(out, px) <= width ) return;
	while ( n > 0 ) {
		do {
			n--;
		} while ( n > 0 && ( out[n] & 0xC0 ) == 0x80 );
		strcpy((char *)out + n, "…");
		if ( dp_text_width(out, px) <= width ) break;
	}
}

static void row_paint( INT k, CTBUF *fig )
{
	INT		t = row_top(k), rh = row_h(), px = card_px();
	T_DPRECT	r, b;
	char		s[16];
	UB		v[MC_VALUE_MAX + 8], w[MC_VALUE_MAX + 16];
	MCCARD		*c = cur_card();
	CONST MCFIELD	*f = &g_db.f[k];

	if ( t + rh < body_top() || t > g_cw.h ) return;
	snprintf(s, sizeof(s), "%d", k + 1);
	(void)dp_text(g_cw.gid, NUM_W - 4 - dp_text_width((CONST UB *)s, 12), t + ( rh + 12 ) / 2 - 1, (CONST UB *)s, C_GREY, 12);
	if ( g_db.view.names ) {
		r.left = NUM_W + 6;
		r.top = t + 2;
		r.right = r.left + name_w();
		r.bottom = t + rh - 2;
		text_in(&r, (CONST char *)f->name, C_TEXT, px, FALSE);
	}
	(void)card_box(( g_screen == SCR_FIND ) ? ED_COND : ED_VALUE, k, &b);
	if ( ed_on() && ed_index() == k && ( ed_kind() == ED_VALUE || ed_kind() == ED_COND ) ) return;
	fill(b.left, b.top, b.right, b.bottom, C_PAPER);
	frame(&b, ( k == g_field ) ? C_PICK : C_LINE, 1);
	if ( g_screen == SCR_FIND ) {
		if ( g_cond[k] != NULL ) {
			fit(g_cond[k], px, b.right - b.left - 8, w, sizeof(w));
			face_text(NULL, &b, w, px, MC_FACE_STD, FALSE);
		}
		return;
	}
	if ( c == NULL || mc_get(c, k) == NULL ) return;
	if ( f->type == MC_T_LINK ) {
		CONST UB	*el = mc_get(c, k);
		INT		box[4];
		T_DPRECT	lr;

		link_box(k, &lr);
		box[0] = lr.left;
		box[1] = lr.top;
		box[2] = lr.right;
		box[3] = lr.bottom;
		ct_link_put(fig, el, (INT)strlen((CONST char *)el), box, -1, FALSE);
		ct_put(fig, "\n");
		return;
	}
	(void)mc_shown(&g_db, c, k, v, sizeof(v));
	fit(v, px, b.right - b.left - 8, w, sizeof(w));
	face_text(fig, &b, w, px, ( f->type == MC_T_TEXT ) ? f->face : MC_FACE_STD, (BOOL)( f->type == MC_T_NUM ));
}

static void design_paint( void )
{
	INT	x[10], k, t, px = card_px(), rh = row_h();
	static CONST char *CONST head[6] = { "番号", "項目名", "種類", "最大文字数", "一覧表示幅", "書体" };
	T_DPRECT r;

	design_cols(x);
	t = TB_H + 4;
	fill(0, t, g_cw.w, t + rh, C_DESIGN);
	(void)dp_text(g_cw.gid, 4, t + rh / 2 + 5, (CONST UB *)head[0], C_TEXT, 11);
	(void)dp_text(g_cw.gid, x[0] + 2, t + rh / 2 + 5, (CONST UB *)head[1], C_TEXT, 11);
	(void)dp_text(g_cw.gid, x[2] + 2, t + rh / 2 + 5, (CONST UB *)head[2], C_TEXT, 11);
	(void)dp_text(g_cw.gid, x[4] + 2, t + rh / 2 + 5, (CONST UB *)head[3], C_TEXT, 11);
	(void)dp_text(g_cw.gid, x[6] + 2, t + rh / 2 + 5, (CONST UB *)head[4], C_TEXT, 11);
	(void)dp_text(g_cw.gid, x[8] + 2, t + rh / 2 + 5, (CONST UB *)head[5], C_TEXT, 11);
	for ( k = 0; k < g_dsn; k++ ) {
		MCFIELD	*f = &g_dsf[k];
		char	s[32];
		BOOL	blank = (BOOL)( f->name[0] == 0 );

		t = row_top(k);
		if ( t + rh < body_top() || t > g_cw.h ) continue;
		snprintf(s, sizeof(s), "%d", k + 1);
		(void)dp_text(g_cw.gid, NUM_W - 4 - dp_text_width((CONST UB *)s, 12), t + ( rh + 12 ) / 2 - 1, (CONST UB *)s, C_GREY, 12);
		if ( !( ed_on() && ed_kind() == ED_DNAME && ed_index() == k ) ) {
			(void)card_box(ED_DNAME, k, &r);
			fill(r.left, r.top, r.right, r.bottom, C_PAPER);
			frame(&r, C_LINE, 1);
			text_in(&r, (CONST char *)f->name, C_TEXT, px, FALSE);
		}
		if ( blank && g_dsfrom[k] < 0 ) continue;
		r.left = x[2];
		r.right = x[3];
		r.top = t + 2;
		r.bottom = t + rh - 2;
		fill(r.left, r.top, r.right, r.bottom, C_BUTTON);
		frame(&r, C_LINE, 1);
		text_in(&r, kind_name[f->type], blank ? C_GREY : C_TEXT, px, FALSE);
		(void)dp_text(g_cw.gid, r.right - 14, r.top + ( r.bottom - r.top + 10 ) / 2, (CONST UB *)"▼", C_GREY, 10);
		if ( !( ed_on() && ed_kind() == ED_DMAX && ed_index() == k ) ) {
			(void)card_box(ED_DMAX, k, &r);
			fill(r.left, r.top, r.right, r.bottom, C_PAPER);
			frame(&r, C_LINE, 1);
			snprintf(s, sizeof(s), "%d", f->max);
			text_in(&r, s, C_TEXT, px, FALSE);
		}
		if ( !( ed_on() && ed_kind() == ED_DWIDTH && ed_index() == k ) ) {
			(void)card_box(ED_DWIDTH, k, &r);
			fill(r.left, r.top, r.right, r.bottom, C_PAPER);
			frame(&r, C_LINE, 1);
			snprintf(s, sizeof(s), "%d", f->width);
			text_in(&r, s, C_TEXT, px, FALSE);
		}
		if ( f->type == MC_T_TEXT ) {
			r.left = x[8];
			r.right = x[9];
			r.top = t + 2;
			r.bottom = t + rh - 2;
			fill(r.left, r.top, r.right, r.bottom, C_BUTTON);
			frame(&r, C_LINE, 1);
			text_in(&r, face_label[f->face], C_TEXT, px, FALSE);
			(void)dp_text(g_cw.gid, r.right - 14, r.top + ( r.bottom - r.top + 10 ) / 2, (CONST UB *)"▼", C_GREY, 10);
		}
		if ( blank && g_dsfrom[k] >= 0 ) {
			(void)dp_text(g_cw.gid, x[9] + 8, t + ( rh + 12 ) / 2 - 1, (CONST UB *)"削除", 0x00CC0000U, 12);
		}
	}
}

static void bars( void )
{
	T_OBWBARS	b;
	SZ		asz = 0;

	memset(&b, 0, sizeof(b));
	b.bar[OB_BAR_R].lo = 0;
	b.bar[OB_BAR_R].hi = rows() * row_h() + 8;
	b.bar[OB_BAR_R].clo = g_sy;
	b.bar[OB_BAR_R].chi = g_sy + ( g_cw.h - body_top() );
	if ( b.bar[OB_BAR_R].chi > b.bar[OB_BAR_R].hi ) b.bar[OB_BAR_R].hi = b.bar[OB_BAR_R].chi;
	(void)ob_wri_rec(g_cw.kw, OB_WR_BARS, 0, &b, 2 * sizeof(T_OBWBAR), &asz);
}

void card_paint( void )
{
	CTBUF		fig;
	T_DPRECT	clip;
	INT		k;

	if ( !g_cw.open || g_cw.gid < 0 ) return;
	fill(0, 0, g_cw.w, g_cw.h, C_GROUND);
	ct_buf(&fig, g_fig, sizeof(g_fig));
	ct_put(&fig, "<tad version=\"1.0\" encoding=\"UTF-8\">\n<figure>\n");
	if ( g_screen == SCR_DESIGN ) {
		design_paint();
	} else {
		for ( k = 0; k < g_db.nf; k++ ) row_paint(k, &fig);
		if ( g_db.nf == 0 ) {
			(void)dp_text(g_cw.gid, 20, body_top() + 30, (CONST UB *)"項目がありません。〔画面〕の〔設計画面〕でカードを設計してください。",
				      C_GREY, 12);
		}
	}
	ct_put(&fig, "</figure>\n</tad>\n");
	clip.left = 0;
	clip.top = TB_H;
	clip.right = g_cw.w;
	clip.bottom = g_cw.h;
	if ( !fig.fail ) (void)dp_draw_tad(g_cw.gid, &clip, 0, 0, fig.p, fig.n);
	if ( g_link_on && g_field >= 0 && g_screen == SCR_CARD && mc_get(cur_card(), g_field) != NULL ) {
		T_DPRECT	r;

		link_box(g_field, &r);
		r.left -= 2;
		r.top -= 2;
		r.right += 2;
		r.bottom += 2;
		frame(&r, C_ORANGE, 2);
	}
	bar_paint();
	ed_draw();
	(void)wm_obj_flush(g_cw.kw, NULL);
	bars();
	g_cw.dirty = FALSE;
}

/* ---------------------------------------------------------------- typing put in */

static void undo_keep( MCCARD *c, INT f )
{
	CONST UB	*v = mc_get(c, f);

	free(undo_v);
	undo_v = ( v != NULL ) ? (UB *)strdup((CONST char *)v) : NULL;
	undo_card = c;
	undo_f = f;
	undo_on = TRUE;
}

/* 取り消し: the last value typed into the card window as it was */
BOOL card_undo( void )
{
	INT	i;

	if ( !undo_on ) return FALSE;
	for ( i = 0; i < g_db.n && g_db.c[i] != undo_card; i++ ) ;
	if ( i == g_db.n || undo_f >= g_db.nf ) {
		undo_on = FALSE;
		return FALSE;
	}
	{
		UB	*v = ( undo_v != NULL ) ? (UB *)strdup((CONST char *)undo_v) : NULL;
		UB	*now = ( mc_get(undo_card, undo_f) != NULL ) ? (UB *)strdup((CONST char *)mc_get(undo_card, undo_f)) : NULL;

		(void)mc_set(&g_db, undo_card, undo_f, v, v != NULL ? (INT)strlen((char *)v) : 0);
		free(undo_v);
		undo_v = now;		/* a second 取り消し puts it back */
		free(v);
	}
	g_changed = TRUE;
	redraw_all();
	note("undone");
	return TRUE;
}

void card_undo_forget( void )
{
	undo_on = FALSE;
}

static INT digits_of( CONST UB *s, INT n )
{
	UB	t[32];
	INT	k = mc_num_norm(s, n, t, sizeof(t)), i, v = 0;

	if ( k <= 0 || t[0] == '-' ) return -1;
	for ( i = 0; i < k && t[i] >= '0' && t[i] <= '9'; i++ ) v = v * 10 + ( t[i] - '0' );
	return v;
}

void card_commit( INT kind, INT k, CONST UB *s, INT n )
{
	char	line[200];

	switch ( kind ) {
	case ED_VALUE:
		{
			MCCARD		*c = cur_card();
			CONST UB	*was;
			ER		er;

			if ( c == NULL || k < 0 || k >= g_db.nf || g_db.f[k].type == MC_T_LINK ) return;
			was = mc_get(c, k);
			if ( ( was == NULL && n == 0 ) || ( was != NULL && (INT)strlen((CONST char *)was) == n && memcmp(was, s, (size_t)n) == 0 ) ) {
				return;
			}
			undo_keep(c, k);
			er = mc_set(&g_db, c, k, s, n);
			if ( er == E_PAR ) {
				warn("数値として読めない値です。", "数字、小数点、負符号で入力してください。");
				undo_on = FALSE;
				return;
			}
			if ( er == E_LIMIT ) {
				warn("数値の桁数が多すぎます。", "数値は20けたまでです。");
				undo_on = FALSE;
				return;
			}
			g_changed = TRUE;
			g_lw.dirty = TRUE;
			{
				UB	v[MC_VALUE_MAX + 8];

				(void)mc_shown(&g_db, c, k, v, sizeof(v));
				snprintf(line, sizeof(line), "set %d %d %.120s", g_cur + 1, k + 1, (char *)v);
				note(line);
			}
		}
		break;
	case ED_COND:
		if ( k < 0 || k >= MC_FIELD_MAX ) return;
		free(g_cond[k]);
		g_cond[k] = NULL;
		if ( n > 0 ) {
			g_cond[k] = malloc((size_t)n + 1);
			if ( g_cond[k] != NULL ) {
				memcpy(g_cond[k], s, (size_t)n);
				g_cond[k][n] = 0;
			}
		}
		snprintf(line, sizeof(line), "cond %d %.120s", k + 1, n > 0 ? (char *)g_cond[k] : "");
		note(line);
		break;
	case ED_DNAME:
		if ( k < 0 || k >= g_dsn ) return;
		{
			INT	m = ( n < MC_NAME_MAX - 1 ) ? n : MC_NAME_MAX - 1;

			while ( m > 0 && ( s[m] & 0xC0 ) == 0x80 ) m--;
			memcpy(g_dsf[k].name, s, (size_t)m);
			g_dsf[k].name[m] = 0;
		}
		/* a name in the empty row makes a field, and another empty row after it */
		if ( k == g_dsn - 1 && g_dsf[k].name[0] != 0 && g_dsfrom[k] < 0 && g_dsn <= MC_FIELD_MAX ) {
			if ( g_dsn < MC_FIELD_MAX ) {
				memset(&g_dsf[g_dsn], 0, sizeof(MCFIELD));
				g_dsf[g_dsn].type = MC_T_TEXT;
				g_dsf[g_dsn].max = 20;
				g_dsf[g_dsn].width = 10;
				g_dsf[g_dsn].face = MC_FACE_MINCHO;
				g_dsfrom[g_dsn] = -1;
				g_dsn++;
			}
		}
		snprintf(line, sizeof(line), "design name %d %.60s", k + 1, (char *)g_dsf[k].name);
		note(line);
		break;
	case ED_DMAX:
	case ED_DWIDTH:
		if ( k < 0 || k >= g_dsn ) return;
		{
			INT	v = digits_of(s, n);

			if ( v < 0 ) {
				warn("数字で入力してください。", NULL);
				return;
			}
			if ( kind == ED_DMAX ) g_dsf[k].max = v;
			else g_dsf[k].width = v;
		}
		break;
	case ED_POS:
		{
			INT	v = digits_of(s, n);

			if ( v >= 1 ) show_card(v - 1);
		}
		break;
	default:
		break;
	}
	g_cw.dirty = TRUE;
}

/* ---------------------------------------------------------------- 設計画面 */

void design_begin( void )
{
	INT	k;

	for ( k = 0; k < g_db.nf; k++ ) {
		g_dsf[k] = g_db.f[k];
		g_dsfrom[k] = k;
	}
	g_dsn = g_db.nf;
	if ( g_dsn < MC_FIELD_MAX ) {
		memset(&g_dsf[g_dsn], 0, sizeof(MCFIELD));
		g_dsf[g_dsn].type = MC_T_TEXT;
		g_dsf[g_dsn].max = 20;
		g_dsf[g_dsn].width = 10;
		g_dsf[g_dsn].face = MC_FACE_MINCHO;
		g_dsfrom[g_dsn] = -1;
		g_dsn++;
	}
	free(g_dslayout);
	g_dslayout = NULL;
	g_dslayoutlen = 0;
	if ( g_db.layout != NULL ) {
		g_dslayout = malloc((size_t)g_db.layoutlen + 1);
		if ( g_dslayout != NULL ) {
			memcpy(g_dslayout, g_db.layout, (size_t)g_db.layoutlen);
			g_dslayout[g_db.layoutlen] = 0;
			g_dslayoutlen = g_db.layoutlen;
		}
	}
}

BOOL design_apply( void )
{
	MCFIELD	nf[MC_FIELD_MAX];
	INT	from[MC_FIELD_MAX], n = 0, k, at, lost;
	BOOL	lose[MC_FIELD_MAX];
	char	s[160];
	ER	er;

	ed_commit();
	for ( k = 0; k < g_dsn; k++ ) {
		if ( g_dsf[k].name[0] == 0 ) continue;	/* an empty name: the field is deleted */
		if ( n >= MC_FIELD_MAX ) break;
		nf[n] = g_dsf[k];
		from[n] = g_dsfrom[k];
		n++;
	}
	er = mc_fields_check(nf, n, &at);
	if ( er < E_OK ) {
		snprintf(s, sizeof(s), "項目%d：%s", at + 1, (char *)nf[at >= 0 && at < n ? at : 0].name);
		warn(( er == E_OBJ ) ? "同じ項目名を2つ設定することはできません。"
		     : ( er == E_LIMIT ) ? "最大文字数か一覧表示幅が範囲を超えています（文字500、数値20、仮身30、表示幅50まで）。"
		     : "項目名は全角の8文字までで入力してください。", s);
		return FALSE;
	}
	lost = mc_format_loss(&g_db, nf, from, n, lose);
	for ( k = 0; k < g_dsn; k++ ) {
		if ( g_dsf[k].name[0] == 0 && g_dsfrom[k] >= 0 ) lost++;	/* a field taken away loses its data */
	}
	if ( lost > 0 && !dlg_format(lost) ) return FALSE;
	er = mc_format_set(&g_db, nf, from, n);
	if ( er < E_OK ) {
		warn("形式を変更できませんでした。", "メモリが足りません。");
		return FALSE;
	}
	free(g_db.layout);
	g_db.layout = NULL;
	g_db.layoutlen = 0;
	if ( g_dslayout != NULL ) {
		g_db.layout = (UB *)malloc((size_t)g_dslayoutlen + 1);
		if ( g_db.layout != NULL ) {
			memcpy(g_db.layout, g_dslayout, (size_t)g_dslayoutlen);
			g_db.layout[g_dslayoutlen] = 0;
			g_db.layoutlen = g_dslayoutlen;
		}
	}
	for ( k = n; k < MC_FIELD_MAX; k++ ) {
		free(g_cond[k]);
		g_cond[k] = NULL;
	}
	undo_on = FALSE;
	g_changed = TRUE;
	snprintf(s, sizeof(s), "designed %d fields", n);
	note(s);
	card_screen(SCR_CARD);
	return TRUE;
}

void design_cancel( void )
{
	ed_cancel();
	card_screen(SCR_CARD);
	note("design cancelled");
}

void card_screen( INT scr )
{
	BOOL	from_card = (BOOL)( g_screen == SCR_CARD );
	INT	k = ( ed_kind() == ED_VALUE ) ? ed_index() : -1;
	UB	picked[MC_VALUE_MAX + 8];
	INT	pn = 0;

	/* data chosen on the card goes with it to 検索条件画面 */
	if ( from_card && scr == SCR_FIND && k >= 0 ) pn = ed_text(picked, sizeof(picked));
	if ( ed_on() ) {
		if ( g_screen == SCR_DESIGN ) ed_cancel();
		else ed_commit();
	}
	if ( scr == SCR_DESIGN && g_screen != SCR_DESIGN ) design_begin();
	g_screen = scr;
	g_sy = 0;
	g_link_on = FALSE;
	if ( scr == SCR_FIND && pn > 0 && k >= 0 ) card_commit(ED_COND, k, picked, pn);
	if ( g_field >= rows() ) g_field = -1;
	note(scr == SCR_CARD ? "screen card" : scr == SCR_FIND ? "screen find" : "screen design");
	g_cw.dirty = TRUE;
}

/* ---------------------------------------------------------------- the pointer */

static INT row_at( INT y )
{
	INT	k;

	if ( y < body_top() ) return -1;
	k = ( y - body_top() + g_sy ) / row_h();
	return ( k >= 0 && k < rows() ) ? k : -1;
}

/* A menu of the definitions shown under a box; the item chosen, or -1 */
static INT pop( CONST char *def, INT x, INT y, UD when, CONST char *CONST *codes, INT n )
{
	TS_UUID	u;
	T_MNSEL	sel;
	ID	mid;
	INT	k, r = -1;

	if ( ui_uuid(def, &u) < E_OK || mn_cre_men(&u, &mid) < E_OK ) return -1;
	if ( mn_pop_men(mid, g_cw.kw, x, y, when, &sel) >= E_OK ) {
		for ( k = 0; k < n; k++ ) {
			if ( strcmp((CONST char *)sel.code, codes[k]) == 0 ) r = k;
		}
	}
	(void)mn_del_men(mid);
	return r;
}

static void begin_box( INT kind, INT k )
{
	UB	v[MC_VALUE_MAX + 8];
	char	s[16];

	if ( ed_on() && ed_kind() == kind && ed_index() == k ) return;
	ed_commit();
	g_field = ( kind == ED_VALUE || kind == ED_COND ) ? k : g_field;
	g_link_on = FALSE;
	v[0] = 0;
	switch ( kind ) {
	case ED_VALUE:
		if ( g_db.f[k].type == MC_T_LINK ) {
			g_cw.dirty = TRUE;
			return;		/* a link is not typed */
		}
		(void)mc_shown(&g_db, cur_card(), k, v, sizeof(v));
		break;
	case ED_COND:
		if ( g_cond[k] != NULL ) snprintf((char *)v, sizeof(v), "%s", (char *)g_cond[k]);
		break;
	case ED_DNAME:
		snprintf((char *)v, sizeof(v), "%s", (char *)g_dsf[k].name);
		break;
	case ED_DMAX:
	case ED_DWIDTH:
		snprintf(s, sizeof(s), "%d", ( kind == ED_DMAX ) ? g_dsf[k].max : g_dsf[k].width);
		snprintf((char *)v, sizeof(v), "%s", s);
		break;
	case ED_POS:
		snprintf((char *)v, sizeof(v), "%d", g_cur + 1);
		break;
	default:
		break;
	}
	if ( kind != ED_POS ) card_scroll_to(k);
	ed_begin(kind, k, v, TRUE);
}

/* A sign of the bar put in the box in hand on 検索条件画面 */
static void sign_put( INT i )
{
	static CONST char *CONST sign[10] = { "=", "≦", "≧", "<", ">", "≠", "≒", "/", "?", "*" };

	if ( !ed_on() || ed_kind() != ED_COND ) {
		begin_box(ED_COND, g_field >= 0 ? g_field : 0);
		ed_caret_at(10000);
	}
	ed_insert((CONST UB *)sign[i], (INT)strlen(sign[i]));
}

static void press_bar( CONST T_OBNTM *m )
{
	INT	i, x = m->x, y = m->y;

	for ( i = 0; i < nbtn; i++ ) {
		BTN	*b = &btn[i];

		if ( x < b->r.left || x >= b->r.right || y < b->r.top || y >= b->r.bottom ) continue;
		if ( strncmp(b->code, "sign.", 5) == 0 ) {
			sign_put(b->code[5] - '0');
		} else if ( strcmp(b->code, "go.prev") == 0 || strcmp(b->code, "go.next") == 0 ) {
			BOOL	twice = (BOOL)( m->when - press_at < DBL_NS && press_f == -10 - i );

			press_at = m->when;
			press_f = -10 - i;
			/* pressed twice: to the first or the last card */
			if ( twice ) command(b->code[3] == 'p' ? "go.first" : "go.last", 0);
			else command(b->code, 0);
		} else {
			command(b->code, 0);
		}
		g_cw.dirty = TRUE;
		return;
	}
	if ( g_screen == SCR_CARD && x >= mark_r.left - 2 && x < mark_r.right + 2 && y >= mark_r.top - 2 && y < mark_r.bottom + 2 ) {
		command("mark.flip", 0);
		return;
	}
	if ( g_screen == SCR_CARD && x >= pos_r.left && x < pos_r.right && y >= pos_r.top && y < pos_r.bottom ) {
		begin_box(ED_POS, 0);
		return;
	}
	if ( g_screen == SCR_DESIGN && x >= slot_r.left && x < slot_r.right && y >= slot_r.top && y < slot_r.bottom ) {
		BOOL	twice = (BOOL)( m->when - press_at < DBL_NS && press_f == -2 );

		press_at = m->when;
		press_f = -2;
		if ( twice && g_dslayout != NULL ) link_open_el(g_dslayout);
		g_field = -2;		/* the 印刷書式 is taken: Delete takes it away */
		g_cw.dirty = TRUE;
	}
}

static void press_design( CONST T_OBNTM *m, INT k )
{
	INT	x[10];

	design_cols(x);
	if ( m->x >= x[0] && m->x < x[1] ) {
		begin_box(ED_DNAME, k);
		return;
	}
	if ( g_dsf[k].name[0] == 0 && g_dsfrom[k] < 0 ) return;
	if ( m->x >= x[2] && m->x < x[3] ) {
		static CONST char *CONST codes[3] = { "kind.text", "kind.number", "kind.link" };
		INT	r;

		ed_commit();
		r = pop(SYSDEF_MENU_MC_KIND, x[2], row_top(k) + row_h(), m->when, codes, 3);
		if ( r >= 0 ) {
			MCFIELD	*f = &g_dsf[k];

			f->type = r;
			if ( r == MC_T_NUM && ( f->max < 1 || f->max > MC_NUM_MAX ) ) f->max = 10;
			if ( r == MC_T_LINK && ( f->max < 1 || f->max > MC_LINK_LETTERS ) ) f->max = 20;
			if ( r == MC_T_TEXT && f->max < 1 ) f->max = 20;
			note(r == 0 ? "design kind text" : r == 1 ? "design kind number" : "design kind link");
		}
	} else if ( m->x >= x[4] && m->x < x[5] ) {
		begin_box(ED_DMAX, k);
	} else if ( m->x >= x[6] && m->x < x[7] ) {
		begin_box(ED_DWIDTH, k);
	} else if ( m->x >= x[8] && m->x < x[9] && g_dsf[k].type == MC_T_TEXT ) {
		static CONST char *CONST codes[3] = { "face.standard", "face.mincho", "face.gothic" };
		INT	r;

		ed_commit();
		r = pop(SYSDEF_MENU_MC_FACE, x[8], row_top(k) + row_h(), m->when, codes, 3);
		if ( r >= 0 ) g_dsf[k].face = r;
	}
	g_cw.dirty = TRUE;
}

static void press( CONST T_OBNTM *m )
{
	INT	x = m->x, y = m->y, k;

	if ( x < 0 || y < 0 || x >= g_cw.w || y >= g_cw.h ) return;
	if ( ed_on() && ed_press(x, y) ) return;
	if ( y < TB_H ) {
		press_bar(m);
		return;
	}
	k = row_at(y);
	if ( k < 0 ) {
		ed_commit();
		return;
	}
	if ( g_screen == SCR_DESIGN ) {
		press_design(m, k);
		return;
	}
	if ( g_screen == SCR_FIND ) {
		T_DPRECT	r;

		if ( card_box(ED_COND, k, &r) && x >= r.left ) {
			if ( ed_on() && ed_kind() == ED_COND && ed_index() == k ) ed_caret_at(x);
			else begin_box(ED_COND, k);
		}
		return;
	}
	/* a link in its field: taken, opened, carried */
	if ( g_db.f[k].type == MC_T_LINK ) {
		BOOL	twice = (BOOL)( m->when - press_at < DBL_NS && press_f == k );

		ed_commit();
		press_at = m->when;
		press_f = k;
		g_field = k;
		if ( field_at_link(x, y) == k ) {
			g_link_on = TRUE;
			hold_link = TRUE;
			hold_twice = twice;
			hold_moved = FALSE;
			hold_x = x;
			hold_y = y;
		} else {
			g_link_on = FALSE;
		}
		g_cw.dirty = TRUE;
		return;
	}
	{
		T_DPRECT	r;

		if ( card_box(ED_VALUE, k, &r) && x >= r.left ) {
			if ( ed_on() && ed_kind() == ED_VALUE && ed_index() == k ) ed_caret_at(x);
			else begin_box(ED_VALUE, k);
		} else {
			ed_commit();
			g_field = k;
			g_cw.dirty = TRUE;
		}
	}
}

static void moved( CONST T_OBNTM *m )
{
	if ( !hold_link ) return;
	if ( m->x - hold_x >= MOVE_MIN || hold_x - m->x >= MOVE_MIN || m->y - hold_y >= MOVE_MIN || hold_y - m->y >= MOVE_MIN ) {
		hold_moved = TRUE;
	}
	if ( m->x < 0 || m->y < 0 || m->x >= g_cw.w || m->y >= g_cw.h ) {
		hold_link = FALSE;
		carry_link_out(hold_x, hold_y, hold_twice);
	}
}

static void released( CONST T_OBNTM *m )
{
	if ( hold_link ) {
		hold_link = FALSE;
		if ( !hold_moved && hold_twice ) {
			CONST UB	*el = mc_get(cur_card(), g_field);

			if ( el != NULL ) link_open_el(el);
		}
	}
}

/* ---------------------------------------------------------------- keys */

static INT next_kind( void )
{
	return ( g_screen == SCR_FIND ) ? ED_COND : ED_VALUE;
}

/* The box to go to from the field in hand, k rows on (wrapping when told) */
static void go_row( INT k, BOOL wrap )
{
	INT	n, kind = ( g_screen == SCR_DESIGN ) ? ( ed_on() ? ed_kind() : ED_DNAME ) : next_kind();

	/* what is typed put in first: a name in the last row of 設計画面 adds a row */
	ed_commit();
	n = rows();
	if ( n <= 0 ) return;
	if ( wrap ) k = ( k % n + n ) % n;
	if ( k < 0 ) k = 0;
	if ( k >= n ) k = n - 1;
	if ( g_screen == SCR_DESIGN && kind != ED_DNAME && g_dsf[k].name[0] == 0 && g_dsfrom[k] < 0 ) kind = ED_DNAME;
	if ( g_screen == SCR_CARD && g_db.f[k].type == MC_T_LINK ) {
		ed_commit();
		g_field = k;
		g_link_on = FALSE;
		card_scroll_to(k);
		return;
	}
	begin_box(kind, k);
}

void card_key( CONST T_OBNTM *m )
{
	UINT	mods = m->mods, code = m->code;
	BOOL	shift = (BOOL)( ( mods & M_SHIFT ) != 0 );
	BOOL	alt = (BOOL)( ( mods & M_ALT ) != 0 );
	BOOL	lctrl = (BOOL)( ( mods & HID_MOD_LCTRL ) != 0 );
	INT	cur = ed_on() ? ed_index() : ( g_screen == SCR_DESIGN ? 0 : g_field );
	INT	page = ( g_cw.h - body_top() ) / row_h();

	if ( page < 1 ) page = 1;
	if ( code == K_F10 || code == 0xE4 ) {
		/* F10 or the right Ctrl: what is typed put in; on 設計画面, 設定 */
		ed_commit();
		if ( g_screen == SCR_DESIGN ) (void)design_apply();
		return;
	}
	if ( ed_on() && ed_kind() == ED_POS ) {
		if ( code == K_ENTER || code == K_KPENTER || code == K_TAB ) {
			ed_commit();
			return;
		}
		(void)ed_key(code, mods);
		return;
	}
	if ( ed_on() && !( alt || lctrl ) && ed_key(code, mods) ) return;
	if ( ( mods & M_CTRL ) != 0 && !( lctrl && ( code == K_UP || code == K_DOWN ) ) ) {
		UB	c = wm_key_char(code, 0);

		switch ( c ) {
		case 'e': case 'E':	command("close", 0);	break;
		case 's': case 'S':	command("save", 0);	break;
		case 'c': case 'C':	command("copy", 0);	break;
		case 'v': case 'V':	command("paste", 0);	break;
		case 'x': case 'X':	command("cut", 0);	break;
		case 'z': case 'Z':	command("undo", 0);	break;
		case 'n': case 'N':	command("card.new", 0);	break;
		case 'd': case 'D':	command("card.dup", 0);	break;
		case 'l': case 'L':	command("list", 0);	break;
		case 'f': case 'F':	command("scr.find", 0);	break;
		case 'p': case 'P':	command("print", 0);	break;
		default:		break;
		}
		return;
	}
	switch ( code ) {
	case K_ENTER:
	case K_KPENTER:
		go_row(cur + ( shift ? -1 : 1 ), FALSE);
		return;
	case K_DOWN:
		if ( alt ) go_row(rows() - 1, FALSE);
		else if ( lctrl ) go_row(cur + page, FALSE);
		else go_row(cur + 1, FALSE);
		return;
	case K_UP:
		if ( alt ) go_row(0, FALSE);
		else if ( lctrl ) go_row(cur - page, FALSE);
		else go_row(cur - 1, FALSE);
		return;
	case K_TAB:
		if ( g_screen == SCR_DESIGN && ed_on() ) {
			/* across the row: name, size, width */
			static CONST INT order[3] = { ED_DNAME, ED_DMAX, ED_DWIDTH };
			INT	i, k = ed_index();

			for ( i = 0; i < 3 && order[i] != ed_kind(); i++ ) ;
			i = ( i + ( shift ? 2 : 1 ) ) % 3;
			if ( g_dsf[k].name[0] == 0 && g_dsfrom[k] < 0 ) i = 0;
			begin_box(order[i], k);
			return;
		}
		go_row(cur + ( shift ? -1 : 1 ), TRUE);
		return;
	case K_PGUP:
		if ( g_screen == SCR_CARD ) command("go.prev", 0);
		return;
	case K_PGDN:
		if ( g_screen == SCR_CARD ) command("go.next", 0);
		return;
	case K_DEL:
	case K_BS:
		if ( g_screen == SCR_CARD && g_link_on && g_field >= 0 ) {
			command("clear.link", 0);
		} else if ( g_screen == SCR_DESIGN && g_field == -2 && g_dslayout != NULL ) {
			free(g_dslayout);
			g_dslayout = NULL;
			g_dslayoutlen = 0;
			note("layout cleared");
			g_cw.dirty = TRUE;
		}
		return;
	case K_ESC:
		g_link_on = FALSE;
		g_cw.dirty = TRUE;
		return;
	default:
		break;
	}
	{
		UB	c = wm_key_char(code, mods);

		if ( c >= 0x20 && c < 0x7F && !ed_on() ) {
			/* a letter begins typing in the field in hand, through the converter like the rest */
			INT	k = ( g_field >= 0 ) ? g_field : 0;

			if ( rows() == 0 ) return;
			if ( g_screen == SCR_DESIGN ) {
				ed_begin(ED_DNAME, k, NULL, FALSE);
			} else if ( g_screen == SCR_FIND ) {
				begin_box(ED_COND, k);
			} else {
				if ( g_db.f[k].type == MC_T_LINK ) return;
				begin_box(ED_VALUE, k);
			}
			(void)ed_key(code, mods);
		}
	}
}

/* ---------------------------------------------------------------- dropped */

void card_dropped( void )
{
	static T_OBDROP	d;
	INT		k;
	UB		*el;
	INT		len = 0;

	if ( ui_drop(&g_ui, &d) < E_OK ) return;
	if ( d.n <= 0 ) return;
	if ( g_screen == SCR_DESIGN ) {
		if ( d.y < TB_H ) {
			el = link_el_from_drop(&d.v[0], &len);
			if ( el != NULL ) {
				free(g_dslayout);
				g_dslayout = el;
				g_dslayoutlen = len;
				ui_drop_answer(&g_ui, &d, OB_DR_ACCEPT, "");
				note("layout dropped");
				g_cw.dirty = TRUE;
				return;
			}
		}
		ui_drop_answer(&g_ui, &d, OB_DR_REFUSE, "印刷書式の欄に置いてください");
		return;
	}
	k = row_at(d.y);
	if ( g_screen != SCR_CARD || k < 0 || g_db.f[k].type != MC_T_LINK || cur_card() == NULL ) {
		ui_drop_answer(&g_ui, &d, OB_DR_REFUSE, "仮身の項目に置いてください");
		return;
	}
	if ( mc_link_count(&g_db) >= MC_LINK_MAX ) {
		ui_drop_answer(&g_ui, &d, OB_DR_REFUSE, "これ以上仮身を格納できません");
		return;
	}
	el = link_el_from_drop(&d.v[0], &len);
	if ( el == NULL ) {
		ui_drop_answer(&g_ui, &d, OB_DR_REFUSE, "");
		return;
	}
	ui_drop_answer(&g_ui, &d, OB_DR_ACCEPT, "");
	undo_keep(cur_card(), k);
	(void)mc_set(&g_db, cur_card(), k, el, len);
	free(el);
	g_field = k;
	g_link_on = TRUE;
	g_changed = TRUE;
	g_lw.dirty = TRUE;
	{
		char	s[100];
		UB	nm[OB_NAME_MAX];

		nm[0] = 0;
		(void)mc_shown(&g_db, cur_card(), k, nm, sizeof(nm));
		snprintf(s, sizeof(s), "link put %d %s", k + 1, (char *)nm);
		note(s);
	}
	g_cw.dirty = TRUE;
}

/* ---------------------------------------------------------------- what happens */

void card_event( CONST T_OBNTM *m )
{
	switch ( m->event ) {
	case OB_E_PRESS:
		if ( m->code == 0 ) press(m);
		break;
	case OB_E_RELEASE:
		if ( m->code == 0 ) released(m);
		break;
	case OB_E_MOVE:
		moved(m);
		break;
	case OB_E_LEAVE:
		if ( hold_link ) {
			hold_link = FALSE;
			carry_link_out(hold_x, hold_y, hold_twice);
		}
		break;
	case OB_E_KEY:
		card_key(m);
		break;
	case OB_E_WHEEL:
		g_sy -= m->dz * 3 * row_h();
		if ( g_sy > rows() * row_h() - row_h() ) g_sy = rows() * row_h() - row_h();
		if ( g_sy < 0 ) g_sy = 0;
		g_cw.dirty = TRUE;
		break;
	case OB_E_SCROLL:
		if ( m->code == OB_BAR_R ) {
			g_sy = m->x;
			if ( g_sy < 0 ) g_sy = 0;
			g_cw.dirty = TRUE;
		}
		break;
	case OB_E_DROP:
		card_dropped();
		break;
	default:
		break;
	}
}
