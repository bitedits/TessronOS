/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	lt_vt.c
 *	A screen of a terminal in a window of a program's own: what
 *	コンソール shows of a POSIX program on a pseudo terminal, tsh and
 *	the commands (design 5.8.4, 16.5.19)
 *
 *	The bytes the program writes are taken as a terminal of the kind
 *	tessron (tools/netbsd/port/terminfo.src) takes them: the controls of
 *	a VT100 and the sequences of the ANSI standard -- the cursor
 *	addressed and moved, lines and letters put in and taken out, a
 *	scrolling region, eight colours with bold, underline and reverse, a
 *	second screen for the programs that take the whole of it, the keys
 *	of the cursor in either of their forms. UTF-8 throughout: a letter
 *	of the wide East Asian kind takes two cells. The lines that go off
 *	the top of the first screen go on into the window's record of lines
 *	(LTERM), where they can be wound back to. Answers the terminal gives
 *	(where the cursor is, what it is) are kept for the caller to send.
 */

#include "lterm.h"

#define INK		0x00000000U
#define GROUND		0x00FFFFFFU

/* The eight colours, then the eight bright ones */
LOCAL CONST UW vt_colour[16] = {
	0x00000000U, 0x00B00000U, 0x00008000U, 0x00806000U,
	0x000030C0U, 0x00900090U, 0x00008080U, 0x00C0C0C0U,
	0x00606060U, 0x00FF3030U, 0x0030C030U, 0x00C0A000U,
	0x004070FFU, 0x00E040E0U, 0x0020C0C0U, 0x00FFFFFFU,
};

#define A_BOLD		0x01
#define A_UNDER		0x02
#define A_REV		0x04
#define A_DIM		0x08
#define A_ITALIC	0x10
#define A_INVIS		0x20

#define C_DEFAULT	0xFF		/* a colour not set: the window's ink or ground */

/* ---------------------------------------------------------------- letters */

/* Whether a code point takes two cells: the wide and full-width ones of East Asia */
LOCAL BOOL wide( UW c )
{
	return (BOOL)( ( c >= 0x1100 && c <= 0x115F ) || ( c >= 0x2E80 && c <= 0x303E )
		    || ( c >= 0x3041 && c <= 0x33FF ) || ( c >= 0x3400 && c <= 0x4DBF )
		    || ( c >= 0x4E00 && c <= 0x9FFF ) || ( c >= 0xA000 && c <= 0xA4CF )
		    || ( c >= 0xAC00 && c <= 0xD7A3 ) || ( c >= 0xF900 && c <= 0xFAFF )
		    || ( c >= 0xFE30 && c <= 0xFE4F ) || ( c >= 0xFF00 && c <= 0xFF60 )
		    || ( c >= 0xFFE0 && c <= 0xFFE6 ) || ( c >= 0x1F300 && c <= 0x1F64F )
		    || ( c >= 0x1F900 && c <= 0x1F9FF ) || ( c >= 0x20000 && c <= 0x3FFFD ) );
}

/* Whether it takes no cell of its own: the combining marks, the joiners, the variation selectors */
LOCAL BOOL zero_width( UW c )
{
	return (BOOL)( ( c >= 0x0300 && c <= 0x036F ) || ( c >= 0x200B && c <= 0x200F )
		    || ( c >= 0x3099 && c <= 0x309A ) || ( c >= 0xFE00 && c <= 0xFE0F )
		    || c == 0xFEFF );
}

/* ---------------------------------------------------------------- the screen */

LOCAL VTCELL *cell( LTVT *v, INT row, INT col )
{
	return &v->scr[v->alt][row][col];
}

LOCAL void blank( LTVT *v, VTCELL *c )
{
	c->ch[0] = ' ';
	c->n = 1;
	c->w = 1;
	c->attr = 0;
	c->fg = C_DEFAULT;
	c->bg = v->bg;		/* a line cleared takes the colour of the ground set (bce) */
}

LOCAL void dirty( LTVT *v, INT row )
{
	if ( row >= 0 && row < v->rows ) v->dirty[row] = TRUE;
}

LOCAL void dirty_all( LTVT *v )
{
	INT	r;

	for ( r = 0; r < v->rows; r++ ) v->dirty[r] = TRUE;
}

LOCAL void clear_cells( LTVT *v, INT row, INT c0, INT c1 )
{
	INT	c;

	if ( row < 0 || row >= v->rows ) return;
	if ( c0 < 0 ) c0 = 0;
	if ( c1 > v->cols ) c1 = v->cols;
	for ( c = c0; c < c1; c++ ) blank(v, cell(v, row, c));
	dirty(v, row);
}

/* A line of the first screen going off its top: into the record of lines */
LOCAL void to_log( LTVT *v, INT row )
{
	UB	buf[VT_COLS_MAX * 4 + 2];
	INT	c, n = 0, k, end = 0;
	VTCELL	*x;

	if ( v->log == NULL || v->alt ) return;
	for ( c = 0; c < v->cols; c++ ) {
		x = cell(v, row, c);
		if ( x->w == 0 ) continue;
		for ( k = 0; k < x->n; k++ ) buf[n++] = x->ch[k];
		if ( !( x->n == 1 && x->ch[0] == ' ' ) ) end = n;
	}
	buf[end++] = '\n';
	lt_put(v->log, buf, end);
}

LOCAL void scroll_up( LTVT *v, INT top, INT bot, INT k )
{
	INT	r, i;

	for ( i = 0; i < k; i++ ) {
		if ( top == 0 ) to_log(v, 0);
		for ( r = top; r < bot; r++ ) {
			memcpy(v->scr[v->alt][r], v->scr[v->alt][r + 1], sizeof(VTCELL) * VT_COLS_MAX);
		}
		clear_cells(v, bot, 0, v->cols);
	}
	for ( r = top; r <= bot; r++ ) dirty(v, r);
}

LOCAL void scroll_down( LTVT *v, INT top, INT bot, INT k )
{
	INT	r, i;

	for ( i = 0; i < k; i++ ) {
		for ( r = bot; r > top; r-- ) {
			memcpy(v->scr[v->alt][r], v->scr[v->alt][r - 1], sizeof(VTCELL) * VT_COLS_MAX);
		}
		clear_cells(v, top, 0, v->cols);
	}
	for ( r = top; r <= bot; r++ ) dirty(v, r);
}

LOCAL void line_feed( LTVT *v )
{
	if ( v->cy == v->bot ) {
		scroll_up(v, v->top, v->bot, 1);
	} else if ( v->cy < v->rows - 1 ) {
		dirty(v, v->cy);
		v->cy++;
		dirty(v, v->cy);
	}
}

LOCAL void move_to( LTVT *v, INT row, INT col )
{
	dirty(v, v->cy);
	if ( row < 0 ) row = 0;
	if ( row >= v->rows ) row = v->rows - 1;
	if ( col < 0 ) col = 0;
	if ( col >= v->cols ) col = v->cols - 1;
	v->cy = row;
	v->cx = col;
	v->wrapnext = FALSE;
	dirty(v, v->cy);
}

LOCAL void reset_tabs( LTVT *v )
{
	INT	c;

	for ( c = 0; c < VT_COLS_MAX; c++ ) v->tabs[c] = (UB)( ( c % 8 ) == 0 && c > 0 );
}

EXPORT void lt_vt_init( LTVT *v, LTERM *log, INT rows, INT cols )
{
	INT	s, r;

	memset(v, 0, sizeof(*v));
	v->log = log;
	v->rows = ( rows < 2 ) ? 2 : ( rows > VT_ROWS_MAX ) ? VT_ROWS_MAX : rows;
	v->cols = ( cols < 10 ) ? 10 : ( cols > VT_COLS_MAX ) ? VT_COLS_MAX : cols;
	v->fg = v->bg = C_DEFAULT;
	v->top = 0;
	v->bot = v->rows - 1;
	v->show_cursor = TRUE;
	v->autowrap = TRUE;
	reset_tabs(v);
	for ( s = 0; s < 2; s++ ) {
		v->alt = s;
		for ( r = 0; r < VT_ROWS_MAX; r++ ) clear_cells(v, r, 0, VT_COLS_MAX);
	}
	v->alt = 0;
	dirty_all(v);
}

EXPORT void lt_vt_resize( LTVT *v, INT rows, INT cols )
{
	INT	s, r, c;

	if ( rows < 2 ) rows = 2;
	if ( rows > VT_ROWS_MAX ) rows = VT_ROWS_MAX;
	if ( cols < 10 ) cols = 10;
	if ( cols > VT_COLS_MAX ) cols = VT_COLS_MAX;
	if ( rows == v->rows && cols == v->cols ) return;
	/* the rows that no longer fit above the cursor go off the top as text scrolled would */
	while ( v->cy >= rows ) {
		scroll_up(v, 0, v->rows - 1, 1);
		v->cy--;
	}
	for ( s = 0; s < 2; s++ ) {
		for ( r = 0; r < VT_ROWS_MAX; r++ ) {
			for ( c = ( r < v->rows ) ? v->cols : 0; c < VT_COLS_MAX; c++ ) {
				blank(v, &v->scr[s][r][c]);
			}
		}
	}
	v->rows = rows;
	v->cols = cols;
	v->top = 0;
	v->bot = rows - 1;
	if ( v->cx >= cols ) v->cx = cols - 1;
	v->wrapnext = FALSE;
	dirty_all(v);
}

EXPORT void lt_vt_flush( LTVT *v )
{
	INT	r, last = -1, c;

	v->alt = 0;
	for ( r = 0; r < v->rows; r++ ) {
		for ( c = 0; c < v->cols; c++ ) {
			VTCELL	*x = cell(v, r, c);

			if ( x->w != 0 && !( x->n == 1 && x->ch[0] == ' ' ) ) {
				last = r;
				break;
			}
		}
	}
	if ( v->cy > last ) last = v->cy - 1;	/* the cursor's own line is the prompt's to come */
	for ( r = 0; r <= last; r++ ) to_log(v, r);
}

/* ---------------------------------------------------------------- letters put */

LOCAL void put_letter( LTVT *v, CONST UB *u, INT n, UW cp )
{
	INT	w = wide(cp) ? 2 : 1, k;
	VTCELL	*c;

	if ( zero_width(cp) ) {
		/* onto the letter before, when there is room in its cell */
		INT	col = ( v->wrapnext || v->cx == 0 ) ? v->cx : v->cx - 1;

		c = cell(v, v->cy, col);
		if ( c->w == 0 && col > 0 ) c = cell(v, v->cy, col - 1);
		if ( c->n + n <= (INT)sizeof(c->ch) ) {
			for ( k = 0; k < n; k++ ) c->ch[c->n++] = u[k];
		}
		dirty(v, v->cy);
		return;
	}
	if ( v->wrapnext && v->autowrap ) {
		v->cx = 0;
		v->wrapnext = FALSE;
		line_feed(v);
	}
	if ( v->cx + w > v->cols ) {
		if ( !v->autowrap ) {
			v->cx = v->cols - w;
		} else {
			clear_cells(v, v->cy, v->cx, v->cols);
			v->cx = 0;
			line_feed(v);
		}
	}
	if ( v->insert ) {
		INT	col;

		for ( col = v->cols - 1; col >= v->cx + w; col-- ) {
			*cell(v, v->cy, col) = *cell(v, v->cy, col - w);
		}
	}
	/* half of a wide letter written over takes the other half with it */
	c = cell(v, v->cy, v->cx);
	if ( c->w == 0 && v->cx > 0 ) blank(v, cell(v, v->cy, v->cx - 1));
	if ( c->w == 2 && v->cx + 1 < v->cols ) blank(v, cell(v, v->cy, v->cx + 1));

	for ( k = 0; k < n && k < (INT)sizeof(c->ch); k++ ) c->ch[k] = u[k];
	c->n = (UB)k;
	c->w = (UB)w;
	c->attr = v->attr;
	c->fg = v->fg;
	c->bg = v->bg;
	if ( w == 2 ) {
		VTCELL	*d = cell(v, v->cy, v->cx + 1);

		d->n = 0;
		d->w = 0;
		d->attr = v->attr;
		d->fg = v->fg;
		d->bg = v->bg;
	}
	dirty(v, v->cy);
	if ( v->cx + w >= v->cols ) {
		v->cx = v->cols - 1;
		v->wrapnext = TRUE;
	} else {
		v->cx += w;
	}
}

/* ---------------------------------------------------------------- answers */

LOCAL void reply( LTVT *v, CONST char *s )
{
	while ( *s != 0 && v->nreply < (INT)sizeof(v->reply) ) v->reply[v->nreply++] = (UB)*s++;
}

LOCAL void reply_num( LTVT *v, INT n )
{
	UB	b[16];

	(void)lt_num(b, n);
	reply(v, (CONST char *)b);
}

EXPORT INT lt_vt_reply( LTVT *v, UB *out, INT max )
{
	INT	n = ( v->nreply < max ) ? v->nreply : max, i;

	memcpy(out, v->reply, (SZ)n);
	for ( i = n; i < v->nreply; i++ ) v->reply[i - n] = v->reply[i];
	v->nreply -= n;
	return n;
}

/* ---------------------------------------------------------------- the sequences */

LOCAL INT par( LTVT *v, INT i, INT dflt )
{
	return ( i < v->npar && v->par[i] > 0 ) ? v->par[i] : dflt;
}

LOCAL void sgr( LTVT *v )
{
	INT	i, p;

	if ( v->npar == 0 ) {
		v->attr = 0;
		v->fg = v->bg = C_DEFAULT;
		return;
	}
	for ( i = 0; i < v->npar; i++ ) {
		p = v->par[i];
		if ( p == 0 ) {
			v->attr = 0;
			v->fg = v->bg = C_DEFAULT;
		} else if ( p == 1 ) v->attr |= A_BOLD;
		else if ( p == 2 ) v->attr |= A_DIM;
		else if ( p == 3 ) v->attr |= A_ITALIC;
		else if ( p == 4 ) v->attr |= A_UNDER;
		else if ( p == 5 ) ;				/* blinking: shown steady */
		else if ( p == 7 ) v->attr |= A_REV;
		else if ( p == 8 ) v->attr |= A_INVIS;
		else if ( p == 22 ) v->attr &= (UB)~( A_BOLD | A_DIM );
		else if ( p == 23 ) v->attr &= (UB)~A_ITALIC;
		else if ( p == 24 ) v->attr &= (UB)~A_UNDER;
		else if ( p == 27 ) v->attr &= (UB)~A_REV;
		else if ( p == 28 ) v->attr &= (UB)~A_INVIS;
		else if ( p >= 30 && p <= 37 ) v->fg = (UB)( p - 30 );
		else if ( p == 39 ) v->fg = C_DEFAULT;
		else if ( p >= 40 && p <= 47 ) v->bg = (UB)( p - 40 );
		else if ( p == 49 ) v->bg = C_DEFAULT;
		else if ( p >= 90 && p <= 97 ) v->fg = (UB)( p - 90 + 8 );
		else if ( p >= 100 && p <= 107 ) v->bg = (UB)( p - 100 + 8 );
		else if ( ( p == 38 || p == 48 ) && i + 1 < v->npar ) {
			/* 256 colours and true colour: the nearest of the sixteen */
			INT	c = -1;

			if ( v->par[i + 1] == 5 && i + 2 < v->npar ) {
				c = v->par[i + 2];
				if ( c >= 16 ) c = ( c >= 232 ) ? ( ( c >= 244 ) ? 7 : 8 ) : 7;
				i += 2;
			} else if ( v->par[i + 1] == 2 && i + 4 < v->npar ) {
				INT	r = v->par[i + 2], g = v->par[i + 3], b = v->par[i + 4];

				c = ( ( r > 127 ) ? 1 : 0 ) | ( ( g > 127 ) ? 2 : 0 ) | ( ( b > 127 ) ? 4 : 0 );
				if ( r > 200 || g > 200 || b > 200 ) c += 8;
				i += 4;
			}
			if ( c >= 0 && c < 16 ) {
				if ( p == 38 ) v->fg = (UB)c;
				else v->bg = (UB)c;
			}
		}
	}
}

LOCAL void save_cursor( LTVT *v )
{
	v->scx = v->cx;
	v->scy = v->cy;
	v->sattr = v->attr;
	v->sfg = v->fg;
	v->sbg = v->bg;
}

LOCAL void restore_cursor( LTVT *v )
{
	move_to(v, v->scy, v->scx);
	v->attr = v->sattr;
	v->fg = v->sfg;
	v->bg = v->sbg;
}

LOCAL void set_mode( LTVT *v, BOOL on )
{
	INT	i, p;

	for ( i = 0; i < v->npar; i++ ) {
		p = v->par[i];
		if ( !v->priv ) {
			if ( p == 4 ) v->insert = on;
			continue;
		}
		switch ( p ) {
		  case 1:	v->appkeys = on; break;
		  case 7:	v->autowrap = on; break;
		  case 25:	v->show_cursor = on; dirty(v, v->cy); break;
		  case 47:
		  case 1047:
		  case 1049:
			if ( on && !v->alt ) {
				if ( p == 1049 ) save_cursor(v);
				v->alt = 1;
				for ( p = 0; p < v->rows; p++ ) clear_cells(v, p, 0, v->cols);
			} else if ( !on && v->alt ) {
				v->alt = 0;
				if ( v->par[i] == 1049 ) restore_cursor(v);
			}
			dirty_all(v);
			break;
		  default:
			break;
		}
	}
}

LOCAL void csi( LTVT *v, UB f )
{
	INT	n, r, c;

	switch ( f ) {
	  case 'A':	move_to(v, v->cy - par(v, 0, 1), v->cx); break;
	  case 'B':	move_to(v, v->cy + par(v, 0, 1), v->cx); break;
	  case 'C':	move_to(v, v->cy, v->cx + par(v, 0, 1)); break;
	  case 'D':	move_to(v, v->cy, v->cx - par(v, 0, 1)); break;
	  case 'E':	move_to(v, v->cy + par(v, 0, 1), 0); break;
	  case 'F':	move_to(v, v->cy - par(v, 0, 1), 0); break;
	  case 'G':
	  case '`':	move_to(v, v->cy, par(v, 0, 1) - 1); break;
	  case 'd':	move_to(v, par(v, 0, 1) - 1, v->cx); break;
	  case 'H':
	  case 'f':	move_to(v, par(v, 0, 1) - 1, par(v, 1, 1) - 1); break;
	  case 'J':
		n = ( v->npar > 0 ) ? v->par[0] : 0;
		if ( n == 0 ) {
			clear_cells(v, v->cy, v->cx, v->cols);
			for ( r = v->cy + 1; r < v->rows; r++ ) clear_cells(v, r, 0, v->cols);
		} else if ( n == 1 ) {
			for ( r = 0; r < v->cy; r++ ) clear_cells(v, r, 0, v->cols);
			clear_cells(v, v->cy, 0, v->cx + 1);
		} else {
			for ( r = 0; r < v->rows; r++ ) clear_cells(v, r, 0, v->cols);
		}
		break;
	  case 'K':
		n = ( v->npar > 0 ) ? v->par[0] : 0;
		if ( n == 0 ) clear_cells(v, v->cy, v->cx, v->cols);
		else if ( n == 1 ) clear_cells(v, v->cy, 0, v->cx + 1);
		else clear_cells(v, v->cy, 0, v->cols);
		break;
	  case 'L':
		if ( v->cy >= v->top && v->cy <= v->bot ) scroll_down(v, v->cy, v->bot, par(v, 0, 1));
		break;
	  case 'M':
		if ( v->cy >= v->top && v->cy <= v->bot ) {
			n = par(v, 0, 1);
			for ( r = 0; r < n; r++ ) {
				for ( c = v->cy; c < v->bot; c++ ) {
					memcpy(v->scr[v->alt][c], v->scr[v->alt][c + 1], sizeof(VTCELL) * VT_COLS_MAX);
				}
				clear_cells(v, v->bot, 0, v->cols);
			}
			for ( r = v->cy; r <= v->bot; r++ ) dirty(v, r);
		}
		break;
	  case 'P':
		n = par(v, 0, 1);
		for ( c = v->cx; c < v->cols; c++ ) {
			if ( c + n < v->cols ) *cell(v, v->cy, c) = *cell(v, v->cy, c + n);
			else blank(v, cell(v, v->cy, c));
		}
		dirty(v, v->cy);
		break;
	  case '@':
		n = par(v, 0, 1);
		for ( c = v->cols - 1; c >= v->cx; c-- ) {
			if ( c - n >= v->cx ) *cell(v, v->cy, c) = *cell(v, v->cy, c - n);
			else blank(v, cell(v, v->cy, c));
		}
		dirty(v, v->cy);
		break;
	  case 'X':
		clear_cells(v, v->cy, v->cx, v->cx + par(v, 0, 1));
		break;
	  case 'S':	scroll_up(v, v->top, v->bot, par(v, 0, 1)); break;
	  case 'T':	scroll_down(v, v->top, v->bot, par(v, 0, 1)); break;
	  case 'm':	sgr(v); break;
	  case 'r':
		r = par(v, 0, 1) - 1;
		c = par(v, 1, v->rows) - 1;
		if ( r < c && c < v->rows ) {
			v->top = r;
			v->bot = c;
			move_to(v, 0, 0);
		}
		break;
	  case 's':	save_cursor(v); break;
	  case 'u':	restore_cursor(v); break;
	  case 'h':	set_mode(v, TRUE); break;
	  case 'l':	set_mode(v, FALSE); break;
	  case 'g':
		n = ( v->npar > 0 ) ? v->par[0] : 0;
		if ( n == 0 ) v->tabs[v->cx] = 0;
		else if ( n == 3 ) memset(v->tabs, 0, sizeof(v->tabs));
		break;
	  case 'n':
		if ( v->npar > 0 && v->par[0] == 6 ) {
			reply(v, "\033[");
			reply_num(v, v->cy + 1);
			reply(v, ";");
			reply_num(v, v->cx + 1);
			reply(v, "R");
		} else if ( v->npar > 0 && v->par[0] == 5 ) {
			reply(v, "\033[0n");
		}
		break;
	  case 'c':
		if ( !v->priv ) reply(v, "\033[?1;2c");	/* a VT100 with the advanced video option */
		break;
	  default:
		break;				/* the others are passed over */
	}
}

LOCAL void esc( LTVT *v, UB c )
{
	switch ( c ) {
	  case '[':
		v->state = 2;
		v->npar = 0;
		v->priv = FALSE;
		memset(v->par, 0, sizeof(v->par));
		return;
	  case ']':
		v->state = 4;			/* a title and the like: up to BEL or ST, passed over */
		return;
	  case '(':
	  case ')':
	  case '*':
	  case '+':
	  case '#':
		v->state = 3;			/* the sets of letters: one more byte, passed over */
		return;
	  case '7':	save_cursor(v); break;
	  case '8':	restore_cursor(v); break;
	  case 'D':	line_feed(v); break;
	  case 'E':	v->cx = 0; line_feed(v); break;
	  case 'M':
		if ( v->cy == v->top ) scroll_down(v, v->top, v->bot, 1);
		else move_to(v, v->cy - 1, v->cx);
		break;
	  case 'H':	v->tabs[v->cx] = 1; break;
	  case '=':	v->appkeys = TRUE; break;
	  case '>':	v->appkeys = FALSE; break;
	  case 'c':
		lt_vt_init(v, v->log, v->rows, v->cols);
		break;
	  default:
		break;
	}
	v->state = 0;
}

EXPORT void lt_vt_put( LTVT *v, CONST UB *s, INT n )
{
	INT	i;
	UB	c;

	for ( i = 0; i < n; i++ ) {
		c = s[i];

		/* a letter of several bytes */
		if ( v->uneed > 0 ) {
			if ( ( c & 0xC0 ) == 0x80 ) {
				v->utf[v->un++] = c;
				v->ucp = ( v->ucp << 6 ) | ( c & 0x3F );
				if ( --v->uneed == 0 ) put_letter(v, v->utf, v->un, v->ucp);
				continue;
			}
			v->uneed = 0;		/* broken: what came is taken as itself */
		}
		if ( v->state == 4 ) {
			if ( c == 0x07 ) v->state = 0;
			else if ( c == 0x1B ) v->state = 5;
			continue;
		}
		if ( v->state == 5 ) {
			v->state = ( c == '\\' ) ? 0 : 4;
			continue;
		}
		if ( v->state == 3 ) {
			v->state = 0;
			continue;
		}
		if ( c < 0x20 || c == 0x7F ) {
			switch ( c ) {
			  case 0x07:	break;
			  case 0x08:
				if ( v->cx > 0 ) move_to(v, v->cy, v->cx - ( v->wrapnext ? 0 : 1 ));
				v->wrapnext = FALSE;
				break;
			  case 0x09:
				do {
					v->cx++;
				} while ( v->cx < v->cols - 1 && !v->tabs[v->cx] );
				if ( v->cx >= v->cols ) v->cx = v->cols - 1;
				break;
			  case 0x0A:
			  case 0x0B:
			  case 0x0C:
				line_feed(v);
				v->wrapnext = FALSE;
				break;
			  case 0x0D:
				dirty(v, v->cy);
				v->cx = 0;
				v->wrapnext = FALSE;
				break;
			  case 0x1B:
				v->state = 1;
				break;
			  case 0x18:
			  case 0x1A:
				v->state = 0;
				break;
			  default:
				break;
			}
			continue;
		}
		if ( v->state == 1 ) {
			esc(v, c);
			continue;
		}
		if ( v->state == 2 ) {
			if ( c >= '0' && c <= '9' ) {
				if ( v->npar == 0 ) v->npar = 1;
				if ( v->par[v->npar - 1] < 10000 ) {
					v->par[v->npar - 1] = v->par[v->npar - 1] * 10 + ( c - '0' );
				}
			} else if ( c == ';' || c == ':' ) {
				if ( v->npar == 0 ) v->npar = 1;
				if ( v->npar < VT_PAR_MAX ) v->npar++;
			} else if ( c == '?' || c == '>' || c == '=' ) {
				v->priv = TRUE;
			} else if ( c >= 0x40 && c <= 0x7E ) {
				v->state = 0;
				csi(v, c);
			}
			continue;
		}
		if ( c < 0x80 ) {
			put_letter(v, &c, 1, c);
		} else if ( ( c & 0xE0 ) == 0xC0 ) {
			v->utf[0] = c; v->un = 1; v->uneed = 1; v->ucp = c & 0x1F;
		} else if ( ( c & 0xF0 ) == 0xE0 ) {
			v->utf[0] = c; v->un = 1; v->uneed = 2; v->ucp = c & 0x0F;
		} else if ( ( c & 0xF8 ) == 0xF0 ) {
			v->utf[0] = c; v->un = 1; v->uneed = 3; v->ucp = c & 0x07;
		}
	}
}

/* ---------------------------------------------------------------- the keys */

EXPORT INT lt_vt_key( LTVT *v, INT key, UB *out )
{
	CONST char *s = NULL;
	INT	n;

	switch ( key ) {
	  case VTK_UP:		s = v->appkeys ? "\033OA" : "\033[A"; break;
	  case VTK_DOWN:	s = v->appkeys ? "\033OB" : "\033[B"; break;
	  case VTK_RIGHT:	s = v->appkeys ? "\033OC" : "\033[C"; break;
	  case VTK_LEFT:	s = v->appkeys ? "\033OD" : "\033[D"; break;
	  case VTK_HOME:	s = v->appkeys ? "\033OH" : "\033[H"; break;
	  case VTK_END:		s = v->appkeys ? "\033OF" : "\033[F"; break;
	  case VTK_INS:		s = "\033[2~"; break;
	  case VTK_DEL:		s = "\033[3~"; break;
	  case VTK_PGUP:	s = "\033[5~"; break;
	  case VTK_PGDN:	s = "\033[6~"; break;
	  case VTK_BTAB:	s = "\033[Z"; break;
	  default:		return 0;
	}
	for ( n = 0; s[n] != 0; n++ ) out[n] = (UB)s[n];
	return n;
}

/* ---------------------------------------------------------------- drawing */

/* The size of a cell: a half-width letter's, measured once */
LOCAL INT cell_w( LTVT *v )
{
	if ( v->cw <= 0 ) {
		v->cw = dp_text_width((CONST UB *)"0", LT_PX);
		if ( v->cw < 4 || v->cw > LT_PX ) v->cw = LT_PX / 2 + 1;
	}
	return v->cw;
}

EXPORT void lt_vt_fit( LTVT *v, LTWIN *w, INT *p_rows, INT *p_cols )
{
	T_OBWPOS	wp;
	SZ		asz = 0;
	INT		ww = 600, wh = 360;

	if ( ob_rea_rec(w->kw, OB_WR_PLACE, 0, &wp, sizeof(wp), &asz) >= E_OK
	  && asz >= (SZ)sizeof(wp) ) {
		ww = wp.wright - wp.wleft;
		wh = wp.wbottom - wp.wtop;
	}
	*p_cols = ( ww - 2 * LT_PAD ) / cell_w(v);
	*p_rows = ( wh - 2 * LT_PAD ) / LT_LH;
	if ( *p_cols < 10 ) *p_cols = 10;
	if ( *p_rows < 2 ) *p_rows = 2;
	if ( *p_cols > VT_COLS_MAX ) *p_cols = VT_COLS_MAX;
	if ( *p_rows > VT_ROWS_MAX ) *p_rows = VT_ROWS_MAX;
}

LOCAL void draw_row( LTVT *v, LTWIN *w, INT row, BOOL caret )
{
	T_DPRECT	r;
	VTCELL		*c;
	UB		buf[8];
	INT		col, k, x, y = LT_PAD + row * LT_LH, cw = cell_w(v);
	UW		ink, ground;
	BOOL		cur;

	r.left = 0;
	r.right = LT_PAD + v->cols * cw + LT_PAD;
	r.top = y;
	r.bottom = y + LT_LH;
	(void)dp_fill_rect(w->gid, &r, GROUND);
	for ( col = 0; col < v->cols; col++ ) {
		c = cell(v, row, col);
		if ( c->w == 0 ) continue;
		x = LT_PAD + col * cw;
		ink = ( c->fg == C_DEFAULT ) ? INK : vt_colour[c->fg & 15];
		ground = ( c->bg == C_DEFAULT ) ? GROUND : vt_colour[c->bg & 15];
		if ( ( c->attr & A_BOLD ) && c->fg < 8 ) ink = vt_colour[c->fg + 8];
		if ( c->attr & A_DIM ) ink = vt_colour[8];
		cur = (BOOL)( caret && v->show_cursor && row == v->cy && col == v->cx && v->back == 0 );
		if ( ( c->attr & A_REV ) ^ ( cur ? A_REV : 0 ) ) {
			UW t = ink;

			ink = ground;
			ground = t;
		}
		if ( ground != GROUND ) {
			r.left = x;
			r.right = x + c->w * cw;
			r.top = y;
			r.bottom = y + LT_LH;
			(void)dp_fill_rect(w->gid, &r, ground);
		}
		if ( c->n > 0 && !( c->n == 1 && c->ch[0] == ' ' ) && !( c->attr & A_INVIS ) ) {
			for ( k = 0; k < c->n; k++ ) buf[k] = c->ch[k];
			buf[k] = 0;
			(void)dp_text(w->gid, x, y + LT_PX, buf, ink, LT_PX);
			if ( c->attr & A_BOLD ) (void)dp_text(w->gid, x + 1, y + LT_PX, buf, ink, LT_PX);
		}
		if ( c->attr & A_UNDER ) {
			r.left = x;
			r.right = x + c->w * cw;
			r.top = y + LT_LH - 2;
			r.bottom = y + LT_LH - 1;
			(void)dp_fill_rect(w->gid, &r, ink);
		}
	}
}

EXPORT void lt_vt_draw( LTVT *v, LTWIN *w, BOOL caret, BOOL all )
{
	T_DPRECT	r;
	INT		row, ww = 600, wh = 360;
	T_OBWPOS	wp;
	SZ		asz = 0;

	if ( all ) {
		if ( ob_rea_rec(w->kw, OB_WR_PLACE, 0, &wp, sizeof(wp), &asz) >= E_OK
		  && asz >= (SZ)sizeof(wp) ) {
			ww = wp.wright - wp.wleft;
			wh = wp.wbottom - wp.wtop;
		}
		r.left = 0;
		r.top = 0;
		r.right = ww;
		r.bottom = wh;
		(void)dp_fill_rect(w->gid, &r, GROUND);
		dirty_all(v);
	}
	/* the cursor's row, where it was and where it is */
	dirty(v, v->drawn_cy);
	dirty(v, v->cy);
	for ( row = 0; row < v->rows; row++ ) {
		if ( !v->dirty[row] ) continue;
		v->dirty[row] = FALSE;
		draw_row(v, w, row, caret);
	}
	v->drawn_cy = v->cy;
	v->caret_x = LT_PAD + v->cx * cell_w(v);
	v->caret_y = LT_PAD + v->cy * LT_LH;
}
