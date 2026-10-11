/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	dtdisk.c
 *	ディスク操作: disks attached as volumes of objects (design 11.26)
 *
 *	A partition of TSFS is attached as a volume of objects with 接続,
 *	and the link to the object the volume opens with -- its top, made
 *	when it has none -- is put in the first window: the disk's link.
 *	切り離し takes a volume away again, unless an object of it is open
 *	in a window or by a program. The links to its objects stay, drawn
 *	pale (虚身), and come back as they were when it is attached again.
 *	状態表示 tells how full the disk of an object is.
 *
 *	The volumes the person attached are remembered by their UUIDs in
 *	the device box (knl_obdev_vols_put) until they are taken away with
 *	切り離し or by deleting the disk's link in the first window
 *	(取り外し). Such a volume is attached again whenever its disk is
 *	found: at the start, and when a disk comes.
 *
 *	What happens without anyone asking is followed here as well. When a
 *	disk is taken out, its volumes are gone (the kernel's), the windows
 *	of their objects are closed, with a message saying so, and the
 *	links are drawn again. A window with changes not saved is not
 *	closed silently: the person is asked to save them to another
 *	volume, to have them saved when the disk is put back, or to throw
 *	them away.
 *
 *	Each step is said on the console, "disk: ...", for the tests.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <ts/wm.h>
#include <ts/part.h>
#include <ts/om.h>
#include <ts/ob.h>
#include <ts/fs.h>
#include <ts/tsfs.h>
#include <ts/tad.h>
#include <ts/uuid.h>
#include "desktop.h"

#define DK_DISKS	2048		/* disks and partitions looked at: as many as the device manager knows */
#define DK_ROWS		16		/* partitions a panel offers */
#define DK_KEEP		256		/* volumes remembered as attached */
#define DK_WAIT		16		/* records waiting for their disk to be put back */
#define DK_PATH		64		/* bytes of a volume's path kept here */
#define DK_TO		8		/* other volumes offered to save to */

#define PN_DISK		10		/* the panel's choices, one per partition */
#define PN_TO		40		/* where changes of a window whose disk went are to go */

/* A record whose disk went while it had changes, kept until the disk is back */
typedef struct {
	BOOL	used;
	TS_UUID	vuuid;			/* the volume it is on */
	TS_UUID	id;
	INT	recno;
	UB	*xml;			/* the record as it was being changed */
	SZ	size;
	UB	name[TAD_NAME_MAX];
} DKWAIT;

LOCAL DKWAIT	dk_wait[DK_WAIT];
LOCAL UW	dk_volser = 0;		/* knl_obfile_serial as last followed */
LOCAL UW	dk_devser = 0;		/* knl_obdev_serial */
LOCAL BOOL	dk_started = FALSE;

/*
 * The windows, what each shows, whether that was on a volume and which,
 * as last looked at. A window is known by its number and its object
 * together: a window closed and another opened in its place between two
 * looks may be given the same number, and must not be taken for the one
 * before (and closed for a volume it never was on).
 */
LOCAL INT	dk_wid[DT_MAX_WIN];
LOCAL TS_UUID	dk_id[DT_MAX_WIN];
LOCAL BOOL	dk_onvol[DT_MAX_WIN];
LOCAL TS_UUID	dk_vu[DT_MAX_WIN];

/* ---------------------------------------------------------------- small things */

LOCAL INT put( UB *buf, INT at, INT max, CONST char *s )
{
	while ( *s != 0 && at < max - 1 ) {
		buf[at++] = (UB)*s++;
	}
	buf[at] = 0;
	return at;
}

LOCAL INT put_num( UB *buf, INT at, INT max, UD v )
{
	char	t[24];
	INT	n = 0;

	do {
		t[n++] = (char)( '0' + v % 10 );
		v /= 10;
	} while ( v > 0 && n < (INT)sizeof(t) );
	while ( n > 0 && at < max - 1 ) {
		buf[at++] = (UB)t[--n];
	}
	buf[at] = 0;
	return at;
}

/* A size the way a person reads it: 512KB, 64MB, 3GB */
LOCAL INT put_size( UB *buf, INT at, INT max, UD bytes )
{
	if ( bytes >= 1024ULL * 1024 * 1024 ) {
		at = put_num(buf, at, max, ( bytes + 512ULL * 1024 * 1024 ) / ( 1024ULL * 1024 * 1024 ));
		return put(buf, at, max, "GB");
	}
	if ( bytes >= 1024ULL * 1024 ) {
		at = put_num(buf, at, max, ( bytes + 512ULL * 1024 ) / ( 1024ULL * 1024 ));
		return put(buf, at, max, "MB");
	}
	at = put_num(buf, at, max, bytes / 1024);
	return put(buf, at, max, "KB");
}

LOCAL BOOL zero_uuid( CONST TS_UUID *u )
{
	INT	i;

	for ( i = 0; i < 16; i++ ) {
		if ( u->b[i] != 0 ) return FALSE;
	}
	return TRUE;
}

/* What a disk is called: its label, or its device's name */
LOCAL void disk_name( CONST UB *label, CONST char *dev, UB *out, INT max )
{
	INT	k;

	if ( label != NULL && label[0] != 0 ) {
		(void)put(out, 0, max, (CONST char *)label);
		return;
	}
	k = put(out, 0, max, "ディスク ");
	(void)put(out, k, max, dev);
}

/* ---------------------------------------------------------------- what is remembered as attached */

LOCAL void keep_vol( CONST TS_UUID *vuuid, CONST TS_UUID *top, BOOL keep )
{
	char	us[TS_UUID_STRLEN + 1];
	ER	er = knl_obdev_vols_put(vuuid, top, keep);

	(void)ts_uuid_to_str(vuuid, us, sizeof(us));
	tm_printf((UB *)"disk: %s %s (%d)\n", keep ? "remember" : "forget", us, (INT)er);
}

/* The volumes remembered: their UUIDs, and their top objects after them; the caller frees */
LOCAL TS_UUID *kept_vols( INT *p_n )
{
	TS_UUID	*vu = (TS_UUID *)Kmalloc(sizeof(TS_UUID) * DK_KEEP * 2);

	*p_n = ( vu != NULL ) ? knl_obdev_vols_get(vu, vu + DK_KEEP, DK_KEEP) : 0;
	return vu;
}

LOCAL INT kept_at( CONST TS_UUID *vu, INT n, CONST TS_UUID *vuuid )
{
	INT	i;

	for ( i = 0; i < n; i++ ) {
		if ( ts_uuid_cmp(&vu[i], vuuid) == 0 ) return i;
	}
	return -1;
}

/* ---------------------------------------------------------------- the first window's links */

/* Whether the first window holds a link to the object */
LOCAL BOOL on_desktop( CONST TS_UUID *u )
{
	DTWIN	*t = dt_first_win();
	T_VOBJ	*v;
	INT	i, n;
	BOOL	hit = FALSE;

	if ( t == NULL || t->rec == NULL || ( v = (T_VOBJ *)Kmalloc(sizeof(T_VOBJ)) ) == NULL ) {
		return FALSE;
	}
	n = tad_lnk_count(t->rec);
	for ( i = 0; i < n && !hit; i++ ) {
		hit = (BOOL)( tad_lnk_get(t->rec, i, v) >= E_OK && ts_uuid_cmp(&v->target, u) == 0 );
	}
	Kfree(v);
	return hit;
}

/* ---------------------------------------------------------------- attaching */

LOCAL void wait_done( void );

/*
 * A partition of TSFS attached by its device's name. The volume's top
 * object is made when it has none: a figure, named as the disk is, the
 * way a cabinet is. 'place' puts a link to it in the first window unless
 * one is there. Answers E_OBJ when the same volume is attached through
 * another device.
 */
LOCAL ER attach_dev( CONST char *dev, BOOL place, UB *p_name, INT max )
{
	T_OBFVREF	*ref;
	TS_UUID		top;
	UB		name[OB_VOL_LABEL + 16];
	ER		er;

	er = ob_att_vol(dev, TSFS_STORE_BLK);
	if ( er < E_OK ) {
		tm_printf((UB *)"disk: attach %s failed %d\n", dev, (INT)er);
		return er;
	}
	ref = (T_OBFVREF *)Kmalloc(sizeof(T_OBFVREF));
	if ( ref == NULL ) {
		return E_NOMEM;
	}
	er = knl_obfile_vol_ref(dev, ref);
	if ( er < E_OK ) {
		Kfree(ref);
		return er;
	}
	disk_name(ref->label, dev, name, sizeof(name));
	top = ref->top;
	if ( zero_uuid(&top) && !ref->rdonly ) {
		er = om_cre_obj(dev, name, OM_BODY_FIG, &top);
		if ( er >= E_OK ) {
			er = knl_obfile_set_top(dev, &top);
		}
		if ( er < E_OK ) {
			tm_printf((UB *)"disk: no top object on %s (%d)\n", dev, (INT)er);
			knl_memset(&top, 0, sizeof(top));
		}
	}
	if ( place ) {
		keep_vol(&ref->vuuid, &top, TRUE);	/* attached by the person: again at the next start */
	}
	Kfree(ref);
	om_store_recheck();
	if ( place && !zero_uuid(&top) && !on_desktop(&top) ) {
		dt_place_on_desktop(&top, name);
	}
	{
		char	us[TS_UUID_STRLEN + 1];

		(void)ts_uuid_to_str(&top, us, sizeof(us));
		tm_printf((UB *)"disk: attached %s \"%s\" top %s\n", dev, (CONST char *)name, us);
	}
	if ( p_name != NULL ) {
		(void)put(p_name, 0, max, (CONST char *)name);
	}
	wait_done();
	dt_draw_all();
	return E_OK;
}

/* ---------------------------------------------------------------- following what happens */

/*
 * The links of a record whose volume went made to say that volume: a
 * link without one meant an object of the same volume, which it no
 * longer is once the record is written somewhere else.
 */
LOCAL void links_name_vol( T_TAD *doc, CONST TS_UUID *vuuid )
{
	T_VOBJ	*v = (T_VOBJ *)Kmalloc(sizeof(T_VOBJ));
	INT	i, n;

	if ( v == NULL || doc == NULL ) {
		if ( v != NULL ) Kfree(v);
		return;
	}
	n = tad_lnk_count(doc);
	for ( i = 0; i < n; i++ ) {
		if ( tad_lnk_get(doc, i, v) < E_OK || !zero_uuid(&v->tvol) || !zero_uuid(&v->tmach) ) {
			continue;
		}
		if ( knl_obfile_vol_of(&v->target) > 0 ) {
			continue;		/* found elsewhere: the save says where */
		}
		v->tvol = *vuuid;
		(void)tad_lnk_set(doc, v);
	}
	Kfree(v);
}

/*
 * A record written as a new object of the volume 'path', named as the
 * one it was, with a link to it in the volume's top (the first window
 * for the system's volume).
 */
LOCAL ER save_to( CONST char *path, CONST UB *name, UINT body, CONST UB *xml, SZ size, TS_UUID *p_new )
{
	TS_UUID	top;
	ER	er;

	er = om_cre_obj(path, name, body, p_new);
	if ( er < E_OK ) {
		return er;
	}
	if ( om_store_get(p_new, 0) == NULL || om_store_restore(p_new, 0, xml, size) == NULL ) {
		return E_IO;
	}
	er = om_store_save(p_new, 0);
	if ( er < E_OK ) {
		return er;
	}
	if ( !dt_disk_top(path, &top) || dt_place_in(&top, p_new, name) < E_OK ) {
		dt_place_on_desktop(p_new, name);
	}
	return E_OK;
}

/* A record kept until its disk is back */
LOCAL BOOL wait_put( CONST TS_UUID *vuuid, CONST TS_UUID *id, INT recno, UB *xml, SZ size, CONST UB *name )
{
	INT	i;

	for ( i = 0; i < DK_WAIT; i++ ) {
		if ( dk_wait[i].used && ts_uuid_cmp(&dk_wait[i].id, id) == 0 && dk_wait[i].recno == recno ) {
			Kfree(dk_wait[i].xml);		/* the newer changes stand */
			dk_wait[i].used = FALSE;
		}
	}
	for ( i = 0; i < DK_WAIT; i++ ) {
		if ( !dk_wait[i].used ) {
			dk_wait[i].used = TRUE;
			dk_wait[i].vuuid = *vuuid;
			dk_wait[i].id = *id;
			dk_wait[i].recno = recno;
			dk_wait[i].xml = xml;
			dk_wait[i].size = size;
			(void)put(dk_wait[i].name, 0, TAD_NAME_MAX, (CONST char *)name);
			return TRUE;
		}
	}
	return FALSE;
}

/* The records kept for a volume that is back, written to it */
LOCAL void wait_done( void )
{
	UB	msg[TAD_NAME_MAX + 64];
	INT	i, k;
	ER	er;

	for ( i = 0; i < DK_WAIT; i++ ) {
		DKWAIT	*w = &dk_wait[i];

		if ( !w->used || !knl_obfile_vuuid_on(&w->vuuid) ) {
			continue;
		}
		er = E_NOEXS;
		if ( knl_obfile_vol_of(&w->id) > 0 ) {
			om_store_forget(&w->id);
			if ( om_store_get(&w->id, w->recno) != NULL
			  && om_store_restore(&w->id, w->recno, w->xml, w->size) != NULL ) {
				er = om_store_save(&w->id, w->recno);
			}
		}
		tm_printf((UB *)"disk: kept changes of %s saved (%d)\n", (CONST char *)w->name, (INT)er);
		k = put(msg, 0, sizeof(msg), "「");
		k = put(msg, k, sizeof(msg), (CONST char *)w->name);
		(void)put(msg, k, sizeof(msg), ( er >= E_OK ) ? "」の変更を保存しました"
							 : "」の変更を保存できませんでした");
		(void)wm_msg_put((CONST char *)msg);
		Kfree(w->xml);
		w->used = FALSE;
	}
}

/*
 * A window whose disk went with changes not saved: the person says
 * where they go -- another volume attached, the disk when it is put
 * back, or nowhere. The window is closed after.
 */
LOCAL void rescue( DTWIN *d, CONST TS_UUID *vuuid, CONST UB *name )
{
	UB		(*to)[DK_PATH], vname[OB_VOL_LABEL + 16], t[WM_LABEL_MAX], msg[TAD_NAME_MAX + 96];
	T_OBFVREF	ref;
	T_WMPANEL	*def;
	T_WMPART	*pt;
	UB		*xml = NULL;
	SZ		size = 0;
	INT		n, m = 0, i, k, pid, w = 600, h, pick = -1;
	UINT		ans;
	DTWIN		*f = dt_first_win();
	TS_UUID		nid;
	ER		er;

	if ( d->xml_view ) {
		(void)dd_xml_commit(d);
	}
	links_name_vol((T_TAD *)d->rec, vuuid);
	if ( om_store_snapshot(&d->id, d->recno, &xml, &size) < E_OK ) {
		tm_printf((UB *)"disk: changes of %s cannot be kept\n", (CONST char *)name);
		return;
	}
	to = (UB (*)[DK_PATH])Kmalloc(sizeof(UB) * DK_PATH * DK_TO);
	n = ( to != NULL ) ? knl_obfile_vols((UB *)to, DK_PATH, DK_TO) : 0;
	for ( i = 0; i < n; i++ ) {
		if ( knl_obfile_vol_ref((CONST char *)to[i], &ref) >= E_OK && !ref.rdonly ) {
			if ( m != i ) knl_memcpy(to[m], to[i], DK_PATH);
			m++;
		}
	}
	tm_printf((UB *)"disk: %s has changes not saved; %d volumes to save to\n", (CONST char *)name, m);
	h = 120 + 26 * ( m + 2 );
	def = dt_pn_new(w, h);
	if ( def == NULL || f == NULL ) {
		if ( def != NULL ) Kfree(def);
		goto keep;
	}
	dt_pn_add(def, WM_PT_LABEL, 0, 14, 4, w - 28, 22, "ディスクが取り外されました。");
	k = put(t, 0, sizeof(t), "「");
	k = put(t, k, sizeof(t), (CONST char *)name);
	(void)put(t, k, sizeof(t), "」の変更は保存されていません。どうしますか:");
	dt_pn_add(def, WM_PT_LABEL, 0, 14, 30, w - 28, 22, (CONST char *)t);
	for ( i = 0; i < m + 2; i++ ) {
		if ( i < m ) {
			if ( knl_obfile_vol_ref((CONST char *)to[i], &ref) >= E_OK ) {
				disk_name(ref.label, (CONST char *)to[i], vname, sizeof(vname));
			} else {
				(void)put(vname, 0, sizeof(vname), (CONST char *)to[i]);
			}
			k = put(t, 0, sizeof(t), "「");
			k = put(t, k, sizeof(t), (CONST char *)vname);
			(void)put(t, k, sizeof(t), "」へ別の実身として保存");
		} else if ( i == m ) {
			(void)put(t, 0, sizeof(t), "ディスクを挿し直したときに保存");
		} else {
			(void)put(t, 0, sizeof(t), "変更を捨てる");
		}
		pt = dt_pn_add(def, WM_PT_CHOICE, PN_TO + i, 24, 58 + 26 * i, w - 48, 22, (CONST char *)t);
		if ( pt != NULL ) {
			pt->group = PN_TO;
			pt->value = ( i == 0 ) ? 1 : 0;
		}
	}
	dt_pn_buttons(def, "実行");
	ans = dt_pn_run(f, def, &pid);
	if ( ans == WM_ANS_OK && pid >= 0 ) {
		for ( i = 0; i < m + 2; i++ ) {
			if ( dt_pn_value(pid, PN_TO + i) != 0 ) pick = i;
		}
	}
	dt_pn_done(f, pid);
	wm_update();
	if ( pick == m + 1 ) {
		tm_printf((UB *)"disk: changes of %s thrown away\n", (CONST char *)name);
		Kfree(xml);
		goto out;
	}
	if ( pick >= 0 && pick < m ) {
		er = save_to((CONST char *)to[pick], name, ( d->fig != NULL ) ? OM_BODY_FIG : OM_BODY_DOC,
			     xml, size, &nid);
		tm_printf((UB *)"disk: changes of %s saved to %s (%d)\n", (CONST char *)name,
			  (CONST char *)to[pick], (INT)er);
		if ( er >= E_OK ) {
			k = put(msg, 0, sizeof(msg), "「");
			k = put(msg, k, sizeof(msg), (CONST char *)name);
			(void)put(msg, k, sizeof(msg), "」を別の実身として保存しました");
			(void)wm_msg_put((CONST char *)msg);
			Kfree(xml);
			goto out;
		}
		dt_tell(f, "保存できませんでした。", "ディスクを挿し直したときに保存します");
	}
    keep:
	/* cancelled, or not written: kept for the disk, rather than lost */
	if ( wait_put(vuuid, &d->id, d->recno, xml, size, name) ) {
		tm_printf((UB *)"disk: changes of %s kept for the disk\n", (CONST char *)name);
		k = put(msg, 0, sizeof(msg), "「");
		k = put(msg, k, sizeof(msg), (CONST char *)name);
		(void)put(msg, k, sizeof(msg), "」の変更は、ディスクを挿し直したときに保存します");
		(void)wm_msg_put((CONST char *)msg);
	} else {
		Kfree(xml);
	}
    out:
	if ( to != NULL ) Kfree(to);
}

/* The windows of objects on a volume that went: closed, and said so */
LOCAL void close_gone( void )
{
	INT	i;

	for ( i = 0; i < DT_MAX_WIN; i++ ) {
		DTWIN	*d = dt_win_at(i);
		UB	name[TAD_NAME_MAX], msg[TAD_NAME_MAX + 96];
		INT	k;

		if ( d == NULL || !d->used || d->wid != dk_wid[i] || !dk_onvol[i]
		  || ts_uuid_cmp(&d->id, &dk_id[i]) != 0 ) {
			continue;
		}
		if ( d == dt_first_win() || knl_obfile_vol_of(&d->id) > 0 ) {
			continue;
		}
		if ( om_store_name(&d->id, name, sizeof(name)) < 0 ) {
			(void)put(name, 0, sizeof(name), "実身");
		}
		k = put(msg, 0, sizeof(msg), "ディスクが取り外されたため、");
		k = put(msg, k, sizeof(msg), (CONST char *)name);
		(void)put(msg, k, sizeof(msg), "のウインドウを閉じました");
		if ( d->dirty && !d->sealed ) {
			rescue(d, &dk_vu[i], name);	/* says where the changes went */
			msg[0] = 0;
		}
		tm_printf((UB *)"disk: closed window of %s\n", (CONST char *)name);
		d->dirty = FALSE;		/* what changed went where the person said */
		dt_close(d);
		if ( msg[0] != 0 ) (void)wm_msg_put((CONST char *)msg);
		dk_wid[i] = 0;
	}
}

/* Which window shows what, and whether that is on a volume: new windows looked at once */
LOCAL void note_windows( BOOL all )
{
	INT	i;

	for ( i = 0; i < DT_MAX_WIN; i++ ) {
		DTWIN	*d = dt_win_at(i);

		if ( d == NULL || !d->used || d->sealed || d->tool != DT_TOOL_NONE ) {
			dk_wid[i] = 0;
			continue;
		}
		if ( all || d->wid != dk_wid[i] || ts_uuid_cmp(&d->id, &dk_id[i]) != 0 ) {
			dk_wid[i] = d->wid;
			dk_id[i] = d->id;
			dk_onvol[i] = (BOOL)( knl_obfile_vol_of(&d->id) > 0 );
			if ( !dk_onvol[i] || knl_obfile_vuuid_of(&d->id, &dk_vu[i]) < E_OK ) {
				knl_memset(&dk_vu[i], 0, sizeof(dk_vu[i]));
			}
		}
	}
}

/*
 * The disks looked over after a device or a medium came or went, and at
 * the start: a partition of TSFS whose volume is remembered as attached
 * is attached again, and what was kept for it is written.
 */
LOCAL void look_disks( void )
{
	UB		*names;
	T_FSPROBE	*pr;
	TS_UUID		*kv;
	UB		path[OB_DV_NAME];
	INT		n, i, k, nk = 0;

	names = (UB *)Kmalloc((SZ)OB_DV_NAME * DK_DISKS);
	pr = (T_FSPROBE *)Kmalloc(sizeof(T_FSPROBE));
	kv = kept_vols(&nk);
	if ( names == NULL || pr == NULL || kv == NULL ) {
		goto done;
	}
	n = ( nk > 0 ) ? knl_obdev_disks(names, DK_DISKS) : 0;
	for ( i = 0; i < n; i++ ) {
		CONST char	*dev = (CONST char *)( names + i * OB_DV_NAME );

		if ( knl_obfile_vol_path(dev) > 0 ) {
			continue;		/* attached already */
		}
		if ( knl_fs_probe(dev, pr) < E_OK || pr->kind != FS_KIND_TSFS
		  || kept_at(kv, nk, &pr->vuuid) < 0 || knl_obfile_vuuid_on(&pr->vuuid) ) {
			continue;
		}
		for ( k = 0; dev[k] != 0 && k < OB_DV_NAME - 1; k++ ) path[k] = (UB)dev[k];
		path[k] = 0;
		tm_printf((UB *)"disk: %s is back\n", (CONST char *)path);
		(void)attach_dev((CONST char *)path, FALSE, NULL, 0);
	}
	wait_done();

    done:
	if ( names != NULL ) Kfree(names);
	if ( pr != NULL ) Kfree(pr);
	if ( kv != NULL ) Kfree(kv);
}

EXPORT void dt_disk_poll( void )
{
	UW	vs, ds;

	knl_obxref_drain();		/* links to other volumes given up by a collection, say */
	vs = knl_obfile_serial();
	ds = knl_obdev_serial();

	if ( !dk_started ) {
		dk_started = TRUE;
		dk_volser = vs;
		dk_devser = ds;
		note_windows(TRUE);
		look_disks();			/* the disks of the links left on the desktop */
		return;
	}
	if ( ds != dk_devser ) {
		dk_devser = ds;
		look_disks();
		vs = knl_obfile_serial();
	}
	if ( vs != dk_volser ) {
		dk_volser = vs;
		close_gone();
		om_store_recheck();
		note_windows(TRUE);
		tm_printf((UB *)"disk: volumes changed\n");
		dt_draw_all();
		wm_update();
		return;
	}
	note_windows(FALSE);
}

/* ---------------------------------------------------------------- 接続 */

/* The partitions of TSFS that are not attached, and what is on the others, offered in a panel */
EXPORT void dt_disk_connect( DTWIN *d, CONST T_VOBJ *v )
{
	UB		*names, (*rows)[WM_LABEL_MAX];
	UINT		*kinds;
	T_FSPROBE	*pr;
	T_WMPANEL	*def;
	T_WMPART	*pt;
	INT		n, i, k, m = 0, pid, w = 480, h, at = -1;
	UINT		ans;
	UB		name[OB_VOL_LABEL + 16];

	if ( d == NULL ) {
		d = dt_first_win();
	}
	if ( d == NULL ) {
		return;
	}
	names = (UB *)Kmalloc((SZ)OB_DV_NAME * DK_DISKS);
	rows = (UB (*)[WM_LABEL_MAX])Kmalloc(sizeof(UB) * WM_LABEL_MAX * DK_ROWS);
	kinds = (UINT *)Kmalloc(sizeof(UINT) * DK_ROWS);
	pr = (T_FSPROBE *)Kmalloc(sizeof(T_FSPROBE));
	if ( names == NULL || rows == NULL || kinds == NULL || pr == NULL ) {
		goto done;
	}
	n = knl_obdev_disks(names, DK_DISKS);
	for ( i = 0; i < n && m < DK_ROWS; i++ ) {
		CONST char	*dev = (CONST char *)( names + i * OB_DV_NAME );

		if ( knl_obfile_vol_path(dev) > 0 || knl_fs_probe(dev, pr) < E_OK
		  || pr->kind == FS_KIND_PARTED || pr->kind == FS_KIND_NONE ) {
			continue;
		}
		k = put(rows[m], 0, WM_LABEL_MAX, dev);
		k = put(rows[m], k, WM_LABEL_MAX, "  ");
		k = put(rows[m], k, WM_LABEL_MAX, (CONST char *)pr->name);
		if ( pr->label[0] != 0 ) {
			k = put(rows[m], k, WM_LABEL_MAX, "「");
			k = put(rows[m], k, WM_LABEL_MAX, (CONST char *)pr->label);
			k = put(rows[m], k, WM_LABEL_MAX, "」");
		}
		k = put(rows[m], k, WM_LABEL_MAX, "  ");
		(void)put_size(rows[m], k, WM_LABEL_MAX, pr->bytes);
		kinds[m] = pr->kind;
		/* a 虚身 chosen: its disk, when it is in */
		if ( at < 0 && v != NULL && pr->kind == FS_KIND_TSFS && !zero_uuid(&pr->top)
		  && ts_uuid_cmp(&pr->top, &v->target) == 0 ) {
			at = m;
		}
		/* the names are kept beside the rows, for after the panel */
		knl_memcpy(names + m * OB_DV_NAME, dev, OB_DV_NAME);
		m++;
	}
	tm_printf((UB *)"disk: connect offers %d\n", m);
	if ( m == 0 ) {
		dt_tell(d, "接続できるディスクがありません", NULL);
		goto done;
	}
	if ( at < 0 ) {
		for ( i = 0; i < m && at < 0; i++ ) {
			if ( kinds[i] == FS_KIND_TSFS ) at = i;
		}
	}
	h = 96 + 26 * m;
	def = dt_pn_new(w, h);
	if ( def == NULL ) {
		goto done;
	}
	dt_pn_add(def, WM_PT_LABEL, 0, 14, 4, w - 28, 22, "接続するディスク:");
	for ( i = 0; i < m; i++ ) {
		pt = dt_pn_add(def, WM_PT_CHOICE, PN_DISK + i, 24, 30 + 26 * i, w - 48, 22,
			       (CONST char *)rows[i]);
		if ( pt != NULL ) {
			pt->group = PN_DISK;
			pt->value = ( i == ( at < 0 ? 0 : at ) ) ? 1 : 0;
		}
	}
	dt_pn_buttons(def, "接続");
	ans = dt_pn_run(d, def, &pid);
	k = -1;
	if ( ans == WM_ANS_OK && pid >= 0 ) {
		for ( i = 0; i < m; i++ ) {
			if ( dt_pn_value(pid, PN_DISK + i) != 0 ) k = i;
		}
	}
	dt_pn_done(d, pid);
	wm_update();
	if ( k < 0 ) {
		tm_printf((UB *)"disk: connect cancelled\n");
		goto done;
	}
	{
		CONST char	*dev = (CONST char *)( names + k * OB_DV_NAME );
		ER		er;

		tm_printf((UB *)"disk: connect %s\n", dev);
		if ( kinds[k] == FS_KIND_FAT12 || kinds[k] == FS_KIND_FAT16 || kinds[k] == FS_KIND_FAT32
		  || kinds[k] == FS_KIND_BTRON ) {
			/* files, not objects: ファイル変換 reads them */
			tm_printf((UB *)"disk: %s is files, not objects\n", dev);
			dt_tell(d, "実身のディスクではありません。", "ファイル変換で開いてください");
			goto done;
		}
		if ( kinds[k] != FS_KIND_TSFS ) {
			tm_printf((UB *)"disk: %s is of a kind not handled\n", dev);
			dt_tell(d, "この形式は扱えません", NULL);
			goto done;
		}
		er = attach_dev(dev, TRUE, name, sizeof(name));
		if ( er == E_OBJ ) {
			tm_printf((UB *)"disk: %s is attached already\n", dev);
			dt_tell(d, "同じディスクがすでに接続されています", NULL);
		} else if ( er < E_OK ) {
			dt_tell(d, "ディスクを接続できませんでした", NULL);
		} else {
			UB	msg[OB_VOL_LABEL + 48];

			k = put(msg, 0, sizeof(msg), "ディスク「");
			k = put(msg, k, sizeof(msg), (CONST char *)name);
			(void)put(msg, k, sizeof(msg), "」を接続しました");
			(void)wm_msg_put((CONST char *)msg);
		}
	}

    done:
	if ( names != NULL ) Kfree(names);
	if ( rows != NULL ) Kfree(rows);
	if ( kinds != NULL ) Kfree(kinds);
	if ( pr != NULL ) Kfree(pr);
	wm_update();
}

/* ---------------------------------------------------------------- 切り離し */

/* Whether the volume of a link's object is one that may be taken away: grey in the menu otherwise */
EXPORT BOOL dt_disk_detachable( CONST T_VOBJ *v )
{
	UB		path[DK_PATH];
	T_OBFVREF	ref;

	return (BOOL)( v != NULL && knl_obfile_path_of(&v->target, path, sizeof(path)) >= E_OK
		       && knl_obfile_vol_ref((CONST char *)path, &ref) >= E_OK && !ref.system );
}

/*
 * A volume taken away: not while an object of it is shown in a window
 * or used by a program, nor the system's. 'what' is the word the
 * messages use (切り離し, 取り外し), 'fail' what is said when it
 * cannot go for another reason. Answers E_OK when it went.
 */
LOCAL ER detach_vol( DTWIN *d, CONST char *path, CONST UB *name, CONST char *what,
		      CONST char *fail )
{
	UB	msg[OB_VOL_LABEL + 96], how[64];
	INT	i, k;
	ER	er;

	/* an object of it shown in a window keeps it */
	for ( i = 0; i < DT_MAX_WIN; i++ ) {
		DTWIN	*w = dt_win_at(i);

		if ( w != NULL && w->used && !w->sealed && w->tool == DT_TOOL_NONE
		  && knl_obfile_on(&w->id, path) ) {
			k = put(msg, 0, sizeof(msg), "ディスク「");
			k = put(msg, k, sizeof(msg), (CONST char *)name);
			(void)put(msg, k, sizeof(msg), "」の実身が開いています。");
			k = put(how, 0, sizeof(how), "閉じてから");
			k = put(how, k, sizeof(how), what);
			(void)put(how, k, sizeof(how), "てください");
			tm_printf((UB *)"disk: detach %s refused: a window is open\n", path);
			dt_tell(d, (CONST char *)msg, (CONST char *)how);
			return E_BUSY;
		}
	}
	er = ob_det_vol(path);
	if ( er == E_BUSY ) {
		k = put(msg, 0, sizeof(msg), "ディスク「");
		k = put(msg, k, sizeof(msg), (CONST char *)name);
		(void)put(msg, k, sizeof(msg), "」の実身を使っているプログラムがあります。");
		k = put(how, 0, sizeof(how), "終わってから");
		k = put(how, k, sizeof(how), what);
		(void)put(how, k, sizeof(how), "てください");
		tm_printf((UB *)"disk: detach %s refused: in use\n", path);
		dt_tell(d, (CONST char *)msg, (CONST char *)how);
		return er;
	}
	if ( er < E_OK ) {
		tm_printf((UB *)"disk: detach %s failed %d\n", path, (INT)er);
		dt_tell(d, fail, NULL);
		return er;
	}
	tm_printf((UB *)"disk: detached %s\n", path);
	om_store_recheck();
	dk_volser = knl_obfile_serial();
	note_windows(TRUE);
	dt_draw_all();
	return E_OK;
}

EXPORT void dt_disk_detach( DTWIN *d, CONST T_VOBJ *v )
{
	UB		path[DK_PATH], name[OB_VOL_LABEL + 16], msg[OB_VOL_LABEL + 96];
	T_OBFVREF	ref;
	INT		k;

	if ( v == NULL || knl_obfile_path_of(&v->target, path, sizeof(path)) < E_OK
	  || knl_obfile_vol_ref((CONST char *)path, &ref) < E_OK ) {
		dt_tell(d, "そのディスクは接続されていません", NULL);
		return;
	}
	disk_name(ref.label, (CONST char *)path, name, sizeof(name));
	if ( ref.system ) {
		dt_tell(d, "システムのディスクは切り離せません", NULL);
		return;
	}
	if ( detach_vol(d, (CONST char *)path, name, "切り離し", "ディスクを切り離せませんでした") < E_OK ) {
		return;
	}
	if ( ref.native ) {
		keep_vol(&ref.vuuid, &ref.top, FALSE);	/* not attached again by itself */
	}
	k = put(msg, 0, sizeof(msg), "ディスク「");
	k = put(msg, k, sizeof(msg), (CONST char *)name);
	(void)put(msg, k, sizeof(msg), "」を切り離しました。取り出せます");
	(void)wm_msg_put((CONST char *)msg);
}

/* ---------------------------------------------------------------- 取り外し */

/*
 * A link about to be deleted from the first window. When it is a
 * disk's link -- the top object of a volume attached, or of one
 * remembered as attached -- deleting it takes the disk away (取り外し):
 * the volume is detached and forgotten, so that it is not attached again
 * at the next start. FALSE when the volume cannot be taken away now; the
 * link is then not deleted.
 */
EXPORT BOOL dt_disk_on_delete( DTWIN *d, CONST T_VOBJ *v )
{
	UB		path[DK_PATH], name[OB_VOL_LABEL + 16], msg[OB_VOL_LABEL + 96];
	T_OBFVREF	ref;
	TS_UUID		*kv;
	INT		nk = 0, i, k;

	if ( v == NULL || d == NULL || d != dt_first_win() ) {
		return TRUE;
	}
	if ( knl_obfile_path_of(&v->target, path, sizeof(path)) >= E_OK ) {
		if ( knl_obfile_vol_ref((CONST char *)path, &ref) < E_OK || ref.system || !ref.native
		  || ts_uuid_cmp(&ref.top, &v->target) != 0 ) {
			return TRUE;		/* an object's link, not a disk's */
		}
		disk_name(ref.label, (CONST char *)path, name, sizeof(name));
		if ( detach_vol(d, (CONST char *)path, name, "取り外し", "ディスクを取り外せませんでした") < E_OK ) {
			return FALSE;
		}
		keep_vol(&ref.vuuid, &ref.top, FALSE);
		k = put(msg, 0, sizeof(msg), "ディスク「");
		k = put(msg, k, sizeof(msg), (CONST char *)name);
		(void)put(msg, k, sizeof(msg), "」を取り外しました。取り出せます");
		(void)wm_msg_put((CONST char *)msg);
		tm_printf((UB *)"disk: removed %s\n", (CONST char *)path);
		return TRUE;
	}
	/* the link of a disk not attached now: forgotten as well */
	kv = kept_vols(&nk);
	for ( i = 0; kv != NULL && i < nk; i++ ) {
		if ( ts_uuid_cmp(&kv[DK_KEEP + i], &v->target) == 0
		  || ( !zero_uuid(&v->tvol) && ts_uuid_cmp(&kv[i], &v->tvol) == 0 ) ) {
			keep_vol(&kv[i], NULL, FALSE);
			tm_printf((UB *)"disk: removed a disk not attached\n");
			break;
		}
	}
	if ( kv != NULL ) Kfree(kv);
	return TRUE;
}

/* ---------------------------------------------------------------- 状態表示 */

/* How full the disk of a link's object is (the system's, away from a link) */
EXPORT void dt_disk_status( DTWIN *d, CONST T_VOBJ *v )
{
	T_OBVOL		vol;
	T_OBFVREF	ref;
	T_WMPANEL	*def;
	UB		path[DK_PATH], name[OB_VOL_LABEL + 16], t[WM_LABEL_MAX];
	CONST char	*lab[6] = { "全体容量:", "使用容量:", "空き容量:", "使用比率:",
				    "使用実身数:", "くず実身数:" };
	UD		total, used;
	INT		gc = 0, i, k, pid, w = 360, h = 230;
	ID		tv;

	if ( d == NULL ) {
		return;
	}
	if ( ob_ref_vol(( v != NULL ) ? &v->target : NULL, &vol) < E_OK ) {
		dt_tell(d, "その実身のディスクは接続されていません", NULL);
		return;
	}
	tv = ( v != NULL ) ? knl_obfile_vol_of(&v->target) : knl_obfile_first_vol();
	if ( tv > 0 ) {
		TS_UUID	buf[32], from;
		INT	got;

		/* the garbage, counted a piece at a time */
		for (;;) {
			got = 0;
			if ( ts_lst_gc(tv, ( gc == 0 ) ? NULL : &from, buf, 32, &got) < E_OK || got <= 0 ) {
				break;
			}
			gc += got;
			from = buf[got - 1];
			if ( got < 32 ) break;
		}
	}
	name[0] = 0;
	if ( ( v != NULL && knl_obfile_path_of(&v->target, path, sizeof(path)) >= E_OK )
	  || ( v == NULL && knl_obfile_vols(path, sizeof(path), 1) == 1 ) ) {
		if ( knl_obfile_vol_ref((CONST char *)path, &ref) >= E_OK ) {
			disk_name(ref.label, (CONST char *)path, name, sizeof(name));
		}
	}
	total = vol.blocks * vol.bsize;
	used = ( vol.blocks - vol.bfree ) * vol.bsize;
	tm_printf((UB *)"disk: status \"%s\" blocks %d free %d objects %d garbage %d\n",
		  (CONST char *)name, (INT)vol.blocks, (INT)vol.bfree, vol.nobj, gc);
	def = dt_pn_new(w, h);
	if ( def == NULL ) {
		return;
	}
	k = put(t, 0, sizeof(t), "ディスク状態: ");
	(void)put(t, k, sizeof(t), (CONST char *)name);
	dt_pn_add(def, WM_PT_LABEL, 0, 14, 4, w - 28, 22, (CONST char *)t);
	for ( i = 0; i < 6; i++ ) {
		dt_pn_add(def, WM_PT_LABEL, 0, 24, 32 + 24 * i, 120, 22, lab[i]);
		switch ( i ) {
		case 0:	(void)put_size(t, 0, sizeof(t), total); break;
		case 1:	(void)put_size(t, 0, sizeof(t), used); break;
		case 2:	(void)put_size(t, 0, sizeof(t), total - used); break;
		case 3:	k = put_num(t, 0, sizeof(t), ( total > 0 ) ? used * 100 / total : 0);
			(void)put(t, k, sizeof(t), "%"); break;
		case 4:	(void)put_num(t, 0, sizeof(t), (UD)vol.nobj); break;
		default: (void)put_num(t, 0, sizeof(t), (UD)gc); break;
		}
		dt_pn_add(def, WM_PT_LABEL, 0, 150, 32 + 24 * i, w - 170, 22, (CONST char *)t);
	}
	{
		T_WMPART	*pt = dt_pn_add(def, WM_PT_BUTTON | P_EMPHAS, DT_PN_OK, w - 116, h - 36, 100, 26, "終了");

		if ( pt != NULL ) pt->answer = WM_ANS_OK;
	}
	(void)dt_pn_run(d, def, &pid);
	dt_pn_done(d, pid);
	wm_update();
}

/* ---------------------------------------------------------------- the top of a volume */

/*
 * The object a link to something saved on a volume goes in: the
 * volume's top object, or the first window for the system's volume and
 * one that has none.
 */
EXPORT BOOL dt_disk_top( CONST char *path, TS_UUID *p_top )
{
	T_OBFVREF	ref;

	if ( path == NULL || knl_obfile_vol_ref(path, &ref) < E_OK || ref.system
	  || zero_uuid(&ref.top) || knl_obfile_vol_of(&ref.top) <= 0 ) {
		return FALSE;
	}
	*p_top = ref.top;
	return TRUE;
}

/* ---------------------------------------------------------------- a link whose object is not there */

/*
 * A link opened whose object cannot be found (a 虚身): which volume it is
 * on, as its id says (design 8.3), is told, and TRUE answered; FALSE for
 * a link whose object is there, to be opened as ever. A link to an object
 * of another machine is not opened from here.
 */
EXPORT BOOL dt_disk_absent( DTWIN *d, CONST T_VOBJ *v )
{
	UB	name[OB_VOL_LABEL + 16], msg[OB_VOL_LABEL + 96];
	TS_UUID	mine;
	INT	k;

	if ( v == NULL ) {
		return FALSE;
	}
	if ( d == NULL ) {
		d = dt_first_win();
	}
	if ( !zero_uuid(&v->tmach) && ( knl_obfile_machine(&mine) < E_OK || ts_uuid_cmp(&mine, &v->tmach) != 0 ) ) {
		tm_printf((UB *)"disk: absent: another machine's\n");
		dt_tell(d, "ほかのマシンの実身です。", "このマシンからは開けません");
		return TRUE;
	}
	if ( dt_reachable(v) ) {
		return FALSE;
	}
	if ( !zero_uuid(&v->tvol) && !knl_obfile_vuuid_on(&v->tvol) ) {
		/* the name the volume had when it was attached; else what the link calls it */
		if ( knl_obfile_vol_name(&v->tvol, name, sizeof(name)) < E_OK ) {
			char	us[TS_UUID_STRLEN + 1];

			(void)ts_uuid_to_str(&v->tvol, us, sizeof(us));
			us[8] = 0;
			k = put(name, 0, sizeof(name), us);
			if ( v->name[0] != 0 ) {
				k = put(name, k, sizeof(name), "、");
				(void)put(name, k, sizeof(name), (CONST char *)v->name);
			}
		}
		k = put(msg, 0, sizeof(msg), "ボリューム「");
		k = put(msg, k, sizeof(msg), (CONST char *)name);
		(void)put(msg, k, sizeof(msg), "」が接続されていません");
		tm_printf((UB *)"disk: absent: volume \"%s\" not attached\n", (CONST char *)name);
		dt_tell(d, (CONST char *)msg, "ディスク操作の接続で接続してください");
		return TRUE;
	}
	tm_printf((UB *)"disk: absent: not found\n");
	dt_tell(d, "実身が見つかりません。", "そのボリュームが接続されていないか、実身が削除されています");
	return TRUE;
}
