/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	calc_edit.c
 *	基本表計算: a cell's words typed (design 11.13)
 *
 *	A cell is typed into where it is: a box over it, as wide as the
 *	words need, with the caret. Typing on a cell taken begins with what
 *	is typed; Enter, F2 or a second press begins with what the cell holds
 *	(its formula, or its words), all of it taken, so that the first
 *	letter typed replaces it. Enter puts what is typed in the cell and
 *	takes the cell below, Tab the cell to the right; Esc leaves the cell
 *	as it was.
 *
 *	The keys go to a session of the system's かな漢字変換 in the input
 *	mode the message line shows: what it commits goes in at the caret,
 *	what it is still working on is drawn at the caret, each clause
 *	underlined and the one being converted turned over, with the
 *	candidates listed under it while they are chosen. In 英語 a letter
 *	goes in as it is, half width whatever the mode says of width: a
 *	number or a formula is typed in the letters a formula is read in.
 *
 *	The same words can be typed in the box of the 操作パネル instead
 *	(calc_panel.c): pressed there, or begun with fx, the box shows them
 *	with the caret and what is being converted, and the candidates are
 *	listed in the sheet's window under the box. Enter there puts them in
 *	the cell and leaves the cell in hand where it is. While a cell is
 *	typed into where it is, the box shows the same words.
 */

#include "calc.h"
#include <ts/hid.h>
#include <ts/wm.h>

#define LIST_MAX	4096
#define LIST_ROWS	9
#define IME_GROUND	0x00FFFFFFU
#define IME_INK		0x00000000U
#define IME_TARGET	0x000050A0U
#define IME_FRAME	0x00808080U

static BOOL	on;
static INT	ecol, erow;
static UB	buf[TEXT_MAX];
static INT	len, caret;
static BOOL	all;			/* everything is taken: the next letter replaces it */

static INT	kid;
static T_KCOUT	out;
static INT	base;
static BOOL	list;
static UB	cand[LIST_MAX];
static INT	ncand, first, total, chosen;
static T_DPRECT	listbox;
static INT	row_h;

static BOOL	field;			/* typed in the panel's box, not over the cell */
static INT	list_x, list_y;		/* where the candidates stand then, in the window */
static INT	shown_from;		/* the first letter the box showed */

BOOL ed_on( void )
{
	return on;
}

static BOOL busy( void )
{
	return (BOOL)( kid > 0 && out.cl[out.n_cl] > base );
}

/* UTF-8 put in at the caret */
static void insert( CONST UB *s, INT n )
{
	if ( all ) {
		len = caret = 0;
		all = FALSE;
	}
	if ( len + n >= TEXT_MAX - 1 ) return;
	memmove(buf + caret + n, buf + caret, (size_t)( len - caret ));
	memcpy(buf + caret, s, (size_t)n);
	len += n;
	caret += n;
	buf[len] = 0;
	g_dirty = TRUE;
}

static void take( INT r )
{
	base = 0;
	if ( out.n_out > 0 ) {
		INT	end = out.cl[out.n_out];

		if ( end > 0 ) insert((CONST UB *)out.text, end);
		base = end;
	}
	list = FALSE;
	if ( ( r & TSMOZC_LIST ) != 0 && busy() ) {
		INT	sel = kc_list(kid, cand, LIST_MAX, &ncand, &first, &total);

		if ( sel > 0 && ncand > 0 ) {
			list = TRUE;
			chosen = sel;
		}
	}
	g_dirty = TRUE;
}

/* A letter as it is */
static BOOL plain( UB ch )
{
	if ( ch < 0x20 || ch >= 0x7F ) return FALSE;
	insert(&ch, 1);
	return TRUE;
}

/* Whatever is being composed, committed */
static void commit_ime( void )
{
	INT	r;

	if ( !busy() ) {
		list = FALSE;
		return;
	}
	r = kc_hid_key(kid, K_ENTER, HID_MOD_LSHIFT, &out);
	if ( r >= 0 && r != KC_NOTMINE ) take(r);
	base = out.cl[out.n_cl];
	list = FALSE;
}

void ed_begin( INT col, INT row, CONST UB *first_letters, BOOL keep )
{
	CONST CXCELL	*c = cx_get(&g_s, col, row);

	on = TRUE;
	field = FALSE;
	shown_from = 0;
	ecol = col;
	erow = row;
	len = caret = 0;
	all = FALSE;
	buf[0] = 0;
	if ( keep && c != NULL && c->kind == CX_TEXT ) {
		snprintf((char *)buf, sizeof(buf), "%s", (CONST char *)c->text);
		len = caret = (INT)strlen((char *)buf);
		all = TRUE;
	}
	if ( first_letters != NULL ) insert(first_letters, (INT)strlen((CONST char *)first_letters));
	if ( kid <= 0 ) {
		kid = kc_open();
		if ( kid <= 0 ) kid = 0;
	}
	memset(&out, 0, sizeof(out));
	base = 0;
	list = FALSE;
	scroll_to(col, row);
	g_dirty = TRUE;
}

void ed_commit( void )
{
	if ( !on ) return;
	commit_ime();
	on = FALSE;
	field = FALSE;
	buf[len] = 0;
	cell_text(ecol, erow, buf);
	g_dirty = TRUE;
}

void ed_cancel( void )
{
	if ( !on ) return;
	if ( busy() ) (void)kc_hid_key(kid, K_ESC, 0, &out);
	memset(&out, 0, sizeof(out));
	base = 0;
	list = FALSE;
	on = FALSE;
	field = FALSE;
	g_dirty = TRUE;
}

void ed_end( void )
{
	if ( kid > 0 ) kc_close(kid);
	kid = 0;
}

/* The caret moved over one letter either way */
static INT step( INT at, INT d )
{
	if ( d < 0 ) {
		if ( at <= 0 ) return 0;
		do {
			at--;
		} while ( at > 0 && ( buf[at] & 0xC0 ) == 0x80 );
		return at;
	}
	if ( at >= len ) return len;
	do {
		at++;
	} while ( at < len && ( buf[at] & 0xC0 ) == 0x80 );
	return at;
}

BOOL ed_key( UINT key, UINT mods )
{
	UINT	mode = kc_mode();
	UB	ch = wm_key_char(key, mods);
	INT	r;

	if ( !on ) return FALSE;
	if ( ( mods & M_CTRL ) != 0 ) return FALSE;

	/* the converter first, while it is composing */
	if ( kid > 0 && ( mode & WM_MODE_ALPH ) == 0 ) {
		if ( list && ch >= '1' && ch <= '9' && ch - '1' < ncand ) {
			r = kc_choose(kid, first + ( ch - '1' ), &out);
			if ( r >= 0 && r != KC_NOTMINE ) take(r);
			return TRUE;
		}
		if ( busy() || ( ch > 0x20 && ch < 0x7F ) ) {
			r = kc_hid_key(kid, key, mods, &out);
			if ( r >= 0 && r != KC_NOTMINE ) {
				take(r);
				return TRUE;
			}
			if ( busy() ) return TRUE;
		}
	}
	switch ( key ) {
	case K_ENTER:
	case K_KPENTER:
		if ( field ) {
			ed_commit();		/* the box's ✓: the cell in hand stays */
			return TRUE;
		}
		ed_commit();
		if ( erow < CX_ROWS_MAX ) g_row = g_arow = erow + 1;
		g_col = g_acol = ecol;
		cx_grow(&g_s, g_col, g_row);
		scroll_to(g_col, g_row);
		return TRUE;
	case K_TAB:
		ed_commit();
		if ( ecol < CX_COLS_MAX ) g_col = g_acol = ecol + 1;
		g_row = g_arow = erow;
		cx_grow(&g_s, g_col, g_row);
		scroll_to(g_col, g_row);
		return TRUE;
	case K_ESC:
		ed_cancel();
		return TRUE;
	case K_BS:
		if ( all ) {
			len = caret = 0;
			all = FALSE;
		} else if ( caret > 0 ) {
			INT	at = step(caret, -1);

			memmove(buf + at, buf + caret, (size_t)( len - caret ));
			len -= caret - at;
			caret = at;
		}
		buf[len] = 0;
		g_dirty = TRUE;
		return TRUE;
	case K_DEL:
		if ( all ) {
			len = caret = 0;
			all = FALSE;
		} else if ( caret < len ) {
			INT	at = step(caret, 1);

			memmove(buf + caret, buf + at, (size_t)( len - at ));
			len -= at - caret;
		}
		buf[len] = 0;
		g_dirty = TRUE;
		return TRUE;
	case K_LEFT:
		all = FALSE;
		caret = step(caret, -1);
		g_dirty = TRUE;
		return TRUE;
	case K_RIGHT:
		all = FALSE;
		caret = step(caret, 1);
		g_dirty = TRUE;
		return TRUE;
	case K_HOME:
		all = FALSE;
		caret = 0;
		g_dirty = TRUE;
		return TRUE;
	case K_END:
		all = FALSE;
		caret = len;
		g_dirty = TRUE;
		return TRUE;
	default:
		break;
	}
	if ( ( mode & WM_MODE_ALPH ) != 0 && ( mode & WM_MODE_KANA ) != 0
	  && ( ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' ) ) ) {
		ch ^= 0x20;		/* 英語 in capitals: the shift turns it back */
	}
	if ( ch >= 0x20 && ch < 0x7F ) return plain(ch);
	return TRUE;
}

BOOL ed_press( INT x, INT y )
{
	if ( list && row_h > 0 && x >= listbox.left && x < listbox.right && y >= listbox.top && y < listbox.bottom ) {
		INT	row = ( y - listbox.top - 2 ) / row_h, r;

		if ( row >= 0 && row < ncand ) {
			r = kc_choose(kid, first + row, &out);
			if ( r >= 0 && r != KC_NOTMINE ) take(r);
		}
		return TRUE;
	}
	return FALSE;
}

static void piece( INT a, INT b, UB *dst, INT size )
{
	INT	n = 0;

	for ( ; a < b && n < size - 1; a++ ) dst[n++] = (UB)out.text[a];
	dst[n] = 0;
}

/* The composition at (x, y) in the environment gid, its letters px tall in a line lh high */
static void ime_text( INT gid, INT x, INT y, INT px, INT lh )
{
	UB		s[TSMOZC_TEXT_MAX];
	T_DPRECT	r;
	INT		i, cx = x, end;

	if ( !busy() ) return;
	end = out.cl[out.n_cl];
	piece(base, end, s, sizeof(s));
	r.left = x;
	r.top = y;
	r.right = x + dp_text_width(s, px) + 2;
	r.bottom = y + lh;
	(void)dp_fill_rect(gid, &r, IME_GROUND);
	for ( i = out.n_out; i < out.n_cl; i++ ) {
		INT		a = out.cl[i], b = out.cl[i + 1], w;
		BOOL		target = (BOOL)( i == out.clause && !out.yomi );
		T_DPRECT	u;

		if ( b <= base ) continue;
		if ( a < base ) a = base;
		piece(a, b, s, sizeof(s));
		w = dp_text_width(s, px);
		if ( target ) {
			u.left = cx;
			u.top = y;
			u.right = cx + w;
			u.bottom = y + lh;
			(void)dp_fill_rect(gid, &u, IME_TARGET);
		}
		(void)dp_text(gid, cx, y + px + 1, s, target ? IME_GROUND : IME_INK, px);
		u.left = cx + 1;
		u.right = cx + w - 1;
		u.top = y + lh - 2;
		u.bottom = y + lh - 1;
		(void)dp_fill_rect(gid, &u, IME_INK);
		cx += w;
	}
}

/* The candidates, in the sheet's window, from (x, y) down */
static void ime_list( INT x, INT y, INT px, INT lh )
{
	CONST UB	*c = cand;
	INT		w = 0, k, i;
	UB		row[TSMOZC_TEXT_MAX + 8];
	T_DPRECT	r;

	if ( !list ) return;
	row_h = lh + 2;
	for ( k = 0; k < ncand && k < LIST_ROWS; k++ ) {
		INT	cw;

		snprintf((char *)row, sizeof(row), "%d %s", k + 1, (CONST char *)c);
		cw = dp_text_width(row, px);
		if ( cw > w ) w = cw;
		c += strlen((CONST char *)c) + 1;
	}
	listbox.left = x;
	listbox.top = y;
	listbox.right = x + w + 8;
	listbox.bottom = listbox.top + row_h * k + 4;
	(void)dp_fill_rect(g_gid, &listbox, IME_GROUND);
	(void)dp_frame_rect(g_gid, &listbox, IME_FRAME, 1);
	c = cand;
	for ( i = 0; i < k; i++ ) {
		BOOL	sel = (BOOL)( first + i == chosen );

		snprintf((char *)row, sizeof(row), "%d %s", i + 1, (CONST char *)c);
		if ( sel ) {
			r.left = listbox.left + 1;
			r.right = listbox.right - 1;
			r.top = listbox.top + 2 + i * row_h;
			r.bottom = r.top + row_h;
			(void)dp_fill_rect(g_gid, &r, IME_TARGET);
		}
		(void)dp_text(g_gid, listbox.left + 4, listbox.top + 2 + i * row_h + px, row,
			      sel ? IME_GROUND : IME_INK, px);
		c += strlen((CONST char *)c) + 1;
	}
}

void ed_draw( void )
{
	T_DPRECT	r, box;
	UB		head[TEXT_MAX];
	INT		w, cx, px = 12, y;

	if ( !on ) return;
	if ( field ) {
		/* typed in the panel's box: only the candidates are the window's */
		if ( busy() ) ime_list(list_x, list_y, px, 18);
		return;
	}
	(void)cell_box(ecol, erow, &r);
	w = dp_text_width(buf, px) + 12;
	if ( busy() ) {
		UB	s[TSMOZC_TEXT_MAX];

		piece(base, out.cl[out.n_cl], s, sizeof(s));
		w += dp_text_width(s, px);
	}
	box = r;
	if ( box.right - box.left < w ) box.right = box.left + w;
	(void)dp_fill_rect(g_gid, &box, all ? C_CUR_FILL : C_PAPER);
	(void)dp_frame_rect(g_gid, &box, C_PICK, 2);
	y = r.top + ( r.bottom - r.top + px ) / 2 - px / 8;
	(void)dp_text(g_gid, box.left + 4, y, buf, C_TEXT, px);
	memcpy(head, buf, (size_t)caret);
	head[caret] = 0;
	cx = box.left + 4 + dp_text_width(head, px);
	if ( busy() ) {
		ime_text(g_gid, cx, r.top + 2, px, r.bottom - r.top - 4);
		ime_list(cx, r.bottom, px, r.bottom - r.top - 4);
	} else {
		T_DPRECT	c;

		c.left = cx;
		c.right = cx + 1;
		c.top = r.top + 3;
		c.bottom = r.bottom - 3;
		(void)dp_fill_rect(g_gid, &c, C_TEXT);
	}
}

/* ---------------------------------------------------------------- the panel's box */

BOOL ed_in_field( void )
{
	return (BOOL)( on && field );
}

/* What is typed goes on in the panel's box; fx begins a formula there */
void ed_to_field( BOOL formula )
{
	if ( !on ) ed_begin(g_col, g_row, NULL, TRUE);
	field = TRUE;
	all = FALSE;
	if ( formula && buf[0] != '=' && len + 1 < TEXT_MAX - 1 ) {
		memmove(buf + 1, buf, (size_t)len + 1);
		buf[0] = '=';
		len++;
		caret = len;
	}
	g_dirty = TRUE;
}

void ed_field_put( INT x, INT y )
{
	list_x = x;
	list_y = y;
}

/* The width of the letters from a to b */
static INT width_of( INT a, INT b, INT px )
{
	UB	s[TEXT_MAX];

	if ( b <= a ) return 0;
	memcpy(s, buf + a, (size_t)( b - a ));
	s[b - a] = 0;
	return dp_text_width(s, px);
}

#define FIELD_PX	12

void ed_field_caret( INT x )
{
	INT	at = shown_from;

	if ( !on || !field ) return;
	commit_ime();
	while ( at < len ) {
		INT	next = step(at, 1);
		INT	a = width_of(shown_from, at, FIELD_PX), b = width_of(shown_from, next, FIELD_PX);

		if ( x < ( a + b ) / 2 ) break;
		at = next;
	}
	caret = at;
	all = FALSE;
	g_dirty = TRUE;
}

/*
 * What is typed, in the box: from a letter that keeps the caret in
 * sight, as many letters as fit; with the caret and what is being
 * converted while it is typed there.
 */
void ed_draw_field( INT gid, CONST T_DPRECT *box, BOOL show )
{
	INT	w = box->right - box->left - 8, end, next, y;
	UB	s[TEXT_MAX];

	if ( !on ) return;
	if ( !field ) shown_from = 0;
	if ( shown_from > caret ) shown_from = caret;
	while ( field && shown_from < caret && width_of(shown_from, caret, FIELD_PX) > w - 4 ) {
		shown_from = step(shown_from, 1);
	}
	end = shown_from;
	while ( end < len ) {
		next = step(end, 1);
		if ( width_of(shown_from, next, FIELD_PX) > w ) break;
		end = next;
	}
	memcpy(s, buf + shown_from, (size_t)( end - shown_from ));
	s[end - shown_from] = 0;
	y = box->top + ( box->bottom - box->top + FIELD_PX ) / 2 - 1;
	(void)dp_text(gid, box->left + 4, y, s, C_TEXT, FIELD_PX);
	if ( field && show ) {
		INT	cx = box->left + 4 + width_of(shown_from, caret, FIELD_PX);

		if ( busy() ) {
			ime_text(gid, cx, box->top + 3, FIELD_PX, box->bottom - box->top - 6);
		} else {
			T_DPRECT	c;

			c.left = cx;
			c.right = cx + 1;
			c.top = box->top + 3;
			c.bottom = box->bottom - 3;
			(void)dp_fill_rect(gid, &c, C_TEXT);
		}
	}
}
