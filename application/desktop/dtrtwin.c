/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	dtrtwin.c
 *	基本動画再生: a record of 実時間制御TAD played in a window of its own
 *	(design 11.12, 15.5)
 *
 *	The window is one of the desktop's (DTOWN): its frame moves it,
 *	makes it larger and smaller and closes it as for any window, and
 *	全画面表示 puts it over the whole screen above the message line. The
 *	record's scene is fitted inside its work area by the player (dtrt.c),
 *	on the colour round it chosen in the menu (black until then, as the
 *	program's template has it). A program whose <realtime> does not play
 *	by itself waits for 再生. The media the program declares are its
 *	playlist: プレイリスト表示 lists them by the title, artist and number
 *	their elements give, and the one chosen is played. What is playing is
 *	shown on the message line. The second button opens the menu, a letter
 *	with Ctrl chooses its item; Space and the arrows are the player's own
 *	keys. Nothing in the record is changed.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <ts/wm.h>
#include <ts/dp.h>
#include <ts/hid.h>
#include <ts/tad.h>
#include <ts/tadview.h>
#include <ts/om.h>
#include <ts/mn.h>
#include <ts/rtad.h>
#include <ts/sysdef.h>
#include "desktop.h"

#define RW_MAX		4		/* players open at once */
#define RW_W		800		/* a window's work area when the object says none */
#define RW_H		600
#define RW_TRACKS	64

#define KEY_SPACE	0x2C
#define KEY_RIGHT	0x4F
#define KEY_LEFT	0x50
#define MOD_CTRL	( HID_MOD_LCTRL | HID_MOD_RCTRL )

typedef struct {
	TS_UUID		id;
	INT		recno;
	DTRT		*rt;
	UW		ground;		/* round the scene */
	UW		paper;		/* the scene's own */
	INT		ww, wh;		/* the work area the scene was laid out for */
	INT		track;		/* the one played from the list, -1 none */
	UB		shown[WM_LABEL_MAX];	/* what the message line says of it */
	UB		name[TAD_NAME_MAX];
} RTWIN;

LOCAL CONST DTOWN	rw_own;

LOCAL void rw_draw( DTWIN *d )
{
	RTWIN		*w = (RTWIN *)d->self;
	T_WMWIN		ww;
	T_DPRECT	all;
	INT		gid = wm_gid(d->wid);

	if ( w == NULL || gid < 0 || wm_ref(d->wid, &ww) < E_OK ) return;
	knl_memset(&all, 0, sizeof(all));
	all.right = ww.work.right - ww.work.left;
	all.bottom = ww.work.bottom - ww.work.top;
	dp_fill_rect(gid, &all, w->ground);
	if ( w->rt == NULL ) {
		w->rt = dtrt_open(d->wid, &w->id, w->recno, &all, 0, 0, w->paper, 0);
		w->ww = all.right;
		w->wh = all.bottom;
	} else if ( w->ww != all.right || w->wh != all.bottom ) {
		w->ww = all.right;
		w->wh = all.bottom;
		dtrt_relayout(w->rt, &all);
		dtrt_visible(w->rt, TRUE);
	} else {
		dtrt_redraw(w->rt);
	}
	wm_update();
}

LOCAL void rw_close( DTWIN *d )
{
	RTWIN	*w = (RTWIN *)d->self;

	if ( w == NULL ) return;
	if ( w->rt != NULL ) dtrt_close(w->rt);
	(void)wm_msg_put(NULL);
	tm_printf((UB *)"player: closed\n");
	Kfree(w);
}

EXPORT void dt_rtwin_open( DTWIN *from, CONST T_VOBJ *v )
{
	RTWIN		*w;
	DTWIN		*d;
	T_DPRECT	o, r;
	UINT		sw = 1024, sh = 768;
	INT		n = 0, recno = ( v->recno >= 0 ) ? v->recno : 0;
	T_TAD		*rec;

	(void)from;
	for ( d = dt_own_next(&rw_own, NULL); d != NULL; d = dt_own_next(&rw_own, d) ) {
		w = (RTWIN *)d->self;
		if ( w != NULL && ts_uuid_cmp(&w->id, &v->target) == 0 ) {
			wm_raise(d->wid);
			wm_focus(d->wid);
			wm_update();
			return;
		}
		n++;
	}
	rec = om_store_get(&v->target, recno);
	if ( n >= RW_MAX || rec == NULL ) {
		(void)wm_msg_put("基本動画再生で開けません");
		return;
	}
	if ( !dtrt_has(rec) ) {
		(void)wm_msg_put("実時間制御のデータがありません");
		return;
	}
	w = (RTWIN *)Kmalloc(sizeof(RTWIN));
	if ( w == NULL ) return;
	knl_memset(w, 0, sizeof(*w));
	w->id = v->target;
	w->recno = recno;
	w->ground = 0x00000000U;
	w->track = -1;
	if ( om_store_paper(&v->target, &w->paper) < E_OK || w->paper == TV_PAPER_LOOK ) w->paper = 0x00000000U;
	if ( om_store_name(&v->target, w->name, sizeof(w->name)) < 0 ) w->name[0] = 0;

	/* as large as the object says, inside the screen above the message line */
	(void)dt_screen_size(&sw, &sh);
	if ( om_store_window(&v->target, &r) < E_OK ) {
		r.left = 80;
		r.top = 60;
		r.right = r.left + RW_W;
		r.bottom = r.top + RW_H;
	}
	o = r;
	if ( o.right - o.left > (INT)sw - 40 ) o.right = o.left + (INT)sw - 40;
	if ( o.bottom - o.top > (INT)sh - wm_msg_height() - 60 ) o.bottom = o.top + (INT)sh - wm_msg_height() - 60;
	if ( o.right > (INT)sw ) {
		o.left -= o.right - (INT)sw;
		o.right = (INT)sw;
	}
	if ( o.bottom > (INT)sh - wm_msg_height() ) {
		o.top -= o.bottom - ( (INT)sh - wm_msg_height() );
		o.bottom = (INT)sh - wm_msg_height();
	}
	if ( o.left < 0 ) o.left = 0;
	if ( o.top < 0 ) o.top = 0;
	d = dt_own_open(&rw_own, w, &o, WM_ATTR_FRAME | WM_ATTR_TITLE | WM_ATTR_RESIZE,
			(CONST char *)w->name, "basic-player");
	if ( d == NULL ) {
		Kfree(w);
		return;
	}
	tm_printf((UB *)"player: opened %s\n", w->name);
	rw_draw(d);
}

/* 背景色変更: what is round the scene */
LOCAL void rw_ground( DTWIN *d )
{
	static CONST char *CONST names[4] = { "黒", "白", "グレー", "青" };
	static CONST UW		cols[4] = { 0x00000000U, 0x00FFFFFFU, 0x00808080U, 0x00000080U };
	RTWIN			*w = (RTWIN *)d->self;
	INT			at;

	at = dt_list_form(d, "背景色変更", names, 4, -1);
	if ( at >= 0 && at < 4 ) w->ground = cols[at];
	rw_draw(d);
}

/* プレイリスト表示: the media of the program, one chosen to play */
LOCAL void rw_list( DTWIN *d )
{
	RTWIN		*w = (RTWIN *)d->self;
	UB		(*names)[WM_LABEL_MAX] = (UB (*)[WM_LABEL_MAX])Kmalloc(sizeof(UB) * WM_LABEL_MAX * RW_TRACKS);
	CONST char	**list = (CONST char **)Kmalloc(sizeof(char *) * RW_TRACKS);
	INT		idx[RW_TRACKS], n, i, at;

	if ( names != NULL && list != NULL ) {
		n = dtrt_tracks(w->rt, names, idx, RW_TRACKS);
		if ( n == 0 ) {
			dt_tell(d, "プレイリストに曲がありません", NULL);
		} else {
			for ( i = 0; i < n; i++ ) list[i] = (CONST char *)names[i];
			at = dt_list_form(d, "プレイリスト", list, n, -1);
			if ( at >= 0 && at < n ) {
				w->track = at;
				dtrt_play_track(w->rt, idx[at]);
			}
		}
	}
	if ( names != NULL ) Kfree(names);
	if ( list != NULL ) Kfree(list);
	rw_draw(d);
}

/* The track before or after the one chosen last */
LOCAL void rw_step_track( RTWIN *w, INT by )
{
	UB	(*names)[WM_LABEL_MAX] = (UB (*)[WM_LABEL_MAX])Kmalloc(sizeof(UB) * WM_LABEL_MAX * RW_TRACKS);
	INT	idx[RW_TRACKS], n, k, now;

	if ( names == NULL ) return;
	n = dtrt_tracks(w->rt, names, idx, RW_TRACKS);
	Kfree(names);
	if ( n <= 0 ) return;
	/* from the one playing, else from the one chosen last */
	now = dtrt_playing(w->rt);
	for ( k = 0; k < n && idx[k] != now; k++ ) ;
	if ( k < n ) w->track = k;
	w->track = ( w->track < 0 ) ? 0 : ( w->track + by + n ) % n;
	dtrt_play_track(w->rt, idx[w->track]);
}

/* 全画面表示 ticked when the window is over the whole screen */
LOCAL void rw_menu_set( DTWIN *d, ID mid )
{
	TS_UUID	vd;

	if ( ts_str_to_uuid(SYSDEF_MENU_RTPLAY_VIEW, &vd) >= E_OK ) {
		(void)mn_chg_atr(mid, &vd, "rt.full", d->full ? MN_TICK : 0);
	}
}

LOCAL void rw_menu_do( DTWIN *d, CONST T_MNSEL *sel )
{
	RTWIN	*w = (RTWIN *)d->self;

	if ( mn_is(sel, "rt.full") ) dt_fullscreen(d);
	else if ( mn_is(sel, "rt.refresh") ) rw_draw(d);
	else if ( mn_is(sel, "rt.bg") ) rw_ground(d);
	else if ( w->rt == NULL ) return;
	else if ( mn_is(sel, "rt.toggle") ) dtrt_toggle(w->rt);
	else if ( mn_is(sel, "rt.stop") ) dtrt_stop(w->rt);
	else if ( mn_is(sel, "rt.restart") ) dtrt_restart(w->rt);
	else if ( mn_is(sel, "rt.back") ) dtrt_seek_by(w->rt, -10000);
	else if ( mn_is(sel, "rt.fwd") ) dtrt_seek_by(w->rt, 10000);
	else if ( mn_is(sel, "rt.prev") ) rw_step_track(w, -1);
	else if ( mn_is(sel, "rt.next") ) rw_step_track(w, 1);
	else if ( mn_is(sel, "rt.list") ) rw_list(d);
}

/*
 * What is done in the work area. What the program takes with <trigger>
 * or <wait> is its; a program waiting for 再生 is begun by a press;
 * Space and the arrows are the player's. A key with Ctrl is a letter of
 * the menu.
 */
LOCAL BOOL rw_input( DTWIN *d, CONST T_WMEV *ev )
{
	RTWIN	*w = (RTWIN *)d->self;

	if ( w == NULL ) return FALSE;
	/* the desktop takes the message line away at a press: what plays is put there again */
	if ( ev->type == HID_EV_BTN_DOWN || ev->type == HID_EV_KEY_DOWN ) w->shown[0] = 0;
	if ( w->rt == NULL ) return FALSE;
	switch ( ev->type ) {
	case HID_EV_BTN_DOWN:
		if ( ev->code == 0 ) {
			if ( dtrt_waiting(w->rt) ) dtrt_toggle(w->rt);
			else (void)dtrt_input(w->rt, ev, FALSE);
		}
		wm_update();
		return TRUE;
	case HID_EV_KEY_DOWN:
		if ( ( ev->mods & MOD_CTRL ) != 0 ) return FALSE;
		if ( dtrt_input(w->rt, ev, TRUE) ) {
			(void)dtrt_input(w->rt, ev, FALSE);
			return TRUE;
		}
		switch ( ev->code ) {
		case KEY_SPACE:	dtrt_toggle(w->rt);		break;
		case KEY_LEFT:	dtrt_seek_by(w->rt, -10000);	break;
		case KEY_RIGHT:	dtrt_seek_by(w->rt, 10000);	break;
		default:	(void)dtrt_input(w->rt, ev, FALSE);	break;
		}
		return TRUE;
	default:
		(void)dtrt_input(w->rt, ev, FALSE);
		return TRUE;
	}
}

/* What the program asked, and what plays shown, from the desktop's task */
LOCAL void rw_poll( DTWIN *d )
{
	RTWIN	*w = (RTWIN *)d->self;
	UB	now[WM_LABEL_MAX];
	INT	n = 0, k;
	T_WMWIN	ww;

	if ( w == NULL || w->rt == NULL ) return;
	(void)dtrt_action(w->rt, &n);		/* slide moves mean nothing here */
	/* the window made another size by someone else (全画面, the desktop's keys) */
	if ( wm_ref(d->wid, &ww) >= E_OK
	  && ( ww.work.right - ww.work.left != w->ww || ww.work.bottom - ww.work.top != w->wh ) ) {
		rw_draw(d);
	}
	if ( wm_focused() != d->wid ) return;
	(void)dtrt_now_playing(w->rt, now, sizeof(now));
	for ( k = 0; now[k] != 0 && now[k] == w->shown[k]; k++ ) ;
	if ( now[k] != w->shown[k] ) {
		for ( k = 0; now[k] != 0 && k < WM_LABEL_MAX - 1; k++ ) w->shown[k] = now[k];
		w->shown[k] = 0;
		(void)wm_msg_put(( now[0] != 0 ) ? (CONST char *)now : NULL);
	}
}

LOCAL CONST DTOWN	rw_own = {
	SYSDEF_MENU_RTPLAY,
	rw_draw,
	rw_input,
	NULL,
	rw_menu_set,
	rw_menu_do,
	NULL,
	rw_close,
	rw_poll
};
