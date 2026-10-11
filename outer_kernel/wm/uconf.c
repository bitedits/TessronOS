/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	uconf.c
 *	The person's settings, as ユーザ環境設定 left them (design 16.5.23)
 *
 *	The user information object (SYSDEF_CONF_USER) keeps in its record 1
 *	a line for each thing the accessory sets (lib/libconf's table): the
 *	times, sizes and switches of the pointer, the keyboard, the screen
 *	and the sound, which are entries of the look table, and the name of
 *	the person who uses the machine. They are taken once the store is
 *	there; the look table's entries are what the window layer, the
 *	keyboard and the pointer read as they go, so nothing else is told.
 *	The name is taken at the start only: a name changed is the machine's
 *	from its next start, as the accessory says.
 *
 *	The machine's settings (SYSDEF_CONF_DEV) are taken here too: the
 *	colour scheme, the wallpaper, the keyboard's kind and the mode it
 *	starts in, and the output the sound plays on (SOUND_OUT). The
 *	network's are the network layer's (so_conf.c).
 *
 *	All three objects are watched once taken: a program that writes one
 *	of them -- a settings accessory -- has it taken again, as a notice
 *	of the change (OB_E_CHANGE) reaches a task of this file. Nobody asks
 *	the system to take them; writing them is enough.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <ts/wm.h>
#include <ts/keymap.h>
#include <ts/ob.h>
#include <ts/uuid.h>
#include <ts/conf.h>
#include <ts/sysdef.h>
#include <ts/hid.h>
#include <ts/so.h>
#include <ts/lockup.h>
#include <ts/xf.h>
#include <ts/cpuclk.h>
#include <ts/snd.h>
#include "obj/obj.h"

LOCAL UB	user_name[CF_NAME_MAX * 4 + 1];	/* UTF-8, up to twelve letters */
LOCAL BOOL	user_named = FALSE;

/* A settings object's record, into a buffer of CF_TEXT_MAX; its length or an error */
LOCAL INT conf_text_of( CONST char *uuid, UB *buf )
{
	TS_UUID	u;
	ID	key;
	SZ	asz = 0;
	ER	er;

	if ( ts_str_to_uuid(uuid, &u) < E_OK ) {
		return E_SYS;
	}
	key = ob_opn_obj(&u, OB_OP_READ);
	if ( key < E_OK ) {
		return (INT)key;
	}
	er = ob_rea_rec(key, 1, 0, buf, CF_TEXT_MAX, &asz);
	ob_cls_obj(key);
	if ( er < E_OK ) {
		return (INT)er;
	}
	return (INT)( ( asz > CF_TEXT_MAX ) ? CF_TEXT_MAX : asz );
}

/*
 * The keyboard's and the pointer's attributes as the look table has
 * them, given to the device layer, which takes every report through
 * them: called whenever one of their entries is written. They are
 * written to the input object 入力 (OB_IN_ATTR), as whoever set the
 * entry -- the kernel, or the administrator's program that asked --
 * and directly only before the object layer is up.
 */
EXPORT void knl_input_sync( void )
{
	T_HIDATTR	a;
	ID		key;

	knl_memset(&a, 0, sizeof(a));
	a.pd_on = (UINT)wm_num(LK_PD_ON, 0);
	a.pd_off = (UINT)wm_num(LK_PD_OFF, 0);
	a.speed = (UINT)wm_num(LK_PD_SPEED, 12);
	a.accel = (UINT)wm_num(LK_PD_ACCEL, 7);
	a.main = (UINT)wm_num(LK_PD_MAIN, 0);
	a.absolute = (UINT)wm_num(LK_PD_ABS, 1);
	a.middbl = (UINT)wm_num(LK_PD_MIDDBL, 1);
	a.key_on = (UINT)wm_num(LK_KEY_ON, 0);
	a.key_off = (UINT)wm_num(LK_KEY_OFF, 0);
	a.key_sim = (UINT)wm_num(LK_KEY_SIM, 0);
	a.krp = (UINT)wm_num(LK_KRP, 1);
	a.krp_start = (UINT)wm_num(LK_KRP_START, 800);
	a.krp_int = (UINT)wm_num(LK_KRP_INT, 100);
	a.sclk = (UINT)wm_num(LK_SCLK, 0);
	a.tshift = (UINT)wm_num(LK_TSHIFT, 0);
	key = ob_opn_obj(&ob_uuid_input, OB_OP_WRITE);
	if ( key > 0 ) {
		(void)ob_wri_rec(key, OB_IN_ATTR, 0, &a, sizeof(a), NULL);
		(void)ob_cls_obj(key);
	} else if ( key == E_NOEXS ) {
		(void)ts_hid_setattr(&a);	/* before there are objects */
	}
}

EXPORT ER wm_conf_apply( void )
{
	UB	*text;
	char	v[CF_VAL_MAX];
	INT	len, i, n, got = 0;

	text = (UB *)Kmalloc(CF_TEXT_MAX);
	if ( text == NULL ) {
		return E_NOMEM;
	}
	len = conf_text_of(SYSDEF_CONF_USER, text);
	if ( len < 0 ) {
		Kfree(text);
		return (ER)len;
	}
	for ( i = 0; i < cf_user_nitem; i++ ) {
		CONST CF_ITEM	*it = &cf_user_items[i];

		if ( !cf_get(text, len, it->key, 0, v, sizeof(v)) ) {
			continue;
		}
		n = cf_num(v, it->dflt);
		if ( n < it->lo ) n = it->lo;
		if ( n > it->hi ) n = it->hi;
		(void)knl_look_setting(it->look, (UW)n);
		got++;
	}
	if ( !user_named && cf_get(text, len, CF_USER_NAME, 0, v, sizeof(v)) ) {
		for ( i = 0; v[i] != 0 && i < (INT)sizeof(user_name) - 1; i++ ) {
			user_name[i] = (UB)v[i];
		}
		user_name[i] = 0;
		user_named = TRUE;
	}
	Kfree(text);
	knl_input_sync();
	tm_printf((UB *)"user: %d settings taken, the user is \"%s\"\n", got, user_name);
	return E_OK;
}

/*
 * The processors' clock: at most CPU_MHZ MHz, 0 the most the firmware
 * allows; with no line, or one that is not a number, CPU_MHZ_DEFAULT
 * (cpuclk.c).
 */
LOCAL void cpu_conf( CONST UB *text, INT len )
{
	char	v[CF_VAL_MAX];
	INT	n = CPU_MHZ_DEFAULT;

	if ( cf_get(text, len, "CPU_MHZ", 0, v, sizeof(v)) ) {
		n = cf_num(v, CPU_MHZ_DEFAULT);
		if ( n < 0 ) n = CPU_MHZ_DEFAULT;
	}
	(void)knl_cpu_limit((UW)n);
}

/*
 * The same at the start, as soon as the store is there: the clock is
 * not to wait for the desktop, which takes all the settings later.
 */
EXPORT ER wm_cpu_conf_apply( void )
{
	UB	*text;
	INT	len;

	text = (UB *)Kmalloc(CF_TEXT_MAX);
	if ( text == NULL ) {
		return E_NOMEM;
	}
	len = conf_text_of(SYSDEF_CONF_DEV, text);
	if ( len >= 0 ) {
		cpu_conf(text, len);
	}
	Kfree(text);
	return ( len < 0 ) ? (ER)len : E_OK;
}

/*
 * The machine's settings: SCHEME n (the colour scheme), WALLPAPER uuid
 * [mode] (a picture of the wallpaper box), KBTYPE 0x41 or 0x40 (a
 * Japanese or an English keyboard), KBMODE (the input mode the desktop
 * starts in: 0 かな, 1 英数), the windows' look (WIN_*, cf_look_items), the
 * watchdog (WATCHDOG seconds, 0 off) and the processors' clock (CPU_MHZ).
 * A line not there leaves what is, but for the watchdog and the clock,
 * which go back to their defaults. The wallpaper is read again only
 * when its line has changed: writing another line of the object is not
 * to load a picture the size of the screen again.
 */
LOCAL char	wall_line[CF_VAL_MAX];		/* the WALLPAPER line last laid */

/*
 * The mixer's settings (include/ts/snd.h): the whole's volume as
 * "SOUND_VOL left right mute", each program's as "SOUND_APP uuid volume
 * mute", a line each. Taken as they are read, without being kept again;
 * the mixer has them kept, a second after they last changed, by
 * snd_keep.
 */
#define SND_KEEP_MAX	SND_APP_MAX

LOCAL void snd_keep( void );

LOCAL void snd_conf( CONST UB *text, INT len )
{
	SDMixSet	s;
	char		v[CF_VAL_MAX], w[48];
	INT		k;
	TS_UUID		u;

	knl_snd_mix_keeper(snd_keep);
	if ( cf_get(text, len, "SOUND_VOL", 0, v, sizeof(v)) && cf_word(v, 1, w, sizeof(w)) ) {
		knl_memset(&s, 0, sizeof(s));
		s.what = SND_MIX_ALL;
		(void)cf_word(v, 0, w, sizeof(w));
		s.lv = (UW)cf_num(w, SND_VOL_MAX);
		(void)cf_word(v, 1, w, sizeof(w));
		s.rv = (UW)cf_num(w, SND_VOL_MAX);
		s.mute = ( cf_word(v, 2, w, sizeof(w)) && cf_num(w, 0) != 0 );
		(void)knl_snd_mix_set(&s, FALSE);
	}
	for ( k = 0; k < SND_KEEP_MAX && cf_get(text, len, "SOUND_APP", k, v, sizeof(v)); k++ ) {
		if ( !cf_word(v, 0, w, sizeof(w)) || ts_str_to_uuid(w, &u) < E_OK ) {
			continue;			/* a line left empty */
		}
		knl_memset(&s, 0, sizeof(s));
		s.what = SND_MIX_APP;
		knl_memcpy(s.prog, &u, 16);
		s.lv = s.rv = ( cf_word(v, 1, w, sizeof(w)) ) ? (UW)cf_num(w, SND_VOL_MAX) : SND_VOL_MAX;
		s.mute = ( cf_word(v, 2, w, sizeof(w)) && cf_num(w, 0) != 0 );
		(void)knl_snd_mix_set(&s, FALSE);
	}
}

/* The mixer's settings into the machine's settings: one write for all the lines */
LOCAL void snd_keep( void )
{
	SDMix		*m = (SDMix *)Kmalloc(sizeof(SDMix));
	SDMixSet	*a = (SDMixSet *)Kmalloc(sizeof(SDMixSet) * SND_KEEP_MAX);
	UB		*text = (UB *)Kmalloc(CF_TEXT_MAX);
	char		v[CF_VAL_MAX], us[40];
	TS_UUID		u;
	SZ		asz = 0;
	INT		len, n, k;
	ID		key = E_NOMEM;
	ER		er = E_NOMEM;

	if ( m != NULL && a != NULL && text != NULL && ts_str_to_uuid(SYSDEF_CONF_DEV, &u) >= E_OK ) {
		key = ob_opn_obj(&u, OB_OP_R | OB_OP_WRITE);
	}
	if ( key > 0 ) {
		(void)knl_snd_mix_get(m);
		n = knl_snd_mix_kept(a, SND_KEEP_MAX);
		er = ob_rea_rec(key, 1, 0, text, CF_TEXT_MAX, &asz);
		len = ( er >= E_OK ) ? (INT)asz : 0;
		tm_sprintf((UB *)v, (CONST UB *)"%d %d %d", (INT)m->lv, (INT)m->rv, (INT)( m->mute != 0 ));
		len = cf_put(text, len, CF_TEXT_MAX, "SOUND_VOL", 0, v);
		for ( k = 0; k < SND_KEEP_MAX && len >= 0; k++ ) {
			if ( k < n ) {
				knl_memcpy(&u, a[k].prog, 16);
				(void)ts_uuid_to_str(&u, us, sizeof(us));
				tm_sprintf((UB *)v, (CONST UB *)"%s %d %d", us, (INT)a[k].lv, (INT)( a[k].mute != 0 ));
				len = cf_put(text, len, CF_TEXT_MAX, "SOUND_APP", k, v);
			} else if ( cf_get(text, len, "SOUND_APP", k, v, sizeof(v)) ) {
				len = cf_put(text, len, CF_TEXT_MAX, "SOUND_APP", k, "");	/* one no longer kept */
			} else {
				break;
			}
		}
		er = ( len < 0 ) ? E_LIMIT : ob_wri_rec(key, 1, 0, text, len, &asz);
		if ( er >= E_OK ) er = ob_trn_rec(key, 1, (UD)len);
		ob_cls_obj(key);
	}
	if ( er < E_OK ) {
		tm_printf((UB *)"wm: the mixer's settings not kept (%d)\n", (INT)er);
	}
	if ( m != NULL ) Kfree(m);
	if ( a != NULL ) Kfree(a);
	if ( text != NULL ) Kfree(text);
}

EXPORT ER wm_dev_conf_apply( void )
{
	UB	*text;
	char	v[CF_VAL_MAX], w[64];
	INT	len, i, n;
	BOOL	looked = FALSE;

	text = (UB *)Kmalloc(CF_TEXT_MAX);
	if ( text == NULL ) {
		return E_NOMEM;
	}
	len = conf_text_of(SYSDEF_CONF_DEV, text);
	if ( len < 0 ) {
		Kfree(text);
		return (ER)len;
	}
	if ( cf_get(text, len, "SCHEME", 0, v, sizeof(v)) && v[0] != 0
	  && cf_num(v, -1) >= 0 && (UINT)cf_num(v, -1) != wm_scheme() ) {
		(void)wm_set_scheme((UINT)cf_num(v, 0));
	}
	if ( cf_get(text, len, "WALLPAPER", 0, v, sizeof(v)) && cf_word(v, 0, w, sizeof(w))
	  && knl_strcmp(v, wall_line) != 0 ) {
		TS_UUID	u;
		char	m[8];
		INT	mode = WM_WALL_FIT;

		if ( cf_word(v, 1, m, sizeof(m)) ) mode = cf_num(m, WM_WALL_FIT);
		if ( ts_str_to_uuid(w, &u) >= E_OK && wm_load_wall_obj(&u, xf_data_rec(&u), (UINT)mode) >= E_OK ) {
			knl_obsys_wall(&u, mode);
			for ( i = 0; v[i] != 0 && i < (INT)sizeof(wall_line) - 1; i++ ) {
				wall_line[i] = v[i];
			}
			wall_line[i] = 0;
		}
	}
	for ( i = 0; i < cf_look_nitem; i++ ) {
		CONST CF_ITEM	*it = &cf_look_items[i];

		if ( !cf_get(text, len, it->key, 0, v, sizeof(v)) ) {
			continue;
		}
		n = cf_num(v, it->dflt);
		if ( n < it->lo ) n = it->lo;
		if ( n > it->hi ) n = it->hi;
		if ( knl_wm_look(it->look, n) > 0 ) {
			looked = TRUE;
		}
	}
	if ( looked ) {
		(void)wm_composite();		/* the windows in their look */
	}
	/* the watchdog: seconds from a stopped machine found to its reset, 0 none (lockup.c) */
	knl_wdog_set(cf_get(text, len, "WATCHDOG", 0, v, sizeof(v)) ? cf_num(v, LK_RESET_S_DEFAULT)
								 : LK_RESET_S_DEFAULT);
	cpu_conf(text, len);
	/* the output the sound plays on (include/ts/snd.h); no line leaves it as it is */
	if ( cf_get(text, len, "SOUND_OUT", 0, v, sizeof(v)) && v[0] != 0 ) {
		(void)knl_snd_select((CONST UB *)v);
	}
	/* whether the stream is kept going in silence; no line: it is let stop */
	if ( cf_get(text, len, "SOUND_KEEP", 0, v, sizeof(v)) && v[0] != 0 ) {
		(void)knl_snd_keep(cf_num(v, 0) != 0);
	}
	snd_conf(text, len);
	if ( cf_get(text, len, "KBTYPE", 0, v, sizeof(v)) ) {
		(void)knl_look_setting(LK_KBD_US, ( cf_num(v, 0x41) == 0x40 ) ? 1 : 0);
	}
	if ( cf_get(text, len, "KBMODE", 0, v, sizeof(v)) ) {
		(void)knl_look_setting(LK_KBD_MODE, (UW)( cf_num(v, 1) & 3 ));
	}
	/* the layouts and the keys given other numbers (キー配列) */
	if ( cf_get(text, len, "KBLAYOUT", 0, v, sizeof(v)) ) {
		(void)knl_look_setting(LK_KBD_LAYOUT, (UW)cf_num(v, KM_LATIN_KEYS));
	}
	if ( cf_get(text, len, "KBKANA", 0, v, sizeof(v)) ) {
		(void)knl_look_setting(LK_KBD_KANA, (UW)cf_num(v, KM_KANA_JIS));
	}
	if ( cf_get(text, len, "KBMAP", 0, v, sizeof(v)) ) {
		km_map_parse((CONST char *)v);
	} else {
		km_map_parse("");
	}
	Kfree(text);
	return E_OK;
}

/* One line of the machine's settings written: the key's value, "" for the key alone */
EXPORT ER wm_dev_conf_put( CONST char *key, CONST char *val )
{
	TS_UUID	u;
	UB	*text;
	ID	k;
	SZ	asz = 0;
	INT	len;
	ER	er;

	if ( ts_str_to_uuid(SYSDEF_CONF_DEV, &u) < E_OK ) {
		return E_SYS;
	}
	text = (UB *)Kmalloc(CF_TEXT_MAX);
	if ( text == NULL ) {
		return E_NOMEM;
	}
	k = ob_opn_obj(&u, OB_OP_R | OB_OP_WRITE);
	if ( k < E_OK ) {
		Kfree(text);
		return (ER)k;
	}
	er = ob_rea_rec(k, 1, 0, text, CF_TEXT_MAX, &asz);
	len = ( er >= E_OK ) ? (INT)asz : 0;
	len = cf_put(text, len, CF_TEXT_MAX, key, 0, val);
	er = ( len < 0 ) ? E_LIMIT : ob_wri_rec(k, 1, 0, text, len, &asz);
	if ( er >= E_OK ) er = ob_trn_rec(k, 1, (UD)len);
	ob_cls_obj(k);
	Kfree(text);
	return er;
}

/* A setting of the machine's read: the value's words into v; FALSE when there is no such line */
EXPORT BOOL wm_dev_conf_get( CONST char *key, char *v, INT max )
{
	UB	*text = (UB *)Kmalloc(CF_TEXT_MAX);
	INT	len;
	BOOL	got = FALSE;

	if ( text == NULL ) {
		return FALSE;
	}
	len = conf_text_of(SYSDEF_CONF_DEV, text);
	if ( len > 0 ) got = cf_get(text, len, key, 0, v, max);
	Kfree(text);
	return got;
}

/* ---------------------------------------------------------------- watched */

#define WATCH_PRI	30		/* low, and within 1..CNF_MAX_TSKPRI: beyond it the task is not made */

LOCAL ID	watch_tsk = 0;

LOCAL void conf_watch( INT stacd, void *exinf )
{
	CONST char *CONST which[3] = { SYSDEF_CONF_NET, SYSDEF_CONF_USER, SYSDEF_CONF_DEV };
	TS_UUID	u[3], ch;
	T_OBCRE	c;
	T_OBNTF	req;
	T_OBNTM	m;
	ID	port = 0, key[3];
	SZ	asz = 0;
	INT	i;

	knl_memset(&c, 0, sizeof(c));
	c.type = OB_T_CHANNEL;
	if ( ob_cre_obj(&c, &ch) < E_OK || ( port = ob_opn_obj(&ch, OB_OP_READ | OB_OP_WRITE | OB_O_NOWAIT) ) <= 0 ) {
		watch_tsk = 0;
		tk_exd_tsk();
	}
	/*
	 * The network's settings, taken behind the desktop's start: the
	 * interface may have to ask the network for an address, which is
	 * not to hold up the first window. A machine run for its tests
	 * keeps the interface its tests set.
	 */
#if !USE_KTEST
	(void)so_conf(SO_CONF_APPLY);
#endif
	knl_memset(&req, 0, sizeof(req));
	req.events = OB_E_CHANGE;
	for ( i = 0; i < 3; i++ ) {
		key[i] = 0;
		if ( ts_str_to_uuid(which[i], &u[i]) < E_OK ) continue;
		key[i] = ob_opn_obj(&u[i], OB_OP_ATRRD);
		if ( key[i] > 0 && ob_ntf_evt(key[i], 1, &req, port) <= 0 ) {
			(void)ob_cls_obj(key[i]);
			key[i] = 0;
		}
	}
	for ( ;; ) {
		BOOL	got[3] = { FALSE, FALSE, FALSE };

		if ( ob_rea_rec(port, 0, 0, &m, sizeof(m), &asz) < E_OK || asz == 0 ) {
			tk_dly_tsk(200);		/* nothing yet */
			continue;
		}
		/* a write and the cut after it are one change: what else comes soon is the same */
		for ( ;; ) {
			for ( i = 0; i < 3; i++ ) {
				if ( ts_uuid_cmp(&m.uuid, &u[i]) == 0 ) got[i] = TRUE;
			}
			tk_dly_tsk(100);
			if ( ob_rea_rec(port, 0, 0, &m, sizeof(m), &asz) < E_OK || asz == 0 ) break;
		}
#if !USE_KTEST
		if ( got[0] ) {
			tm_printf((UB *)"settings: the network's were written\n");
			(void)so_conf(SO_CONF_APPLY);
		}
#endif
		if ( got[1] ) {
			tm_printf((UB *)"settings: the person's were written\n");
			(void)wm_conf_apply();
		}
		if ( got[2] ) {
			tm_printf((UB *)"settings: the machine's were written\n");
			(void)wm_dev_conf_apply();
		}
	}
}

/* The three settings objects watched from now on */
EXPORT ER wm_conf_watch( void )
{
	T_CTSK	ct;

	if ( watch_tsk > 0 ) {
		return E_OK;
	}
	knl_memset(&ct, 0, sizeof(ct));
	ct.tskatr = TA_HLNG | TA_RNG0;
	ct.task = (FP)conf_watch;
	ct.itskpri = WATCH_PRI;
	ct.stksz = 16 * 1024;
	watch_tsk = tk_cre_tsk(&ct);
	if ( watch_tsk <= 0 ) {
		tm_printf((UB *)"settings: the watcher could not be made (%d)\n", (INT)watch_tsk);
		return (ER)watch_tsk;
	}
	return tk_sta_tsk(watch_tsk, 0);
}

EXPORT INT wm_user_name( UB *buf, INT max )
{
	INT	i;

	if ( buf == NULL || max <= 0 ) {
		return 0;
	}
	for ( i = 0; user_name[i] != 0 && i < max - 1; i++ ) {
		buf[i] = user_name[i];
	}
	buf[i] = 0;
	return i;
}
