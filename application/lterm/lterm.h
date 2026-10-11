/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	lterm.h
 *	Lines of text in a window of a program's own: what コンソール and
 *	シリアル通信 are drawn with (design 16.5.19)
 *
 *	The lines are kept in a ring, the oldest going when it is full; the
 *	bytes a terminal is sent are put in as a terminal takes them. A line
 *	longer than the window is wide goes on over as many rows as it takes,
 *	and the view is counted in those rows: the last of them, unless it
 *	has been wound back with the keys, the wheel or the scroll bar down
 *	the window's right side. The window, its channel and the notices are
 *	here too, since both programs have them the same way.
 */

#ifndef __LTERM_H__
#define __LTERM_H__

#include <tk/typedef.h>
#include <ts/uapp.h>

extern unsigned long strlen( const char *s );
IMPORT ER ts_get_mono( UD *p_ns );

#define LT_LINES	300		/* lines kept */
#define LT_COLS		1024		/* bytes of one line */
#define LT_PX		14		/* the letters' size */
#define LT_LH		18		/* one row to the next */
#define LT_PAD		4
#define LT_WHEEL	3		/* rows a notch of the wheel winds */

typedef struct {
	UB	line[LT_LINES][LT_COLS];
	INT	len[LT_LINES];
	INT	first;			/* the oldest line in the ring */
	INT	n;			/* lines in use; the last is being written */
	BOOL	cr;			/* a CR came: the line starts again */
	INT	esc;			/* in an escape sequence */
	INT	back;			/* rows the view is wound back from the foot */

	/* the rows each line takes, for the width they were counted at */
	INT	rows[LT_LINES];		/* 0: not counted since it changed */
	INT	wrap_w;

	/* what the last drawing showed: the scroll bar works from it */
	INT	total;			/* rows there are */
	INT	shown;			/* rows the window shows */
	T_DPRECT bar;			/* the bar, in the work area */
	INT	grab;			/* the knob held: where it was taken, -1 not */

	/* where the tail ended (the caret), -1 when it was not in the view */
	INT	tail_x, tail_y;
	BOOL	hold;			/* TRUE: lt_draw leaves showing it to the caller */
} LTERM;

/* The window of the program and where what happens to it arrives */
typedef struct {
	TS_UUID	win, ch;
	ID	kw;			/* the window's key */
	ID	port;			/* the channel's */
	INT	gid;
} LTWIN;

IMPORT void lt_clear( LTERM *t );
IMPORT void lt_put( LTERM *t, CONST UB *s, INT n );
IMPORT void lt_puts( LTERM *t, CONST char *s );
IMPORT void lt_putn( LTERM *t, D v );

/* Drawn: the lines, then `tail` (a prompt and what is typed, or NULL) with the caret */
IMPORT void lt_draw( LTERM *t, LTWIN *w, CONST UB *tail, BOOL caret );
IMPORT void lt_wind( LTERM *t, INT rows );	/* + back, - forward */

/*
 * A press, a move or a let go of the first button, or a wheel: what the
 * scroll bar and the wheel do. Answers TRUE when it was theirs and the
 * view changed or may have.
 */
IMPORT BOOL lt_pointer( LTERM *t, CONST T_OBNTM *m );

IMPORT ER   lt_open( LTWIN *w, CONST char *name, CONST char *json );
IMPORT void lt_close( LTWIN *w );

/* Text helpers: a number into a buffer; answers the length */
IMPORT INT  lt_num( UB *out, D v );
IMPORT BOOL lt_same( CONST UB *a, CONST char *b );
IMPORT ER   lt_uuid( CONST char *s, TS_UUID *u );	/* "xxxxxxxx-xxxx-..." */

/* ---------------------------------------------------------------- lt_vt.c */

/*
 * The screen of a terminal of the kind tessron (lt_vt.c): what a POSIX
 * program on a pseudo terminal writes, its cursor addressed, in cells
 * of the width of a half-width letter. The lines that go off the top of
 * its first screen go into an LTERM.
 */
#define VT_ROWS_MAX	80
#define VT_COLS_MAX	200
#define VT_PAR_MAX	16

typedef struct {
	UB	ch[6];			/* the letter, UTF-8, with the marks put on it */
	UB	n;			/* bytes of it; 0 the right half of a wide letter */
	UB	w;			/* cells it takes: 1, 2, or 0 for that right half */
	UB	attr;
	UB	fg, bg;			/* the colours, 0..15, 0xFF the window's own */
} VTCELL;

typedef struct {
	INT	rows, cols;
	VTCELL	scr[2][VT_ROWS_MAX][VT_COLS_MAX];	/* the first screen and the second */
	INT	alt;			/* the second is shown */
	INT	cx, cy;			/* the cursor */
	BOOL	wrapnext;		/* the last column written: the next letter goes on a new line */
	INT	top, bot;		/* the scrolling region */
	INT	scx, scy;		/* the cursor kept by ESC 7 */
	UB	sattr, sfg, sbg;
	UB	attr, fg, bg;		/* what a letter is written with */
	BOOL	insert, appkeys, show_cursor, autowrap;
	UB	tabs[VT_COLS_MAX];

	INT	state;			/* in a sequence: 1 ESC, 2 CSI, 3 one byte to pass, 4/5 OSC */
	INT	par[VT_PAR_MAX];
	INT	npar;
	BOOL	priv;
	UB	utf[6];			/* a letter of several bytes coming */
	INT	un, uneed;
	UW	ucp;

	UB	reply[64];		/* answers to the program, for the caller to send */
	INT	nreply;

	BOOL	dirty[VT_ROWS_MAX];	/* rows to draw again */
	INT	drawn_cy;
	INT	cw;			/* pixels of a cell */
	INT	back;			/* not 0: the record of lines is shown instead */
	INT	caret_x, caret_y;	/* where the cursor was drawn, for the converter's letters */
	LTERM	*log;
} LTVT;

/* The keys a terminal sends sequences for */
#define VTK_UP		1
#define VTK_DOWN	2
#define VTK_RIGHT	3
#define VTK_LEFT	4
#define VTK_HOME	5
#define VTK_END		6
#define VTK_INS		7
#define VTK_DEL		8
#define VTK_PGUP	9
#define VTK_PGDN	10
#define VTK_BTAB	11

IMPORT void lt_vt_init( LTVT *v, LTERM *log, INT rows, INT cols );
IMPORT void lt_vt_resize( LTVT *v, INT rows, INT cols );
IMPORT void lt_vt_put( LTVT *v, CONST UB *s, INT n );
IMPORT INT  lt_vt_reply( LTVT *v, UB *out, INT max );	/* answers waiting; their length */
IMPORT INT  lt_vt_key( LTVT *v, INT key, UB *out );	/* the bytes of a key (VTK_*) */
IMPORT void lt_vt_fit( LTVT *v, LTWIN *w, INT *p_rows, INT *p_cols );	/* rows and columns the window holds */
IMPORT void lt_vt_draw( LTVT *v, LTWIN *w, BOOL caret, BOOL all );	/* the rows changed, or all */
IMPORT void lt_vt_flush( LTVT *v );	/* what the first screen shows, into the record of lines */

#endif /* __LTERM_H__ */
