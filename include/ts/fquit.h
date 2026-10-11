/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	fquit.h
 *	強制終了: Shift+Pause, the window that lists the processes, and the
 *	desktop started again (outer_kernel/wm/fquit.c, design 5.5.1,
 *	5.5.3 - 5.5.5)
 *
 *	Shift+Pause and Ctrl+Shift+Pause are taken from the input layer as
 *	the keyboard reports them, before the ring the desktop reads, by a
 *	task of their own above the input's: they work whatever the desktop
 *	is doing, and while the desktop does nothing at all.
 */

#ifndef __TS_FQUIT_H__
#define __TS_FQUIT_H__

#include <tk/typedef.h>
#include <ts/hid.h>

/*
 * What the desktop tells 強制終了 of itself, once it has started
 * (dt_start). Each is called from 強制終了's own tasks.
 *
 *   task	the desktop's task, 0 while it is not running
 *   unsaved	how many of its windows hold changes not saved, their
 *		names put in names one after another with "、" between
 *		(at most max bytes with the terminator); read without its
 *		locks, from a desktop that may have stopped anywhere
 *   start	the desktop started again, after its tasks were stopped,
 *		its windows closed and its variables put back as they were
 *		when the machine started (fquit.c)
 */
typedef struct {
	ID	(*task)( void );
	INT	(*unsaved)( UB *names, INT max );
	ER	(*start)( void );
} T_FQDESK;

IMPORT void knl_fq_desktop( CONST T_FQDESK *d );

/* Started with the windows (knl_wm_init) */
IMPORT ER   knl_fq_init( void );

/*
 * The input layer (device/usb/hid.c) shows every event to 強制終了
 * before it puts it in the ring, under the ring's lock: TRUE when it
 * was taken and goes no further. inj is an event put in by hand.
 */
IMPORT BOOL knl_fq_input( CONST T_HIDEV *ev, BOOL inj );

/* The window that lists the processes opened (the desktop's 小物 強制終了), or closed when it is up */
IMPORT void knl_fq_open( void );

/* How many times the desktop has been started again since the machine started */
IMPORT INT  knl_fq_restarts( void );

/*
 * Between 強制終了 and the layers it puts in order.
 *
 * knl_wm_front_pick: the window a person works in and the popups shown
 * in front of it, nearest first; E_TMOUT when the windows' lock is not
 * had within tmo. knl_wm_front_any: the window nearest the viewer that
 * is not a popup, with the keys or not. knl_wmobj_who: who a window is for -- the process
 * that made it, and the process a window of the system's was opened for
 * (knl_wmobj_act_for). knl_wmobj_count: the windows a process made.
 *
 * knl_wm_tsk_end, knl_pn_wins_gone, knl_mn_tsk_end, knl_ob_tsk_end: the
 * windows, the panels and menus in them, the menus and the keys to
 * objects the tasks tids made, closed: the tasks were stopped where
 * they stood and cannot.
 */
IMPORT ER   knl_wm_front_pick( TMO tmo, INT *p_wid, INT *pops, INT max, INT *p_npop );
IMPORT ER   knl_wm_front_any( TMO tmo, INT *p_wid );
IMPORT ER   knl_wmobj_who( INT wid, TMO tmo, ID *p_maker, ID *p_for );
IMPORT INT  knl_wmobj_count( ID pid, TMO tmo );
IMPORT void knl_wmobj_act_for( ID pid );
IMPORT INT  knl_wm_tsk_end( CONST ID *tids, INT ntid );
IMPORT void knl_pn_wins_gone( CONST INT *wids, INT n );
IMPORT INT  knl_mn_tsk_end( CONST ID *tids, INT n );
IMPORT INT  knl_ob_tsk_end( CONST ID *tids, INT ntid );

#if USE_KTEST
/* What the tests look at */
#define FQ_ROWS_SEEN	64

typedef struct {
	INT	wid;			/* the window that lists the processes, 0 not up */
	INT	askwid;			/* the panel asking whether to start the desktop again, 0 none */
	BOOL	grab;			/* every event is its */
	INT	nrow;
	ID	pid[FQ_ROWS_SEEN];	/* the rows, top first */
	ID	sel;			/* the row chosen, 0 none */
	BOOL	confirm;		/* 立ち上げ直す pressed once: asking */
	BOOL	armed;			/* Shift+Pause with the desktop stuck: the next starts it again */
	INT	restarts;
	UW	said;			/* messages so far */
	char	text[256];		/* the last */
} T_FQSTAT;

IMPORT void knl_fq_stat( T_FQSTAT *st );
IMPORT void knl_ob_test_hold( BOOL on );	/* obname.c: the objects' lock held by the test */
#endif

#endif /* __TS_FQUIT_H__ */
