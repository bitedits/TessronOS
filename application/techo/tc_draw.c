/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	tc_draw.c
 *	電子手帳: the three windows drawn (design 11.23)
 *
 *	予定表: the day along the top, its weekday and date in bold when it
 *	is a holiday, the 前へ and 次へ marks at the right, and the day's
 *	記入欄 under it, one ruled line each.
 *	カレンダー: the year and month with the era beside them and the two
 *	marks, the week's days, and six weeks of dates; the holidays in bold,
 *	this week in a frame and today in a thick one.
 *	住所録: 〒 and よみ along the top beside the marks, a line for each
 *	other field under them with its name, and at the foot the index of
 *	the person shown and where the person is in it, "2/24". Pressed, the
 *	index lists the eleven to choose from over it.
 *
 *	The 記入欄 being typed into is drawn by tc_edit.c over its place.
 */

#include "tc.h"

static CONST char *CONST wd[7] = { "日", "月", "火", "水", "木", "金", "土" };

/* ---------------------------------------------------------------- pieces */

static void fill( INT gid, INT l, INT t, INT r, INT b, UW c )
{
	T_DPRECT	rc;

	rc.left = l;
	rc.top = t;
	rc.right = r;
	rc.bottom = b;
	if ( r > l && b > t ) (void)dp_fill_rect(gid, &rc, c);
}

static void frame( INT gid, INT l, INT t, INT r, INT b, UW c, INT w )
{
	T_DPRECT	f;

	f.left = l;
	f.top = t;
	f.right = r;
	f.bottom = b;
	if ( r > l && b > t ) (void)dp_frame_rect(gid, &f, c, w);
}

/* Letters, in bold when asked: drawn twice a pixel apart */
static INT text( INT gid, INT x, INT y, CONST char *s, UW c, INT px, BOOL bold )
{
	INT	w = dp_text(gid, x, y, (CONST UB *)s, c, px);

	if ( bold ) {
		(void)dp_text(gid, x + 1, y, (CONST UB *)s, c, px);
		w++;
	}
	return w;
}

/* Letters cut at a letter's edge to fit width */
static void text_clip( INT gid, INT x, INT y, CONST UB *s, UW col, INT px, INT width )
{
	UB	b[TC_LINE_MAX];
	INT	n;

	if ( width <= 0 || s == NULL || s[0] == 0 ) return;
	strncpy((char *)b, (CONST char *)s, sizeof(b) - 1);
	b[sizeof(b) - 1] = 0;
	n = (INT)strlen((char *)b);
	while ( n > 0 && dp_text_width(b, px) > width ) {
		do {
			n--;
		} while ( n > 0 && ( b[n] & 0xC0 ) == 0x80 );
		b[n] = 0;
	}
	if ( n > 0 ) (void)dp_text(gid, x, y, b, col, px);
}

/* A mark of a triangle in a box: pointing left or right */
static void mark( INT gid, INT l, INT t, INT r, INT b, BOOL left, BOOL grey )
{
	INT	h = b - t - 6, i, cy = ( t + b ) / 2;
	UW	c = grey ? C_GREY : C_TEXT;

	frame(gid, l, t, r, b, C_RULE, 1);
	for ( i = 0; i <= h / 2; i++ ) {
		INT	x = left ? r - 5 - i : l + 4 + i;	/* the tallest column at the back, the point ahead */

		fill(gid, x, cy - ( h / 2 - i ), x + 1, cy + ( h / 2 - i ) + 1, c);
	}
}

/* ---------------------------------------------------------------- 予定表 */

#define SCHED_TOP	42

static void sched_draw( TCWIN *w )
{
	INT		gid = w->gid, y, m, d, i, x;
	char		s[64];
	BOOL		hol;
	CONST char	*name;

	tc_day_split(g_day, &y, &m, &d);
	hol = tc_holiday(&g_db, y, m, d);
	fill(gid, 0, 0, w->w, w->h, C_PAPER);
	/* the day, in a tab along the top */
	snprintf(s, sizeof(s), "%4d年 %2d月 %2d日（%s）", y, m, d, wd[ct_wday(y, m, d)]);
	frame(gid, 6, 4, w->w - 60, SCHED_TOP - 4, C_LINE, 1);
	x = 14 + text(gid, 14, 28, s, hol ? C_HOLIDAY : C_TEXT, 17, hol);
	if ( g_db.jp && ( name = tc_jp_holiday(y, m, d) ) != NULL ) {
		/* the national holiday's name, as room allows */
		text_clip(gid, x + 8, 27, (CONST UB *)name, C_HOLIDAY, 12, w->w - 66 - x - 8);
	}
	mark(gid, w->w - 52, 8, w->w - 32, 30, TRUE, (BOOL)( g_day <= TC_DAY_FIRST ));
	mark(gid, w->w - 28, 8, w->w - 8, 30, FALSE, (BOOL)( g_day >= TC_DAY_LAST ));
	fill(gid, 6, SCHED_TOP - 2, w->w - 6, SCHED_TOP - 1, C_LINE);
	for ( i = 0; i < TC_ROWS; i++ ) {
		T_DPRECT	r;

		(void)field_box(W_SCHED, i, &r);
		fill(gid, r.left, r.bottom - 1, r.right, r.bottom, C_RULE);
		if ( !( ed_win() == W_SCHED && ed_field() == i ) ) {
			text_clip(gid, r.left + 4, r.top + ( r.bottom - r.top + FIELD_PX ) / 2 - FIELD_PX / 8,
				  g_rows[i], C_TEXT, FIELD_PX, r.right - r.left - 8);
		}
	}
	frame(gid, 6, SCHED_TOP - 2, w->w - 6, w->h - 4, C_LINE, 1);
}

/* ---------------------------------------------------------------- カレンダー */

#define CAL_HEAD	32
#define CAL_WEEK	20
#define CAL_X0		6

static INT cal_cw( TCWIN *w )	{ return ( w->w - 2 * CAL_X0 ) / 7; }
static INT cal_rh( TCWIN *w )	{ return ( w->h - CAL_HEAD - CAL_WEEK - 6 ) / 6; }
static INT cal_top( void )	{ return CAL_HEAD + CAL_WEEK; }

static void cal_draw( TCWIN *w )
{
	INT	gid = w->gid, cw = cal_cw(w), rh = cal_rh(w), start = ct_wday(g_cy, g_cm, 1);
	INT	days = ct_days(g_cy, g_cm), d, x, ty, tm, td, today_row = -1;
	char	s[48];
	UB	era[24];

	tc_day_split(g_today, &ty, &tm, &td);
	fill(gid, 0, 0, w->w, w->h, C_PAPER);
	snprintf(s, sizeof(s), "%4d年 %2d月", g_cy, g_cm);
	x = 8 + text(gid, 8, 22, s, C_TEXT, 15, FALSE);
	(void)tc_era(g_cy, g_cm, 1, era, sizeof(era));
	(void)dp_text(gid, x + 8, 21, era, C_LABEL, 11);
	mark(gid, w->w - 52, 6, w->w - 32, 26, TRUE, (BOOL)( g_cy == TC_Y_FIRST && g_cm == 1 ));
	mark(gid, w->w - 28, 6, w->w - 8, 26, FALSE, (BOOL)( g_cy == TC_Y_LAST && g_cm == 12 ));
	fill(gid, CAL_X0, CAL_HEAD - 1, w->w - CAL_X0, CAL_HEAD, C_LINE);
	for ( d = 0; d < 7; d++ ) {
		INT	tw = dp_text_width((CONST UB *)wd[d], 12);

		(void)dp_text(gid, CAL_X0 + d * cw + ( cw - tw ) / 2, CAL_HEAD + 15, (CONST UB *)wd[d],
			      ( d == 0 ) ? C_HOLIDAY : ( d == 6 ) ? C_SATURDAY : C_TEXT, 12);
	}
	fill(gid, CAL_X0, cal_top() - 1, w->w - CAL_X0, cal_top(), C_RULE);
	for ( d = 1; d <= days; d++ ) {
		INT	cell = start + d - 1, row = cell / 7, col = cell % 7;
		INT	l = CAL_X0 + col * cw, t = cal_top() + row * rh;
		BOOL	hol = tc_holiday(&g_db, g_cy, g_cm, d);
		INT	tw;

		snprintf(s, sizeof(s), "%d", d);
		tw = dp_text_width((CONST UB *)s, 14);
		(void)text(gid, l + ( cw - tw ) / 2, t + ( rh + 14 ) / 2 - 2, s,
			   hol ? C_HOLIDAY : ( col == 6 ) ? C_SATURDAY : C_TEXT, 14, hol);
		if ( g_cy == ty && g_cm == tm && d == td ) {
			today_row = row;
			frame(gid, l + 1, t + 1, l + cw - 1, t + rh - 1, C_TEXT, 2);
		}
	}
	/* this week in a frame */
	if ( today_row >= 0 ) {
		INT	t = cal_top() + today_row * rh;

		frame(gid, CAL_X0 - 2, t - 1, CAL_X0 + 7 * cw + 2, t + rh + 1, C_LINE, 1);
	}
}

/* ---------------------------------------------------------------- 住所録 */

#define ADDR_TOP	40
#define ADDR_FOOT	272
#define IDX_W		26
#define IDX_H		22

static void idx_box( TCWIN *w, T_DPRECT *r )
{
	r->left = w->w - 120;
	r->top = w->h - 32;
	r->right = r->left + IDX_W;
	r->bottom = r->top + IDX_H + 4;
}

static void addr_draw( TCWIN *w )
{
	INT		gid = w->gid, k, pos = 0, count = 0;
	T_DPRECT	r, ib;
	UB		pt[24];
	char		s[48];

	fill(gid, 0, 0, w->w, w->h, C_PAPER);
	mark(gid, w->w - 52, 10, w->w - 32, 30, TRUE, (BOOL)( g_cur <= 0 ));
	mark(gid, w->w - 28, 10, w->w - 8, 30, FALSE, (BOOL)( g_cur >= g_db.na - 1 ));
	for ( k = 0; k < TC_NF; k++ ) {
		INT	f = tc_show_order[k];

		(void)field_box(W_ADDR, k, &r);
		(void)dp_text(gid, ( k == 1 ) ? r.left - 34 : 8, r.top + ( r.bottom - r.top + 14 ) / 2 - 2,
			      (CONST UB *)tc_field_name[f], C_LABEL, 14);
		fill(gid, r.left, r.bottom - 1, r.right, r.bottom, C_RULE);
		if ( !( ed_win() == W_ADDR && ed_field() == k ) ) {
			text_clip(gid, r.left + 4, r.top + ( r.bottom - r.top + FIELD_PX ) / 2 - FIELD_PX / 8,
				  tc_addr_get(&g_db, g_cur, f), C_TEXT, FIELD_PX, r.right - r.left - 8);
		}
	}
	/* the index of the person shown, and where the person is in it */
	idx_box(w, &ib);
	fill(gid, 6, ib.top - 6, w->w - 6, ib.top - 5, C_LINE);
	frame(gid, ib.left, ib.top, ib.right, ib.bottom, C_LINE, 1);
	if ( g_cur >= 0 && g_cur < g_db.na ) {
		CONST char	*nm = tc_index_name(tc_index_of(tc_addr_get(&g_db, g_cur, TC_F_YOMI)));

		(void)dp_text(gid, ib.left + ( IDX_W - dp_text_width((CONST UB *)nm, 15) ) / 2, ib.bottom - 7,
			      (CONST UB *)nm, C_TEXT, 15);
		tc_index_pos(&g_db, g_cur, &pos, &count);
	}
	tc_pos_text(pos, count, pt, sizeof(pt));
	snprintf(s, sizeof(s), "%s", (char *)pt);
	(void)dp_text(gid, ib.right + 10, ib.bottom - 7, (CONST UB *)s, C_TEXT, 14);
	/* the list of the indexes, over it */
	if ( g_idx_open ) {
		T_DPRECT	l;

		l.left = ib.left;
		l.right = ib.right;
		l.bottom = ib.top - 1;
		l.top = l.bottom - TC_IDX_N * IDX_H;
		fill(gid, l.left, l.top, l.right, l.bottom, C_PAPER);
		frame(gid, l.left, l.top, l.right, l.bottom, C_TEXT, 1);
		for ( k = 0; k < TC_IDX_N; k++ ) {
			CONST char	*nm = tc_index_name(k);
			BOOL		has = (BOOL)( tc_index_first(&g_db, k) >= 0 );

			(void)dp_text(gid, l.left + ( IDX_W - dp_text_width((CONST UB *)nm, 14) ) / 2,
				      l.top + k * IDX_H + 16, (CONST UB *)nm, has ? C_TEXT : C_GREY, 14);
			if ( k > 0 ) fill(gid, l.left + 2, l.top + k * IDX_H, l.right - 2, l.top + k * IDX_H + 1, C_RULE);
		}
	}
}

/* ---------------------------------------------------------------- where things are */

BOOL field_box( INT w, INT f, T_DPRECT *r )
{
	TCWIN	*t;

	if ( w < 0 || w >= W_N || f < 0 ) return FALSE;
	t = &g_w[w];
	if ( w == W_SCHED ) {
		if ( f >= TC_ROWS ) return FALSE;
		r->left = 10;
		r->right = t->w - 10;
		r->top = SCHED_TOP + f * ROW_H;
		r->bottom = r->top + ROW_H - 1;
		return TRUE;
	}
	if ( w == W_ADDR ) {
		if ( f >= TC_NF ) return FALSE;
		if ( f == 0 ) {
			r->left = 28;
			r->right = 150;
			r->top = 8;
			r->bottom = 34;
		} else if ( f == 1 ) {
			r->left = 192;
			r->right = t->w - 58;
			r->top = 8;
			r->bottom = 34;
		} else {
			r->left = 62;
			r->right = t->w - 10;
			r->top = ADDR_TOP + ( f - 2 ) * ( ROW_H + 2 );
			r->bottom = r->top + ROW_H;
		}
		return TRUE;
	}
	return FALSE;
}

static BOOL in( INT x, INT y, INT l, INT t, INT r, INT b )
{
	return (BOOL)( x >= l && x < r && y >= t && y < b );
}

INT index_at( INT x, INT y )
{
	T_DPRECT	ib;
	INT		top;

	if ( !g_idx_open ) return -1;
	idx_box(&g_w[W_ADDR], &ib);
	top = ib.top - 1 - TC_IDX_N * IDX_H;
	if ( !in(x, y, ib.left, top, ib.right, ib.top - 1) ) return -1;
	return ( y - top ) / IDX_H;
}

INT part_at( INT w, INT x, INT y )
{
	TCWIN		*t = &g_w[w];
	T_DPRECT	r;
	INT		f;

	for ( f = 0; field_box(w, f, &r); f++ ) {
		if ( in(x, y, r.left, r.top, r.right, r.bottom) ) return f;
	}
	if ( in(x, y, t->w - 54, 4, t->w - 30, 32) ) return PT_PREV;
	if ( in(x, y, t->w - 30, 4, t->w - 4, 32) ) return PT_NEXT;
	if ( w == W_CAL ) {
		INT	cw = cal_cw(t), rh = cal_rh(t), start = ct_wday(g_cy, g_cm, 1), cell, d;

		if ( in(x, y, 0, 0, t->w - 56, CAL_HEAD) ) return PT_MONTH;
		if ( y >= cal_top() && x >= CAL_X0 && x < CAL_X0 + 7 * cw && cw > 0 && rh > 0 ) {
			cell = ( ( y - cal_top() ) / rh ) * 7 + ( x - CAL_X0 ) / cw;
			d = cell - start + 1;
			if ( d >= 1 && d <= ct_days(g_cy, g_cm) ) return PT_DAY - d;
		}
	}
	if ( w == W_ADDR ) {
		idx_box(t, &r);
		if ( in(x, y, r.left, r.top, r.right, r.bottom) ) return PT_INDEX;
	}
	return PT_NONE;
}

void draw( INT w )
{
	TCWIN	*t = &g_w[w];

	if ( !t->open || t->gid < 0 ) return;
	switch ( w ) {
	case W_SCHED:	sched_draw(t);	break;
	case W_CAL:	cal_draw(t);	break;
	case W_ADDR:	addr_draw(t);	break;
	default:	break;
	}
	if ( g_drop_w == w && g_drop_f >= 0 ) {
		T_DPRECT	r;

		if ( field_box(w, g_drop_f, &r) ) {
			frame(t->gid, r.left - 1, r.top - 1, r.right + 1, r.bottom + 1, C_PICK, 2);
			fill(t->gid, g_drop_x, r.top + 3, g_drop_x + 2, r.bottom - 3, C_PICK);
		}
	}
	if ( ed_win() == w ) ed_draw();
	(void)wm_obj_flush(t->kw, NULL);
	t->dirty = FALSE;
}
