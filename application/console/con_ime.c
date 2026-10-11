/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	con_ime.c
 *	コンソール: かな漢字変換 on the line typed (design 16.5.19)
 *
 *	The keys go to a session of the system's converter (kc_hid_key) in
 *	the input mode the message line shows, as in the other programs that
 *	run as processes. What it commits goes into the line; what it is
 *	still working on -- the composition -- is drawn where the line ends,
 *	each clause underlined and the one being converted turned over, with
 *	the candidates listed under it (over it when the window has no room
 *	below) while they are chosen. A digit, or a press on a row, takes a
 *	candidate.
 *
 *	In 英語 a letter goes in as it is when the mode is 半角, and in its
 *	full-width form otherwise; the line is read with those forms taken
 *	as ASCII (con_fold), so a command typed either way is the same.
 */

#include "console.h"
#include <ts/wm.h>
#include <ts/hid.h>

#define LIST_MAX	4096
#define LIST_ROWS	9
#define IME_GROUND	0x00FFFFFFU
#define IME_INK		0x00000000U
#define IME_TARGET	0x000050A0U
#define IME_FRAME	0x00808080U
#define KEY_ENTER	0x28
#define ROW_H		( LT_LH + 2 )

LOCAL INT	kid;
LOCAL T_KCOUT	out;
LOCAL INT	base;			/* where the composition starts in out.text */
LOCAL BOOL	list;
LOCAL UB	cand[LIST_MAX];
LOCAL INT	ncand, first, total, chosen;
LOCAL T_DPRECT	listbox;		/* where the list was drawn */

LOCAL BOOL ready( void )
{
	if ( kid <= 0 ) {
		kid = kc_open();
		if ( kid <= 0 ) kid = 0;
	}
	return (BOOL)( kid > 0 );
}

EXPORT BOOL ime_busy( void )
{
	return (BOOL)( kid > 0 && out.cl[out.n_cl] > base );
}

/* What the converter gave: the committed part into the line, the rest kept, the list fetched */
LOCAL void take( INT r )
{
	base = 0;
	if ( out.n_out > 0 ) {
		INT	end = out.cl[out.n_out];

		if ( end > 0 ) con_insert((CONST UB *)out.text, end);
		base = end;
	}
	list = FALSE;
	if ( ( r & TSMOZC_LIST ) != 0 && ime_busy() ) {
		INT	sel = kc_list(kid, cand, LIST_MAX, &ncand, &first, &total);

		if ( sel > 0 && ncand > 0 ) {
			list = TRUE;
			chosen = sel;
		}
	}
}

/* A letter as it is (半角) or in its full-width form */
LOCAL BOOL plain( UB ch, BOOL han )
{
	UB	u[4];
	UW	cp;

	if ( ch < 0x20 || ch >= 0x7F ) return FALSE;
	if ( han ) {
		con_insert(&ch, 1);
		return TRUE;
	}
	cp = ( ch == ' ' ) ? 0x3000 : 0xFF01 + ( ch - 0x21 );
	u[0] = (UB)( 0xE0 | ( cp >> 12 ) );
	u[1] = (UB)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
	u[2] = (UB)( 0x80 | ( cp & 0x3F ) );
	con_insert(u, 3);
	return TRUE;
}

EXPORT BOOL ime_key( UINT key, UINT mods )
{
	UINT	mode = kc_mode();
	BOOL	han = (BOOL)( ( mode & WM_MODE_HAN ) != 0 );
	UB	ch = wm_key_char(key, mods);
	INT	r;

	if ( ( mods & ( HID_MOD_LCTRL | HID_MOD_RCTRL ) ) != 0 ) return FALSE;
	(void)ime_tick();
	if ( ( mode & WM_MODE_ALPH ) != 0 ) {
		if ( ( mode & WM_MODE_KANA ) != 0 && ( ( ch >= 'a' && ch <= 'z' ) || ( ch >= 'A' && ch <= 'Z' ) ) ) {
			ch ^= 0x20;		/* 英語 in capitals: the shift turns it back */
		}
		return ( ch > 0x20 && ch < 0x7F ) ? plain(ch, han) : FALSE;
	}
	if ( !ready() ) return FALSE;
	if ( list && ch >= '1' && ch <= '9' && ch - '1' < ncand ) {
		r = kc_choose(kid, first + ( ch - '1' ), &out);
		if ( r >= 0 && r != KC_NOTMINE ) take(r);
		return TRUE;
	}
	r = kc_hid_key(kid, key, mods, &out);
	if ( r >= 0 && r != KC_NOTMINE ) {
		take(r);
		return TRUE;
	}
	/* nothing being composed: a space as the mode types it, the rest the line's */
	if ( ch == ' ' && !ime_busy() ) return plain(' ', han);
	return ime_busy();
}

/*
 * Gone over to 英語 while something was being composed: the converter
 * answers no key in 英語, so what is shown is put into the line as it
 * stands and the session is begun again, as the desktop's own windows
 * commit it when 英数 is pressed
 */
EXPORT BOOL ime_tick( void )
{
	INT	end;

	if ( !ime_busy() || ( kc_mode() & WM_MODE_ALPH ) == 0 ) return FALSE;
	end = out.cl[out.n_cl];
	if ( end > base ) con_insert((CONST UB *)out.text + base, end - base);
	ime_end();
	return TRUE;
}

EXPORT void ime_commit( void )
{
	INT	r;

	if ( !ime_busy() ) {
		list = FALSE;
		return;
	}
	r = kc_hid_key(kid, KEY_ENTER, HID_MOD_LSHIFT, &out);
	if ( r >= 0 && r != KC_NOTMINE ) take(r);
	base = out.cl[out.n_cl];
	list = FALSE;
}

EXPORT BOOL ime_press( INT x, INT y )
{
	if ( list && x >= listbox.left && x < listbox.right && y >= listbox.top && y < listbox.bottom ) {
		INT	row = ( y - listbox.top - 2 ) / ROW_H, r;

		if ( row >= 0 && row < ncand ) {
			r = kc_choose(kid, first + row, &out);
			if ( r >= 0 && r != KC_NOTMINE ) take(r);
		}
		return TRUE;
	}
	ime_commit();
	return FALSE;
}

EXPORT void ime_end( void )
{
	if ( kid > 0 ) kc_close(kid);
	kid = 0;
	base = 0;
	list = FALSE;
	memset(&out, 0, sizeof(out));
}

/* Bytes a..b of the composition as a string */
LOCAL void piece( INT a, INT b, UB *buf, INT size )
{
	INT	n = 0;

	for ( ; a < b && n < size - 1; a++ ) buf[n++] = (UB)out.text[a];
	buf[n] = 0;
}

/* The work area's height */
LOCAL INT work_h( LTWIN *w )
{
	T_OBWPOS	wp;
	SZ		asz = 0;

	if ( ob_rea_rec(w->kw, OB_WR_PLACE, 0, &wp, sizeof(wp), &asz) >= E_OK && asz >= (SZ)sizeof(wp) ) {
		return wp.wbottom - wp.wtop;
	}
	return 360;
}

EXPORT void ime_draw( LTWIN *w, INT x, INT y )
{
	static UB	s[TSMOZC_TEXT_MAX], row[TSMOZC_TEXT_MAX + 8];
	T_DPRECT	r, u;
	INT		i, k, cx = x, end, wd, gid = w->gid;
	CONST UB	*c;

	if ( !ime_busy() || x < 0 || y < 0 ) return;
	end = out.cl[out.n_cl];
	piece(base, end, s, sizeof(s));
	r.left = x;
	r.top = y;
	r.right = x + dp_text_width(s, LT_PX) + 2;
	r.bottom = y + LT_LH;
	(void)dp_fill_rect(gid, &r, IME_GROUND);
	for ( i = out.n_out; i < out.n_cl; i++ ) {
		INT	a = out.cl[i], b = out.cl[i + 1];
		BOOL	target = (BOOL)( i == out.clause && !out.yomi );

		if ( b <= base ) continue;
		if ( a < base ) a = base;
		piece(a, b, s, sizeof(s));
		wd = dp_text_width(s, LT_PX);
		if ( target ) {
			u.left = cx;
			u.top = y;
			u.right = cx + wd;
			u.bottom = y + LT_LH;
			(void)dp_fill_rect(gid, &u, IME_TARGET);
		}
		(void)dp_text(gid, cx, y + LT_PX, s, target ? IME_GROUND : IME_INK, LT_PX);
		u.left = cx + 1;
		u.right = cx + wd - 1;
		u.top = y + LT_LH - 2;
		u.bottom = y + LT_LH - 1;
		(void)dp_fill_rect(gid, &u, IME_INK);
		cx += wd;
	}
	if ( out.caret >= base && out.caret <= end ) {
		piece(base, out.caret, s, sizeof(s));
		r.left = x + dp_text_width(s, LT_PX);
		r.right = r.left + 2;
		r.top = y;
		r.bottom = y + LT_LH;
		(void)dp_fill_rect(gid, &r, IME_INK);
	}
	if ( !list ) return;

	/* the candidates, a number before each */
	wd = 0;
	c = cand;
	for ( k = 0; k < ncand && k < LIST_ROWS; k++ ) {
		INT	n = lt_num(row, k + 1), cw;

		row[n++] = ' ';
		for ( i = 0; c[i] != 0 && n < (INT)sizeof(row) - 1; i++ ) row[n++] = c[i];
		row[n] = 0;
		cw = dp_text_width(row, LT_PX);
		if ( cw > wd ) wd = cw;
		c += strlen((CONST char *)c) + 1;
	}
	listbox.left = x;
	listbox.right = x + wd + 8;
	listbox.top = y + LT_LH + 2;
	listbox.bottom = listbox.top + ROW_H * k + 4;
	if ( listbox.bottom > work_h(w) && y - ( listbox.bottom - listbox.top ) - 2 >= 0 ) {
		listbox.bottom = y - 2;
		listbox.top = listbox.bottom - ( ROW_H * k + 4 );
	}
	(void)dp_fill_rect(gid, &listbox, IME_GROUND);
	(void)dp_frame_rect(gid, &listbox, IME_FRAME, 1);
	c = cand;
	for ( i = 0; i < k; i++ ) {
		BOOL	sel = (BOOL)( first + i == chosen );
		INT	n = lt_num(row, i + 1), j;

		row[n++] = ' ';
		for ( j = 0; c[j] != 0 && n < (INT)sizeof(row) - 1; j++ ) row[n++] = c[j];
		row[n] = 0;
		if ( sel ) {
			r.left = listbox.left + 1;
			r.right = listbox.right - 1;
			r.top = listbox.top + 2 + i * ROW_H;
			r.bottom = r.top + ROW_H;
			(void)dp_fill_rect(gid, &r, IME_TARGET);
		}
		(void)dp_text(gid, listbox.left + 4, listbox.top + 2 + i * ROW_H + LT_PX, row,
			      sel ? IME_GROUND : IME_INK, LT_PX);
		c += strlen((CONST char *)c) + 1;
	}
}
