/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	obdev.c
 *	The device manager: devices as objects (design 18.7)
 *
 *	Every device the device management knows, and each subunit of one,
 *	is an object; so is the clock, a device this manager makes itself.
 *	Record 0 describes the device (xmlTAD made when it is read); record
 *	1 is its data -- blocks of a disk at byte positions, the bytes of a
 *	stream, the time of the clock. A serial port has two more: record 2
 *	its speed (UW, bits per second) and record 3 what it is (T_SERINFO).
 *
 *	A device's UUID is made (v7) the first time the device is seen and
 *	kept in the device box, an object of fixed UUID on the first volume,
 *	so that a virtual object of a device still finds it after the next
 *	start. Until a volume is there the names and UUIDs are kept here
 *	only, and written out when one comes.
 *
 *	A disk's volume is mounted through its object (fs_attach_dev): the
 *	key's rights on the blocks are the permission, the mount point is
 *	/media and the device's name, and the mount shows in the object's
 *	attributes (tessronos.mount) from the file layer's table, told with
 *	OB_E_CHANGE when it comes and goes. When a medium or a device goes,
 *	the mounts on it go with it.
 *
 *	A key that may only read the attributes -- enough to be told of
 *	what happens to the device -- does not open the device: only one
 *	that may read or write its records does. The device management
 *	has room for few devices open at a time (CNF_MAX_OPNDEV), and a
 *	program watching every disk would take it all.
 *
 *	The system itself is a virtual device of this manager as well
 *	(ob_uuid_system): its records are made and taken in obsys.c.
 *
 *	The list of the devices is an object too, virtual like the clock
 *	(ob_uuid_devlist). Its record 0 holds a link to each device there
 *	is, and whatever is told of any device -- that it came or went,
 *	its medium was put in or taken out, a volume on it was mounted or
 *	taken off -- is told to those who watch the list as well, the
 *	notice naming the device. One request watches them all, those
 *	that come later included.
 *
 *	Some devices are not in the device management: the keyboards and
 *	pointers and the input they feed, the screen, the banks of pins,
 *	each device on the USB bus, the random source. Whoever serves one
 *	adds it here by name with the calls that make its records
 *	(knl_obdev_add, T_OBDVOPS); it is then an object like the others,
 *	kept in the device box under its name, listed, protected by the
 *	mode it gives, and told of when it comes and goes.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <ts/dt.h>
#include <ts/tsfs.h>
#include <ts/ser.h>
#include <ts/blk.h>
#include <ts/fs.h>
#include "obj.h"

#define OBD_MAX		2048		/* devices and subunits known: every disk's partitions among them */
#define OBD_HND_MAX	1024		/* devices open at once through their objects */
#define OBD_LIST_MAX	( OBD_MAX * 80 )	/* bytes of the list's record 0: a link to each */
#define ENT_CLOCK	( -1 )		/* OBDHND.ent: the clock */
#define ENT_LIST	( -2 )		/* the list of the devices */
#define ENT_SYSTEM	( -3 )		/* the system (obsys.c) */
#define OBD_SYS_MAX	4096		/* bytes of one of the system's records */
#define OBD_NAME	OB_DV_NAME
#define OBD_TEXT_MAX	512
#define OBD_DESC_MAX	4096		/* bytes of record 0 of a device added by name */

typedef struct {
	BOOL	used;
	UB	name[OBD_NAME];
	TS_UUID	uuid;
	BOOL	kept;			/* written in the device box */
	BOOL	seen;			/* the device management has it now, or it was added */
	CONST T_OBDVOPS *ops;		/* added by name: its records; NULL a registered device */
	void	*ctx;
	BOOL	fixed;			/* its UUID is a fixed one, not the box's */
} OBDENT;

typedef struct {
	BOOL	used;
	INT	ent;			/* ENT_CLOCK, ENT_LIST, or the device */
	ID	dd;			/* 0: not opened, the key reads no record */
	W	blksz;
	BOOL	sound;			/* a sound device: records are its data numbers */
	BOOL	camera;			/* a camera: records are its data numbers */
	BOOL	serial;			/* a serial port: records 2 and 3 are its attributes */
	CONST T_OBDVOPS *vops;		/* a device added by name */
	void	*vctx;
	UD	pos;			/* the key's own place in a stream */
} OBDHND;

/* A serial port's records and the data numbers they are */
#define OB_SER_NREC	4
LOCAL CONST W ser_dn[OB_SER_NREC] = { 0, 0, TDN_SER_SPEED, TDN_SER_INFO };

/*
 * A sound device's records and the data numbers they are (OB_SND_*):
 * record 1 is the PCM, the rest its attributes.
 */
LOCAL CONST W snd_dn[OB_SND_NREC] = {
	0,				/* record 0 is the description, not the device's */
	0,				/* PCM */
	-110, -111, -112, -113, -114, -115,	/* info, mode, control, count, end count, buffer */
	-140,				/* status */
	-120, -121,			/* input and output selectors */
	-122, -123, -124, -125, -126, -127, -128, -129, -130, -131, -132, -133, -134,
	-141, -142,			/* the outputs, and the one chosen */
	-143, -144			/* the mixer, and a volume set */
};

/* A camera's records and the data numbers they are (OB_CAM_*, include/ts/cam.h) */
LOCAL CONST W cam_dn[OB_CAM_NREC] = { 0, 0, -1, -2, -3, -4, -5 };

#define TDK_KIND(atr)	( (atr) & 0x00FFU )
#define TDK_SOUND_KIND	0x0080U		/* include/ts/snd.h: TDK_SOUND */
#define TDK_CAMERA_KIND	0x00A0U		/* include/ts/cam.h: TDK_CAMERA */

/* Whether a device is a sound device, from the kind it registered as */
LOCAL BOOL dev_is_sound( CONST UB *name )
{
	T_RDEV	rd;

	return (BOOL)( tk_ref_dev(name, &rd) >= E_OK && TDK_KIND(rd.devatr) == TDK_SOUND_KIND );
}

LOCAL BOOL dev_is_camera( CONST UB *name )
{
	T_RDEV	rd;

	return (BOOL)( tk_ref_dev(name, &rd) >= E_OK && TDK_KIND(rd.devatr) == TDK_CAMERA_KIND );
}

LOCAL BOOL dev_is_serial( CONST UB *name )
{
	T_RDEV	rd;

	return (BOOL)( tk_ref_dev(name, &rd) >= E_OK && TDK_KIND(rd.devatr) == TDK_SERIAL );
}

LOCAL OBDENT	obd[OBD_MAX];
LOCAL INT	obd_hi = 0;		/* one past the last entry ever used: the loops go no further */
LOCAL OBDHND	obd_hnd[OBD_HND_MAX];
LOCAL ID	obd_mtx = 0;
LOCAL BOOL	obd_loaded = FALSE;	/* the device box has been read */
LOCAL UW	obd_serial = 0;		/* each time a device or a medium came or went */

#define LOCK()		tk_loc_mtx(obd_mtx, TMO_FEVR)
#define UNLOCK()	tk_unl_mtx(obd_mtx)

LOCAL BOOL same_name( CONST UB *a, CONST UB *b )
{
	INT	i;

	for ( i = 0; a[i] != 0 && b[i] != 0; i++ ) {
		if ( a[i] != b[i] ) return FALSE;
	}
	return (BOOL)( a[i] == b[i] );
}

LOCAL BOOL is_clock( CONST TS_UUID *u )
{
	return (BOOL)( ts_uuid_cmp(u, &ob_uuid_clock) == 0 );
}

LOCAL BOOL is_list( CONST TS_UUID *u )
{
	return (BOOL)( ts_uuid_cmp(u, &ob_uuid_devlist) == 0 );
}

LOCAL BOOL is_system( CONST TS_UUID *u )
{
	return (BOOL)( ts_uuid_cmp(u, &ob_uuid_system) == 0 );
}

/* Something happened on a device: told on it, and on the list of the devices */
LOCAL void dev_post( CONST TS_UUID *u, UINT event )
{
	knl_ob_post(u, -1, event, NULL);
	knl_ob_post_as(&ob_uuid_devlist, u, -1, event, NULL);
}

LOCAL INT ent_by_name( CONST UB *name )
{
	INT	i;

	for ( i = 0; i < obd_hi; i++ ) {
		if ( obd[i].used && same_name(obd[i].name, name) ) return i;
	}
	return -1;
}

LOCAL INT ent_by_uuid( CONST TS_UUID *u )
{
	INT	i;

	for ( i = 0; i < obd_hi; i++ ) {
		if ( obd[i].used && obd[i].seen && ts_uuid_cmp(&obd[i].uuid, u) == 0 ) return i;
	}
	return -1;
}

LOCAL INT ent_new( CONST UB *name )
{
	INT	i, k;

	for ( i = 0; i < OBD_MAX; i++ ) {
		if ( !obd[i].used ) {
			knl_memset(&obd[i], 0, sizeof(OBDENT));
			if ( i >= obd_hi ) obd_hi = i + 1;
			for ( k = 0; name[k] != 0 && k < OBD_NAME - 1; k++ ) {
				obd[i].name[k] = name[k];
			}
			obd[i].used = TRUE;
			return i;
		}
	}
	return -1;
}

/* ---------------------------------------------------------------- the device box */

/* The names and UUIDs the box holds, taken in (the box's win over ours) */
LOCAL void box_load( ID vol )
{
	UB	*j;
	INT	len = 0, arr, pos, e;

	j = (UB *)Kmalloc(OB_META_MAX);
	if ( j == NULL ) {
		return;
	}
	if ( knl_tsfs_get_meta(vol, &ob_uuid_devbox, j, OB_META_MAX, &len) >= E_OK ) {
		arr = knl_oj_path(j, len, "tessronos", "devices");
		pos = arr;
		while ( arr >= 0 && ( e = knl_oj_next(j, len, arr, &pos) ) >= 0 ) {
			UB	name[OBD_NAME], us[TS_UUID_STRLEN + 1];
			TS_UUID	u;
			INT	i;

			if ( j[e] != '{'
			  || knl_oj_str(j, len, knl_oj_member(j, len, e, "name"), name, sizeof(name)) < 0
			  || knl_oj_str(j, len, knl_oj_member(j, len, e, "uuid"), us, sizeof(us)) < 0
			  || ts_str_to_uuid((CONST char *)us, &u) < E_OK ) {
				continue;
			}
			i = ent_by_name(name);
			if ( i < 0 ) {
				i = ent_new(name);
			}
			if ( i >= 0 && !obd[i].fixed ) {
				obd[i].uuid = u;
				obd[i].kept = TRUE;
			}
		}
	}
	Kfree(j);
}

/* Whether metadata read from the store says nothing refers to the object */
LOCAL BOOL refs_none( CONST UB *j, INT len )
{
	LOCAL CONST char	key[] = "\"refCount\":0";
	INT			i, k;

	for ( i = 0; i + (INT)sizeof(key) - 1 <= len; i++ ) {
		for ( k = 0; key[k] != 0 && j[i + k] == (UB)key[k]; k++ ) ;
		if ( key[k] == 0 ) {
			return (BOOL)( i + k >= len || j[i + k] < '0' || j[i + k] > '9' );
		}
	}
	return FALSE;
}

/* The box written again with every name and UUID known */
LOCAL void box_save( ID vol )
{
	UB	*j, *t;
	INT	len = 0, n = 0, i, k;
	BOOL	first = TRUE, fresh = FALSE;
	ER	er;

	j = (UB *)Kmalloc(OB_META_MAX);
	t = (UB *)Kmalloc(OB_META_MAX);
	if ( j == NULL || t == NULL ) {
		if ( j != NULL ) Kfree(j);
		if ( t != NULL ) Kfree(t);
		return;
	}
	if ( knl_tsfs_get_meta(vol, &ob_uuid_devbox, j, OB_META_MAX, &len) < E_OK ) {
		/* a new box: referred to by the system, so never garbage */
		len = knl_oj_put(j, 0, OB_META_MAX,
			"{\"name\":\"デバイス箱\",\"refCount\":1,\"recordCount\":0,"
			"\"editable\":true,\"deletable\":false,\"readable\":true}");
		fresh = TRUE;
	}
	/*
	 * Every device known, the ones there now first; when that is more
	 * than the box holds, those that are not there are left out (they
	 * are given UUIDs again when they come back).
	 */
	n = knl_oj_put(t, n, OB_META_MAX, "[");
	for ( k = 0; k < 2; k++ ) {
		for ( i = 0; i < obd_hi && n >= 0; i++ ) {
			INT	at = n;

			if ( !obd[i].used || obd[i].fixed || obd[i].seen != ( k == 0 ) ) continue;
			n = knl_oj_put(t, n, OB_META_MAX - 1, first ? "{\"name\":" : ",{\"name\":");
			n = knl_oj_put_str(t, n, OB_META_MAX - 1, obd[i].name);
			n = knl_oj_put(t, n, OB_META_MAX - 1, ",\"uuid\":");
			n = knl_oj_put_uuid(t, n, OB_META_MAX - 1, &obd[i].uuid);
			n = knl_oj_put(t, n, OB_META_MAX - 1, "}");
			/* room is kept for the rest of the metadata */
			if ( n < 0 || n > OB_META_MAX - 1024 ) {
				n = at;
				i = obd_hi;
				k = 2;
				break;
			}
			first = FALSE;
		}
	}
	n = knl_oj_put(t, n, OB_META_MAX, "]");
	if ( len > 0 && n > 0 ) {
		len = knl_oj_set_path(j, len, OB_META_MAX, "tessronos", "devices", t, n);
		/* the native store makes no object by writing metadata: a new box is made under its UUID */
		er = ( len <= 0 ) ? E_PAR
		   : fresh ? knl_tsfs_cre_as(vol, &ob_uuid_devbox, j, len)
		   : knl_tsfs_set_meta(vol, &ob_uuid_devbox, j, len);
		if ( er >= E_OK ) {
			for ( i = 0; i < obd_hi; i++ ) {
				if ( obd[i].used ) obd[i].kept = TRUE;
			}
			/*
			 * The store keeps the count of references itself and takes
			 * nothing from the metadata's; no link reaches the box, so
			 * the system holds it once (a box made before it did is
			 * held when it is next written)
			 */
			if ( fresh || refs_none(j, len) ) {
				(void)ts_lnk_obj(vol, &ob_uuid_devbox);
			}
		}
	}
	Kfree(j);
	Kfree(t);
}

EXPORT void knl_obdev_volume_up( void )
{
	ID	vol = knl_obfile_first_vol();

	if ( vol <= 0 || obd_mtx <= 0 ) {
		return;
	}
	LOCK();
	if ( !obd_loaded ) {
		box_load(vol);
		obd_loaded = TRUE;
	}
	box_save(vol);
	UNLOCK();
}

/* ---------------------------------------------------------------- the devices there are */

/* The device management asked again: new devices get a UUID, gone ones are not seen */
LOCAL void refresh( void )
{
	T_LDEV	ld[8];
	INT	start = 0, i, k, s;
	BOOL	fresh = FALSE;
	ID	vol;

	for ( i = 0; i < obd_hi; i++ ) {
		if ( obd[i].ops == NULL ) obd[i].seen = FALSE;	/* one added by name stays */
	}
	for (;;) {
		INT	n = tk_lst_dev(ld, start, 8);
		INT	got = ( n > 8 ) ? 8 : n;

		if ( n <= 0 ) break;
		for ( k = 0; k < got; k++ ) {
			for ( s = -1; s < ld[k].nsub; s++ ) {
				UB	name[OBD_NAME];
				INT	m = 0, e;

				while ( ld[k].devnm[m] != 0 && m < L_DEVNM ) {
					name[m] = ld[k].devnm[m];
					m++;
				}
				if ( s >= 0 ) {		/* a subunit: the name and its number */
					if ( s >= 100 ) name[m++] = (UB)( '0' + s / 100 );
					if ( s >= 10 ) name[m++] = (UB)( '0' + s / 10 % 10 );
					name[m++] = (UB)( '0' + s % 10 );
				}
				name[m] = 0;
				e = ent_by_name(name);
				if ( e < 0 ) {
					e = ent_new(name);
					if ( e < 0 ) continue;
					if ( ts_gen_uuid(&obd[e].uuid) < E_OK ) {
						obd[e].used = FALSE;	/* no clock: no UUID yet */
						continue;
					}
					fresh = TRUE;
				}
				obd[e].seen = TRUE;
			}
		}
		start += got;
		if ( n <= 8 ) break;
	}
	if ( fresh && obd_loaded && ( vol = knl_obfile_first_vol() ) > 0 ) {
		box_save(vol);
	}
}

/* ---------------------------------------------------------------- what drivers say */

/* Whether a name is the device's own or one of its units' ("uda", "uda0") */
LOCAL BOOL name_under( CONST UB *name, CONST UB *devnm )
{
	INT	i;

	for ( i = 0; devnm[i] != 0; i++ ) {
		if ( name[i] != devnm[i] ) return FALSE;
	}
	for ( ; name[i] != 0; i++ ) {
		if ( name[i] < '0' || name[i] > '9' ) return FALSE;
	}
	return TRUE;
}

/*
 * The medium or the device went: the mounts of it and of its units go
 * too (design 12.2.3), each told on its object as the change of its
 * attributes. Outside the lock: taking a mount down reaches the device.
 */
LOCAL void revoke( CONST UB *devnm )
{
	UB	name[OBD_NAME];
	TS_UUID	u;
	INT	i;
	BOOL	hit;

	for ( i = 0; i < obd_hi; i++ ) {
		LOCK();
		hit = ( obd[i].used && name_under(obd[i].name, devnm) );
		if ( hit ) {
			knl_memcpy(name, obd[i].name, OBD_NAME);
			u = obd[i].uuid;
		}
		UNLOCK();
		if ( hit && knl_fs_revoke((CONST char *)name) > 0 ) {
			dev_post(&u, OB_E_CHANGE);
		}
		if ( hit ) {
			(void)knl_obfile_revoke(name);	/* the volumes of objects on it */
		}
	}
}

EXPORT void knl_obdev_media( CONST UB *devnm, BOOL in )
{
	TS_UUID	u;
	INT	e;

	if ( obd_mtx <= 0 || devnm == NULL ) {
		return;
	}
	LOCK();
	e = ent_by_name(devnm);
	if ( e < 0 ) {
		refresh();			/* one this manager has not met yet */
		e = ent_by_name(devnm);
	}
	if ( e >= 0 ) {
		u = obd[e].uuid;
	}
	obd_serial++;
	UNLOCK();
	if ( e >= 0 ) {
		dev_post(&u, in ? OB_E_ATTACH : OB_E_DETACH);
	}
	if ( !in ) {
		revoke(devnm);
	}
}

EXPORT void knl_obdev_changed( void )
{
	typedef struct {
		TS_UUID	u;
		UINT	ev;
		INT	ent;
	} CHG;
	BOOL	*was;
	CHG	*c;
	INT	i, n = 0;

	if ( obd_mtx <= 0 ) {
		return;
	}
	was = (BOOL *)Kmalloc(sizeof(BOOL) * OBD_MAX);
	c = (CHG *)Kmalloc(sizeof(CHG) * OBD_MAX);
	if ( was == NULL || c == NULL ) {
		if ( was != NULL ) Kfree(was);
		if ( c != NULL ) Kfree(c);
		return;
	}
	LOCK();
	for ( i = 0; i < obd_hi; i++ ) {
		was[i] = ( obd[i].used && obd[i].seen );
	}
	refresh();
	for ( i = 0; i < obd_hi; i++ ) {
		BOOL	now = ( obd[i].used && obd[i].seen );

		if ( now != was[i] ) {
			c[n].u = obd[i].uuid;
			c[n].ev = now ? OB_E_ATTACH : OB_E_DETACH;
			c[n].ent = i;
			n++;
		}
	}
	obd_serial++;
	UNLOCK();
	for ( i = 0; i < n; i++ ) {
		dev_post(&c[i].u, c[i].ev);
		if ( c[i].ev == OB_E_DETACH ) {
			revoke(obd[c[i].ent].name);	/* an entry keeps its name */
		}
	}
	Kfree(was);
	Kfree(c);
}

/* ---------------------------------------------------------------- the volumes kept attached */

/*
 * The volumes of objects to be attached again at the next start, by
 * their own UUIDs with their top objects, kept in the device box's
 * metadata (tessronos.volumes): [{"volume": UUID, "top": UUID}, ...].
 */
#define VOLS_MAX	256

LOCAL INT vols_read( ID vol, UB *j, INT *p_len, TS_UUID *vu, TS_UUID *top, INT max )
{
	INT	len = 0, arr, pos, e, n = 0;

	if ( knl_tsfs_get_meta(vol, &ob_uuid_devbox, j, OB_META_MAX - 1, &len) < E_OK ) {
		*p_len = 0;
		return 0;
	}
	j[len] = 0;
	*p_len = len;
	arr = knl_oj_path(j, len, "tessronos", "volumes");
	pos = arr;
	while ( arr >= 0 && n < max && ( e = knl_oj_next(j, len, arr, &pos) ) >= 0 ) {
		UB	a[TS_UUID_STRLEN + 1], b[TS_UUID_STRLEN + 1];

		if ( j[e] != '{'
		  || knl_oj_str(j, len, knl_oj_member(j, len, e, "volume"), a, sizeof(a)) < 0
		  || ts_str_to_uuid((CONST char *)a, &vu[n]) < E_OK ) {
			continue;
		}
		if ( knl_oj_str(j, len, knl_oj_member(j, len, e, "top"), b, sizeof(b)) < 0
		  || ts_str_to_uuid((CONST char *)b, &top[n]) < E_OK ) {
			knl_memset(&top[n], 0, sizeof(top[n]));
		}
		n++;
	}
	return n;
}

EXPORT INT knl_obdev_vols_get( TS_UUID *vu, TS_UUID *top, INT max )
{
	UB	*j;
	INT	len = 0, n = 0;
	ID	vol = knl_obfile_first_vol();

	if ( vol <= 0 || obd_mtx <= 0 || vu == NULL || top == NULL ) {
		return 0;
	}
	j = (UB *)Kmalloc(OB_META_MAX);
	if ( j == NULL ) {
		return 0;
	}
	LOCK();
	n = vols_read(vol, j, &len, vu, top, max);
	UNLOCK();
	Kfree(j);
	return n;
}

EXPORT ER knl_obdev_vols_put( CONST TS_UUID *vuuid, CONST TS_UUID *top, BOOL keep )
{
	TS_UUID	*vu, *tp;
	UB	*j, *t;
	INT	len = 0, n, i, k = 0;
	ID	vol = knl_obfile_first_vol();
	ER	er = E_OK;

	if ( vol <= 0 || obd_mtx <= 0 || vuuid == NULL ) {
		return E_NOEXS;
	}
	j = (UB *)Kmalloc(OB_META_MAX);
	t = (UB *)Kmalloc(OB_META_MAX);
	vu = (TS_UUID *)Kmalloc(sizeof(TS_UUID) * VOLS_MAX * 2);
	if ( j == NULL || t == NULL || vu == NULL ) {
		if ( j != NULL ) Kfree(j);
		if ( t != NULL ) Kfree(t);
		if ( vu != NULL ) Kfree(vu);
		return E_NOMEM;
	}
	tp = vu + VOLS_MAX;
	LOCK();
	if ( !obd_loaded ) {
		box_load(vol);
		obd_loaded = TRUE;
	}
	box_save(vol);				/* the box is there to keep it in */
	n = vols_read(vol, j, &len, vu, tp, VOLS_MAX - 1);
	for ( i = 0; i < n; i++ ) {
		if ( ts_uuid_cmp(&vu[i], vuuid) != 0 ) {
			vu[k] = vu[i];
			tp[k] = tp[i];
			k++;
		}
	}
	if ( keep ) {
		vu[k] = *vuuid;
		if ( top != NULL ) tp[k] = *top;
		else knl_memset(&tp[k], 0, sizeof(tp[k]));
		k++;
	}
	if ( len <= 0 ) {
		er = E_NOEXS;
	} else {
		INT	m = knl_oj_put(t, 0, OB_META_MAX, "[");

		for ( i = 0; i < k; i++ ) {
			m = knl_oj_put(t, m, OB_META_MAX, ( i > 0 ) ? ",{\"volume\":" : "{\"volume\":");
			m = knl_oj_put_uuid(t, m, OB_META_MAX, &vu[i]);
			m = knl_oj_put(t, m, OB_META_MAX, ",\"top\":");
			m = knl_oj_put_uuid(t, m, OB_META_MAX, &tp[i]);
			m = knl_oj_put(t, m, OB_META_MAX, "}");
		}
		m = knl_oj_put(t, m, OB_META_MAX, "]");
		len = ( m > 0 ) ? knl_oj_set_path(j, len, OB_META_MAX, "tessronos", "volumes", t, m) : -1;
		er = ( len <= 0 ) ? E_LIMIT : knl_tsfs_set_meta(vol, &ob_uuid_devbox, j, len);
	}
	UNLOCK();
	Kfree(j);
	Kfree(t);
	Kfree(vu);
	return er;
}

EXPORT UW knl_obdev_serial( void )
{
	return __atomic_load_n(&obd_serial, __ATOMIC_ACQUIRE);
}

/* The names of the disks and partitions there are now, OBD_NAME bytes each */
EXPORT INT knl_obdev_disks( UB *buf, INT max )
{
	T_RDEV	rd;
	UB	name[OBD_NAME];
	INT	i, n = 0;

	if ( obd_mtx <= 0 || buf == NULL ) {
		return 0;
	}
	for ( i = 0; i < obd_hi && n < max; i++ ) {
		BOOL	take;

		LOCK();
		if ( i == 0 ) refresh();
		take = (BOOL)( obd[i].used && obd[i].seen && obd[i].ops == NULL );
		if ( take ) knl_memcpy(name, obd[i].name, OBD_NAME);
		UNLOCK();
		if ( take && tk_ref_dev(name, &rd) >= E_OK && rd.blksz > 1 && !dev_is_sound(name) ) {
			knl_memcpy(buf + n * OBD_NAME, name, OBD_NAME);
			n++;
		}
	}
	return n;
}

EXPORT ER knl_obdev_uuid( CONST UB *devnm, TS_UUID *p_uuid )
{
	INT	e;

	if ( obd_mtx <= 0 || devnm == NULL || p_uuid == NULL ) {
		return E_NOEXS;
	}
	LOCK();
	e = ent_by_name(devnm);
	if ( e < 0 ) {
		refresh();
		e = ent_by_name(devnm);
	}
	if ( e >= 0 ) {
		*p_uuid = obd[e].uuid;
	}
	UNLOCK();

	return ( e >= 0 ) ? E_OK : E_NOEXS;
}

/* ---------------------------------------------------------------- devices added by name */

/* A UUID without the clock (version 4): the device box keeps it all the same */
LOCAL ER random_uuid( TS_UUID *u )
{
	ER	er = ts_get_random(u->b, sizeof(u->b));

	u->b[6] = (UB)( 0x40 | ( u->b[6] & 0x0F ) );
	u->b[8] = (UB)( 0x80 | ( u->b[8] & 0x3F ) );
	return er;
}

EXPORT ER knl_obdev_add( CONST UB *name, CONST TS_UUID *fixed, CONST T_OBDVOPS *ops, void *ctx,
			 TS_UUID *p_uuid )
{
	TS_UUID	u;
	BOOL	fresh = FALSE, was;
	INT	e;
	ID	vol;

	if ( obd_mtx <= 0 || name == NULL || name[0] == 0 || ops == NULL ) {
		return E_PAR;
	}
	LOCK();
	e = ent_by_name(name);
	if ( e >= 0 && obd[e].ops == NULL && obd[e].seen ) {
		UNLOCK();
		return E_OBJ;			/* a registered device has the name */
	}
	if ( e < 0 ) {
		e = ent_new(name);
		if ( e < 0 ) {
			UNLOCK();
			return E_LIMIT;
		}
		fresh = TRUE;
	}
	if ( fixed != NULL ) {
		obd[e].uuid = *fixed;
		obd[e].fixed = TRUE;
	} else if ( fresh && ts_gen_uuid(&obd[e].uuid) < E_OK && random_uuid(&obd[e].uuid) < E_OK ) {
		obd[e].used = FALSE;
		UNLOCK();
		return E_OBJ;
	}
	was = obd[e].seen;
	obd[e].ops = ops;
	obd[e].ctx = ctx;
	obd[e].seen = TRUE;
	u = obd[e].uuid;
	if ( fresh && fixed == NULL && obd_loaded && ( vol = knl_obfile_first_vol() ) > 0 ) {
		box_save(vol);
	}
	UNLOCK();
	if ( p_uuid != NULL ) *p_uuid = u;
	if ( !was ) {
		dev_post(&u, OB_E_ATTACH);
	}
	return E_OK;
}

EXPORT ER knl_obdev_del( CONST UB *name )
{
	TS_UUID	u;
	BOOL	was = FALSE;
	INT	e;

	if ( obd_mtx <= 0 || name == NULL ) {
		return E_PAR;
	}
	LOCK();
	e = ent_by_name(name);
	if ( e >= 0 && obd[e].ops != NULL ) {
		was = obd[e].seen;
		obd[e].seen = FALSE;		/* the entry and its UUID stay for its return */
		u = obd[e].uuid;
	}
	UNLOCK();
	if ( e < 0 ) {
		return E_NOEXS;
	}
	if ( was ) {
		dev_post(&u, OB_E_DETACH);
	}
	return E_OK;
}

EXPORT void knl_obdev_post( CONST TS_UUID *uuid, INT recno, UINT event, CONST T_OBNTM *n )
{
	knl_ob_post(uuid, recno, event, n);
	knl_ob_post_as(&ob_uuid_devlist, uuid, recno, event, n);
}

EXPORT INT knl_obdev_link( UB *t, INT n, INT max, CONST TS_UUID *u, CONST UB *name )
{
	char	us[TS_UUID_STRLEN + 1];

	(void)ts_uuid_to_str(u, us, sizeof(us));
	n = knl_oj_put(t, n, max, "<p><link id=\"");
	n = knl_oj_put(t, n, max, us);
	n = knl_oj_put(t, n, max, "_0.xtad\"/>");
	n = knl_oj_put(t, n, max, (CONST char *)name);
	return knl_oj_put(t, n, max, "</p>");
}

/* The entry of a key's device added by name, while it is there (under the lock) */
LOCAL BOOL added_there( OBDHND *o )
{
	return (BOOL)( o->vops != NULL && o->ent >= 0 && obd[o->ent].seen && obd[o->ent].ops == o->vops );
}

/* ---------------------------------------------------------------- the manager */

LOCAL ER obd_find( CONST TS_UUID *uuid )
{
	INT	i;

	if ( is_clock(uuid) || is_list(uuid) || is_system(uuid) ) {
		return E_OK;
	}
	LOCK();
	refresh();
	i = ent_by_uuid(uuid);
	UNLOCK();

	return ( i >= 0 ) ? E_OK : E_NOEXS;
}

LOCAL ER obd_ref( CONST TS_UUID *uuid, T_OBREF *r )
{
	T_RDEV	rd;
	INT	i;

	if ( is_clock(uuid) ) {
		(void)knl_oj_put(r->name, 0, OB_NAME_MAX, "clock");
		r->sub = OB_S_CLOCK;
		r->flags = OB_F_VIRTUAL;
		r->nrec = 2;
		return E_OK;
	}
	if ( is_list(uuid) ) {
		(void)knl_oj_put(r->name, 0, OB_NAME_MAX, "devices");
		r->sub = OB_S_DEVLIST;
		r->flags = OB_F_VIRTUAL;
		r->nrec = 1;
		return E_OK;
	}
	if ( is_system(uuid) ) {
		(void)knl_oj_put(r->name, 0, OB_NAME_MAX, "system");
		r->sub = OB_S_SYSTEM;
		r->flags = OB_F_VIRTUAL;
		r->nrec = OB_SYS_NREC;
		return E_OK;
	}
	LOCK();
	i = ent_by_uuid(uuid);
	if ( i >= 0 && obd[i].ops != NULL ) {
		knl_memcpy(r->name, obd[i].name, OBD_NAME);
		r->sub = obd[i].ops->sub;
		r->flags = OB_F_VIRTUAL;
		r->nrec = obd[i].ops->nrec;
	} else if ( i >= 0 ) {
		knl_memcpy(r->name, obd[i].name, OBD_NAME);
		if ( dev_is_sound(obd[i].name) ) {
			r->sub = OB_S_SOUND;
			r->nrec = OB_SND_NREC;
		} else if ( dev_is_camera(obd[i].name) ) {
			r->sub = OB_S_CAMERA;
			r->nrec = OB_CAM_NREC;
		} else {
			r->sub = ( tk_ref_dev(obd[i].name, &rd) >= E_OK && rd.blksz > 1 )
			       ? OB_S_DISK : OB_S_CHAR;
			r->nrec = 2;
		}
	}
	UNLOCK();

	return ( i >= 0 ) ? E_OK : E_NOEXS;
}

/*
 * Devices belong to the system. A disk's blocks are the administrators'
 * alone (rw-r----- with their group); the clock is read by everyone;
 * sound is everyone's to play and record, each opening a channel of
 * its own. The system object is written by the administrators and by
 * the ordinary users' group (its records of the colour scheme, the
 * wallpaper and the power are the only ones that take writing), and so
 * is a medium of the person's (medium_free).
 */

/* Whether name begins with the base, the disk's name without its partition's number */
LOCAL BOOL on_disk( CONST UB *name, CONST UB *base, INT nb )
{
	INT	k;

	for ( k = 0; k < nb; k++ ) {
		if ( name[k] != base[k] ) return FALSE;
	}
	return TRUE;
}

/*
 * A medium the person at the machine may read and write as a whole: a
 * disk of the USB mass storage ("uda", "uda0", a CD-ROM as well), of
 * which the system holds nothing -- no native volume attached (its
 * objects keep their own protection, which the blocks would get round)
 * and nothing mounted but where a program mounts a medium, under
 * FS_MEDIA_DIR. A disk the machine started from is never one.
 */
LOCAL BOOL medium_free( CONST UB *name )
{
	T_FSMNT	*mt;
	UB	*vp;
	INT	nb = 0, n, i;
	BOOL	free = TRUE;
	CONST INT stride = 32;

	if ( name[0] != 'u' || name[1] != 'd' ) {
		return FALSE;
	}
	while ( nb < OBD_NAME && name[nb] != 0 && ( name[nb] < '0' || name[nb] > '9' ) ) nb++;

	n = fs_mounts(NULL, 0);
	if ( n > 0 ) {
		mt = (T_FSMNT *)Kmalloc(sizeof(T_FSMNT) * (SZ)( n + 4 ));
		if ( mt == NULL ) {
			return FALSE;
		}
		n = fs_mounts(mt, n + 4);
		for ( i = 0; i < n && free; i++ ) {
			if ( mt[i].dev[0] != 0 && on_disk(mt[i].dev, name, nb)
			  && !on_disk(mt[i].path, (CONST UB *)FS_MEDIA_DIR "/", (INT)sizeof(FS_MEDIA_DIR)) ) {
				free = FALSE;
			}
		}
		Kfree(mt);
	}
	vp = (UB *)Kmalloc(stride * 64);
	if ( vp == NULL ) {
		return FALSE;
	}
	n = knl_obfile_vols(vp, stride, 64);
	for ( i = 0; i < n && free; i++ ) {
		if ( on_disk(vp + i * stride, name, nb) ) {
			free = FALSE;
		}
	}
	Kfree(vp);
	return free;
}

LOCAL ER obd_prot( CONST TS_UUID *uuid, T_OBPRT *prt )
{
	CONST T_OBDVOPS	*ops = NULL;
	INT	i;
	BOOL	open = FALSE, medium = FALSE;
	UB	name[OBD_NAME];

	name[0] = 0;
	if ( !is_clock(uuid) && !is_list(uuid) && !is_system(uuid) ) {
		LOCK();
		i = ent_by_uuid(uuid);
		if ( i >= 0 ) {
			ops = obd[i].ops;
			knl_memcpy(name, obd[i].name, OBD_NAME);
		}
		UNLOCK();
		if ( ops != NULL ) {
			/* added by name: the mode it gives, and the record only x reaches */
			knl_memset(prt, 0, sizeof(*prt));
			prt->owner = ob_user_system;
			prt->group = ob_group_admin;
			prt->mode = ops->mode;
			prt->attr = OB_A_PERM;
			if ( ops->xrec > 0 ) {
				prt->nrmask = 1;
				prt->rmask[0].recno = ops->xrec;
				prt->rmask[0].ops = ops->xops | OB_OP_EXEC;
			}
			return E_OK;
		}
		/* what a program plays sound, talks on a line or takes pictures with is everyone's */
		open = ( i >= 0 && ( dev_is_sound(name) || dev_is_serial(name) || dev_is_camera(name) ) );
		/* a medium of the person's: read and written by the ordinary users too */
		medium = ( i >= 0 && !open && medium_free(name) );
	}
	knl_memset(prt, 0, sizeof(*prt));
	prt->owner = ob_user_system;
	prt->group = ob_group_admin;
	prt->mode = is_list(uuid) ? 0444 : is_clock(uuid) ? 0644 : is_system(uuid) ? 0664 : open ? 0666 : 0640;
	prt->attr = OB_A_PERM;
	if ( is_system(uuid) ) {
		/* the colour scheme, the wallpaper and the power are the person's at the machine to set */
		prt->nacl = 1;
		prt->acl[0].kind = OB_ACE_GROUP;
		prt->acl[0].who = ob_group_users;
		prt->acl[0].ops = OB_OP_WRITE;
	} else {
		/*
		 * What a device is -- its size, where it is mounted -- is the
		 * ordinary users' to look at; a medium of the person's is
		 * theirs to read and write as well
		 */
		prt->nacl = 1;
		prt->acl[0].kind = OB_ACE_GROUP;
		prt->acl[0].who = ob_group_users;
		prt->acl[0].ops = medium ? ( OB_OP_R | OB_OP_WRITE ) : OB_OP_ATRRD;
	}

	return E_OK;
}

LOCAL INT obd_open( CONST TS_UUID *uuid, UINT ops )
{
	INT	e = ENT_CLOCK, i;
	ID	dd = 0;
	T_RDEV	rd;
	W	blksz = 1;
	BOOL	sound = FALSE, serial = FALSE, camera = FALSE;
	CONST T_OBDVOPS	*vops = NULL;
	void	*vctx = NULL;
	UD	pos = 0;

	if ( is_list(uuid) ) {
		e = ENT_LIST;
	} else if ( is_system(uuid) ) {
		e = ENT_SYSTEM;
	} else if ( !is_clock(uuid) ) {
		LOCK();
		e = ent_by_uuid(uuid);
		vops = ( e >= 0 ) ? obd[e].ops : NULL;
		vctx = ( e >= 0 ) ? obd[e].ctx : NULL;
		UNLOCK();
		if ( e < 0 ) {
			return E_NOEXS;
		}
		if ( vops != NULL ) {
			/* added by name: nothing of the device management to open */
		} else if ( ( ops & ( OB_OP_READ | OB_OP_WRITE ) ) != 0 ) {
			dd = tk_opn_dev(obd[e].name, ( ops & OB_OP_WRITE ) ? TD_UPDATE : TD_READ);
			if ( dd < E_OK ) {
				return (INT)dd;
			}
		}
		if ( vops == NULL && tk_ref_dev(obd[e].name, &rd) >= E_OK && rd.blksz > 0 ) {
			blksz = rd.blksz;
			sound = ( TDK_KIND(rd.devatr) == TDK_SOUND_KIND );
			serial = ( TDK_KIND(rd.devatr) == TDK_SERIAL );
			camera = ( TDK_KIND(rd.devatr) == TDK_CAMERA_KIND );
		}
	}
	if ( vops != NULL && vops->opened != NULL ) {
		vops->opened(vctx, &pos);
	}
	LOCK();
	for ( i = 0; i < OBD_HND_MAX; i++ ) {
		if ( !obd_hnd[i].used ) {
			obd_hnd[i].vops = vops;
			obd_hnd[i].vctx = vctx;
			obd_hnd[i].pos = pos;
			obd_hnd[i].used = TRUE;
			obd_hnd[i].ent = e;
			obd_hnd[i].dd = dd;
			obd_hnd[i].blksz = blksz;
			obd_hnd[i].sound = sound;
			obd_hnd[i].serial = serial;
			obd_hnd[i].camera = camera;
			UNLOCK();
			return i + 1;
		}
	}
	UNLOCK();
	if ( dd > 0 ) tk_cls_dev(dd, 0);

	return E_LIMIT;
}

LOCAL OBDHND *hnd_of( INT h )
{
	return ( h >= 1 && h <= OBD_HND_MAX && obd_hnd[h - 1].used ) ? &obd_hnd[h - 1] : NULL;
}

LOCAL ER obd_close( INT h )
{
	OBDHND	*o = hnd_of(h);

	if ( o == NULL ) {
		return E_ID;
	}
	if ( o->dd > 0 ) tk_cls_dev(o->dd, 0);
	o->used = FALSE;

	return E_OK;
}

/* Record 0: what the device is, as xmlTAD */
LOCAL INT describe( OBDHND *o, UB *t, INT max )
{
	CONST UB	*name = ( o->ent == ENT_SYSTEM ) ? (CONST UB *)"system"
			      : ( o->ent < 0 ) ? (CONST UB *)"clock" : obd[o->ent].name;
	INT		n;

	n = knl_oj_put(t, 0, max, "<tad version=\"1.0\" encoding=\"UTF-8\" filename=\"");
	n = knl_oj_put(t, n, max, (CONST char *)name);
	n = knl_oj_put(t, n, max, "\"><document><p>");
	n = knl_oj_put(t, n, max, (CONST char *)name);
	if ( o->ent == ENT_SYSTEM ) {
		n = knl_oj_put(t, n, max, ": the system (virtual). Records 1 to 9 are key and value "
				   "lines: the system, power (write OFF or RESTART), the screen (画面), the USB "
				   "devices, the colour scheme (write its number), the wallpaper (write its "
				   "object), the network, sound (write SELECT and an output's id), the hardware's state");
	} else if ( o->ent < 0 ) {
		n = knl_oj_put(t, n, max, ": clock (virtual)");
	} else if ( o->sound ) {
		n = knl_oj_put(t, n, max, ": sound. Record 1 is the PCM (written, it plays; read, "
				   "it gives what was recorded); records 2 on are its attributes");
	} else if ( o->camera ) {
		n = knl_oj_put(t, n, max, ": camera. Record 1 is its frames (read, the newest one "
				   "not had yet); record 2 what it is, 3 its modes, 4 the mode picked (written, "
				   "it is picked), 5 the stream (write 1 to start, 0 to stop), 6 its counts");
	} else if ( o->serial ) {
		n = knl_oj_put(t, n, max, ": serial port. Record 1 is the data (read, what has "
				   "come in; written, it is sent); record 2 its speed, record 3 what it is");
	} else if ( o->blksz > 1 ) {
		n = knl_oj_put(t, n, max, ": disk, block ");
		n = knl_oj_put_num(t, n, max, o->blksz);
		n = knl_oj_put(t, n, max, " bytes");
	} else {
		n = knl_oj_put(t, n, max, ": stream");
	}
	return knl_oj_put(t, n, max, "</p></document></tad>");
}

/* The list's record 0: a link to each device there is */
LOCAL INT list_text( UB *t, INT max )
{
	char	us[TS_UUID_STRLEN + 1];
	INT	n, i;

	n = knl_oj_put(t, 0, max, "<tad version=\"1.0\" encoding=\"UTF-8\" filename=\"devices\">"
			   "<document><p>devices</p>");
	LOCK();
	refresh();
	for ( i = 0; i < obd_hi; i++ ) {
		if ( !obd[i].used || !obd[i].seen ) continue;
		(void)ts_uuid_to_str(&obd[i].uuid, us, sizeof(us));
		n = knl_oj_put(t, n, max, "<p><link id=\"");
		n = knl_oj_put(t, n, max, us);
		n = knl_oj_put(t, n, max, "_0.xtad\"/>");
		n = knl_oj_put(t, n, max, (CONST char *)obd[i].name);
		n = knl_oj_put(t, n, max, "</p>");
	}
	UNLOCK();
	return knl_oj_put(t, n, max, "</document></tad>");
}

/* The devices there are now */
LOCAL INT list_count( void )
{
	INT	i, n = 0;

	LOCK();
	refresh();
	for ( i = 0; i < obd_hi; i++ ) {
		if ( obd[i].used && obd[i].seen ) n++;
	}
	UNLOCK();
	return n;
}

LOCAL INT two( UB *t, INT n, INT max, INT v )
{
	if ( v < 10 ) n = knl_oj_put(t, n, max, "0");
	return knl_oj_put_num(t, n, max, v);
}

/* The clock's record 1: the local time, ISO 8601 with the offset */
LOCAL INT clock_text( UB *t, INT max )
{
	TS_TIME	now;
	TS_TM	tm;
	INT	tz = 0, n, a;

	if ( dt_gettime(&now) < E_OK || dt_localtime(&now, &tm) < E_OK ) {
		return -1;
	}
	(void)dt_getsystz(&tz);
	if ( tm.tm_isdst ) tz += 60;		/* summer time: an hour more */
	n = knl_oj_put_num(t, 0, max, tm.tm_year + 1900);
	n = knl_oj_put(t, n, max, "-");
	n = two(t, n, max, tm.tm_mon + 1);
	n = knl_oj_put(t, n, max, "-");
	n = two(t, n, max, tm.tm_mday);
	n = knl_oj_put(t, n, max, "T");
	n = two(t, n, max, tm.tm_hour);
	n = knl_oj_put(t, n, max, ":");
	n = two(t, n, max, tm.tm_min);
	n = knl_oj_put(t, n, max, ":");
	n = two(t, n, max, tm.tm_sec);
	a = ( tz < 0 ) ? -tz : tz;
	n = knl_oj_put(t, n, max, ( tz < 0 ) ? "-" : "+");
	n = two(t, n, max, a / 60);
	n = knl_oj_put(t, n, max, ":");
	return two(t, n, max, a % 60);
}

/* Numbers of an ISO 8601 time: "2026-09-25T11:17:00" (an offset is ignored) */
LOCAL ER clock_set( CONST UB *s, SZ len )
{
	INT	v[6], k = 0, p = 0;
	TS_TM	tm;
	TS_TIME	t;

	while ( k < 6 && p < len ) {
		INT	x = 0;
		BOOL	any = FALSE;

		while ( p < len && s[p] >= '0' && s[p] <= '9' ) {
			x = x * 10 + ( s[p++] - '0' );
			any = TRUE;
		}
		if ( !any ) return E_PAR;
		v[k++] = x;
		if ( p < len ) p++;		/* the - : or T between */
	}
	if ( k < 6 ) {
		return E_PAR;
	}
	knl_memset(&tm, 0, sizeof(tm));
	tm.tm_year = v[0] - 1900;
	tm.tm_mon = v[1] - 1;
	tm.tm_mday = v[2];
	tm.tm_hour = v[3];
	tm.tm_min = v[4];
	tm.tm_sec = v[5];
	if ( dt_mktime_local(&tm, &t) < E_OK ) {
		return E_PAR;
	}
	return knl_dt_set_clock(t);		/* the key said it may; the write is told */
}

/* ---------------------------------------------------------------- a device added by name */

/* What it is, its ops, and whether it is still there */
LOCAL BOOL vdev_take( OBDHND *o, UB *name, CONST T_OBDVOPS **p_ops, void **p_ctx )
{
	BOOL	there;

	if ( name == NULL ) {
		/* a record read or written: whether it is still there is enough, and it is a word */
		there = added_there(o);
	} else {
		LOCK();
		there = added_there(o);
		if ( there ) {
			knl_memcpy(name, obd[o->ent].name, OBD_NAME);
		}
		UNLOCK();
	}
	*p_ops = o->vops;
	*p_ctx = o->vctx;
	return there;
}

/* Record 0: the name and what the one who serves it says, links included */
LOCAL INT vdev_describe( OBDHND *o, UB *t, INT max )
{
	CONST T_OBDVOPS	*ops;
	void		*ctx;
	UB		name[OBD_NAME];
	INT		n;

	if ( !vdev_take(o, name, &ops, &ctx) ) {
		return -1;
	}
	n = knl_oj_put(t, 0, max, "<tad version=\"1.0\" encoding=\"UTF-8\" filename=\"");
	n = knl_oj_put(t, n, max, (CONST char *)name);
	n = knl_oj_put(t, n, max, "\"><document><p>");
	n = knl_oj_put(t, n, max, (CONST char *)name);
	n = knl_oj_put(t, n, max, ": ");
	n = knl_oj_put(t, n, max, ops->kind);
	n = knl_oj_put(t, n, max, "</p>");
	if ( ops->text != NULL ) {
		n = ops->text(ctx, t, n, max);
	}
	return knl_oj_put(t, n, max, "</document></tad>");
}

LOCAL ER vdev_rea( OBDHND *o, INT recno, D off, void *buf, SZ size, SZ *p_asize )
{
	CONST T_OBDVOPS	*ops;
	void		*ctx;
	UB		*t;
	INT		len;
	SZ		n = 0;

	if ( !vdev_take(o, NULL, &ops, &ctx) ) {
		return E_NOEXS;			/* the device went */
	}
	if ( recno < 0 || recno >= ops->nrec ) {
		return E_NOEXS;
	}
	if ( recno > 0 ) {
		return ( ops->rea != NULL ) ? ops->rea(ctx, &o->pos, recno, off, buf, size, p_asize )
					    : E_NOSPT;
	}
	t = (UB *)Kmalloc(OBD_DESC_MAX);
	if ( t == NULL ) {
		return E_NOMEM;
	}
	len = vdev_describe(o, t, OBD_DESC_MAX);
	if ( len >= 0 && off < len ) {
		n = (SZ)( len - off );
		if ( n > size ) n = size;
		knl_memcpy(buf, t + off, (INT)n);
	}
	Kfree(t);
	if ( len < 0 ) {
		return E_NOEXS;
	}
	if ( p_asize != NULL ) *p_asize = n;
	return E_OK;
}

LOCAL ER vdev_wri( OBDHND *o, INT recno, D off, CONST void *buf, SZ size, SZ *p_asize )
{
	CONST T_OBDVOPS	*ops;
	void		*ctx;

	if ( !vdev_take(o, NULL, &ops, &ctx) ) {
		return E_NOEXS;
	}
	if ( recno < 0 || recno >= ops->nrec ) {
		return E_NOEXS;
	}
	if ( recno == 0 || ops->wri == NULL ) {
		return E_RONLY;
	}
	return ops->wri(ctx, &o->pos, recno, off, buf, size, p_asize);
}

LOCAL ER vdev_lrc( OBDHND *o, T_OBREC *buf, INT n, INT *p_cnt )
{
	CONST T_OBDVOPS	*ops;
	void		*ctx;
	UB		*t;
	INT		k;

	if ( !vdev_take(o, NULL, &ops, &ctx) ) {
		return E_NOEXS;
	}
	t = (UB *)Kmalloc(OBD_DESC_MAX);
	for ( k = 0; k < ops->nrec && k < n; k++ ) {
		buf[k].recno = k;
		buf[k].rt = ( k == 0 ) ? OB_RT_TAD : OB_RT_SYSDATA;
		buf[k].sub = 0;
		if ( k == 0 ) {
			INT	len = ( t != NULL ) ? vdev_describe(o, t, OBD_DESC_MAX) : 0;

			buf[k].size = ( len > 0 ) ? (UD)len : 0;
		} else {
			buf[k].size = ( ops->size != NULL ) ? ops->size(ctx, k) : 0;
		}
	}
	if ( t != NULL ) Kfree(t);
	*p_cnt = k;
	return E_OK;
}

LOCAL ER vdev_gat( OBDHND *o, UB *json, SZ size, SZ *p_asize )
{
	CONST T_OBDVOPS	*ops;
	void		*ctx;
	UB		name[OBD_NAME];
	INT		n;

	if ( !vdev_take(o, name, &ops, &ctx) ) {
		return E_NOEXS;
	}
	n = knl_oj_put(json, 0, (INT)size, "{\"name\":");
	n = knl_oj_put_str(json, n, (INT)size, name);
	n = knl_oj_put(json, n, (INT)size, ",\"device\":{\"kind\":");
	n = knl_oj_put_str(json, n, (INT)size, (CONST UB *)ops->kind);
	if ( ops->attr != NULL ) {
		n = ops->attr(ctx, json, n, (INT)size);
	}
	n = knl_oj_put(json, n, (INT)size, "}}");
	if ( n < 0 ) {
		return E_LIMIT;
	}
	if ( p_asize != NULL ) *p_asize = n;
	return E_OK;
}

LOCAL ER obd_rea( INT h, INT recno, D off, void *buf, SZ size, SZ *p_asize, BOOL nowait )
{
	OBDHND	*o = hnd_of(h);
	UB	t[OBD_TEXT_MAX];
	INT	len;
	SZ	n = 0, asz = 0;
	ER	er;

	(void)nowait;
	if ( o == NULL ) {
		return E_ID;
	}
	if ( o->vops != NULL ) {
		return vdev_rea(o, recno, off, buf, size, p_asize);
	}
	if ( off < 0 ) {
		return E_PAR;
	}
	if ( o->ent == ENT_LIST ) {
		UB	*l;

		if ( recno != 0 ) {
			return E_NOEXS;
		}
		l = (UB *)Kmalloc(OBD_LIST_MAX);
		if ( l == NULL ) {
			return E_NOMEM;
		}
		len = list_text(l, OBD_LIST_MAX);
		if ( len >= 0 && off < len ) {
			n = (SZ)( len - off );
			if ( n > size ) n = size;
			knl_memcpy(buf, l + off, (INT)n);
		}
		Kfree(l);
		if ( len < 0 ) {
			return E_SYS;
		}
		if ( p_asize != NULL ) *p_asize = n;
		return E_OK;
	}
	if ( o->ent == ENT_SYSTEM && recno > 0 ) {
		UB	*s;

		if ( recno >= OB_SYS_NREC ) {
			return E_NOEXS;
		}
		s = (UB *)Kmalloc(OBD_SYS_MAX);
		if ( s == NULL ) {
			return E_NOMEM;
		}
		len = knl_obsys_text(recno, s, OBD_SYS_MAX);
		if ( len >= 0 && off < len ) {
			n = (SZ)( len - off );
			if ( n > size ) n = size;
			knl_memcpy(buf, s + off, (INT)n);
		}
		Kfree(s);
		if ( len < 0 ) {
			return E_SYS;
		}
		if ( p_asize != NULL ) *p_asize = n;
		return E_OK;
	}
	if ( o->dd <= 0 && o->ent >= 0 && recno > 0 ) {
		return E_OACV;			/* opened for the attributes only */
	}
	if ( recno == 0 || ( recno == 1 && o->ent < 0 ) ) {
		len = ( recno == 0 ) ? describe(o, t, sizeof(t)) : clock_text(t, sizeof(t));
		if ( len < 0 ) {
			return E_SYS;
		}
		if ( off < len ) {
			n = (SZ)( len - off );
			if ( n > size ) n = size;
			knl_memcpy(buf, t + off, (INT)n);
		}
		if ( p_asize != NULL ) *p_asize = n;
		return E_OK;
	}
	if ( o->sound ) {
		if ( recno < 1 || recno >= OB_SND_NREC ) {
			return E_NOEXS;
		}
		if ( recno > OB_SND_PCM && off != 0 ) {
			return E_PAR;		/* an attribute is read whole */
		}
		er = tk_srea_dev(o->dd, snd_dn[recno], buf, size, &asz);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;
		return ( er < E_OK ) ? er : E_OK;
	}
	if ( o->camera ) {
		if ( recno < 1 || recno >= OB_CAM_NREC ) {
			return E_NOEXS;
		}
		if ( off != 0 ) {
			return E_PAR;		/* a frame and an attribute are read whole */
		}
		er = tk_srea_dev(o->dd, cam_dn[recno], buf, size, &asz);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;
		return ( er < E_OK ) ? er : E_OK;
	}
	if ( o->serial && recno > 1 && recno < OB_SER_NREC ) {
		if ( off != 0 ) {
			return E_PAR;		/* an attribute is read whole */
		}
		er = tk_srea_dev(o->dd, ser_dn[recno], buf, size, &asz);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;
		return ( er < E_OK ) ? er : E_OK;
	}
	if ( recno != 1 ) {
		return E_NOEXS;
	}
	if ( o->blksz > 1 ) {
		if ( off % o->blksz != 0 || size % o->blksz != 0 ) {
			return E_PAR;		/* whole blocks only */
		}
		/* the block drivers take the start in blocks and the size in bytes */
		er = tk_srea_dev(o->dd, (D)( off / o->blksz ), buf, size, &asz);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;
		return ( er < E_OK ) ? er : E_OK;
	}
	er = tk_srea_dev(o->dd, 0, buf, size, &asz);
	if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;

	return ( er < E_OK ) ? er : E_OK;
}

LOCAL ER obd_wri( INT h, INT recno, D off, CONST void *buf, SZ size, SZ *p_asize,
		  BOOL nowait )
{
	OBDHND	*o = hnd_of(h);
	SZ	asz = 0;
	ER	er;

	(void)nowait;
	if ( o == NULL ) {
		return E_ID;
	}
	if ( o->vops != NULL ) {
		return vdev_wri(o, recno, off, buf, size, p_asize);
	}
	if ( o->ent == ENT_LIST ) {
		return E_RONLY;
	}
	if ( o->ent == ENT_SYSTEM ) {
		if ( recno <= 0 || recno >= OB_SYS_NREC ) {
			return ( recno == 0 ) ? E_RONLY : E_NOEXS;
		}
		if ( off != 0 ) {
			return E_PAR;		/* what is asked is written whole */
		}
		er = knl_obsys_write(recno, (CONST UB *)buf, size);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = size;
		return er;
	}
	if ( o->dd <= 0 && o->ent >= 0 && recno > 0 ) {
		return E_OACV;			/* opened for the attributes only */
	}
	if ( o->sound && recno >= 1 && recno < OB_SND_NREC ) {
		if ( off != 0 ) {
			return E_PAR;		/* sound is written as it comes, an attribute whole */
		}
		if ( recno == OB_SND_OUTS || recno == OB_SND_OUTSEL ) {
			/* the output is the system's: chosen through its record 8, which is protected */
			return E_RONLY;
		}
		er = tk_swri_dev(o->dd, snd_dn[recno], buf, size, &asz);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;
		return ( er < E_OK ) ? er : E_OK;
	}
	if ( o->camera && ( recno == OB_CAM_MODE || recno == OB_CAM_STREAM ) ) {
		if ( off != 0 ) {
			return E_PAR;
		}
		er = tk_swri_dev(o->dd, cam_dn[recno], buf, size, &asz);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;
		return ( er < E_OK ) ? er : E_OK;
	}
	if ( o->serial && recno == 2 ) {
		if ( off != 0 ) {
			return E_PAR;
		}
		er = tk_swri_dev(o->dd, TDN_SER_SPEED, buf, size, &asz);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;
		return ( er < E_OK ) ? er : E_OK;
	}
	if ( recno != 1 || off < 0 ) {
		return ( recno == 0 || ( o->serial && recno == 3 ) ) ? E_RONLY : E_PAR;
	}
	if ( o->ent < 0 ) {
		er = clock_set((CONST UB *)buf, size);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = size;
		return er;
	}
	if ( o->blksz > 1 ) {
		if ( off % o->blksz != 0 || size % o->blksz != 0 ) {
			return E_PAR;
		}
		er = tk_swri_dev(o->dd, (D)( off / o->blksz ), buf, size, &asz);
		if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;
		return ( er < E_OK ) ? er : E_OK;
	}
	er = tk_swri_dev(o->dd, 0, buf, size, &asz);
	if ( er >= E_OK && p_asize != NULL ) *p_asize = asz;

	return ( er < E_OK ) ? er : E_OK;
}

LOCAL ER obd_lrc( INT h, T_OBREC *buf, INT n, INT *p_cnt )
{
	OBDHND	*o = hnd_of(h);
	UB	t[OBD_TEXT_MAX];
	ID	dd;
	INT	k;

	if ( o == NULL ) {
		return E_ID;
	}
	if ( o->vops != NULL ) {
		return vdev_lrc(o, buf, n, p_cnt);
	}
	if ( o->ent == ENT_LIST ) {
		UB	*l = (UB *)Kmalloc(OBD_LIST_MAX);
		INT	len = ( l != NULL ) ? list_text(l, OBD_LIST_MAX) : 0;

		if ( l != NULL ) Kfree(l);
		if ( n > 0 ) {
			buf[0].recno = 0;
			buf[0].rt = OB_RT_TAD;
			buf[0].sub = 0;
			buf[0].size = ( len > 0 ) ? (UD)len : 0;
		}
		*p_cnt = ( n > 0 ) ? 1 : 0;
		return E_OK;
	}
	if ( o->ent == ENT_SYSTEM ) {
		UB	*s = (UB *)Kmalloc(OBD_SYS_MAX);

		for ( k = 0; k < OB_SYS_NREC && k < n; k++ ) {
			buf[k].recno = k;
			buf[k].rt = ( k == 0 ) ? OB_RT_TAD : OB_RT_SYSDATA;
			buf[k].sub = 0;
			buf[k].size = ( k == 0 ) ? (UD)describe(o, t, sizeof(t))
				    : ( s != NULL ) ? (UD)knl_obsys_text(k, s, OBD_SYS_MAX) : 0;
		}
		if ( s != NULL ) Kfree(s);
		*p_cnt = k;
		return E_OK;
	}
	/* a key for the attributes has the device asked with it opened for now */
	dd = o->dd;
	if ( dd <= 0 && o->ent >= 0 && ( o->sound || o->camera || o->blksz > 1 ) ) {
		dd = tk_opn_dev(obd[o->ent].name, TD_READ);
	}
	for ( k = 0; k < ( o->sound ? OB_SND_NREC : o->camera ? OB_CAM_NREC : o->serial ? OB_SER_NREC : 2 ) && k < n; k++ ) {
		buf[k].recno = k;
		buf[k].rt = ( k == 0 ) ? OB_RT_TAD : OB_RT_SYSDATA;
		buf[k].sub = 0;
		buf[k].size = ( k == 0 ) ? (UD)describe(o, t, sizeof(t)) : 0;
		if ( o->serial && k > 1 ) {
			buf[k].size = ( k == 2 ) ? sizeof(UW) : sizeof(T_SERINFO);
		}
		if ( o->camera && k > OB_CAM_FRAME ) {
			SZ	asz = 0;

			if ( dd > 0 && tk_srea_dev(dd, cam_dn[k], NULL, 0, &asz) >= E_OK ) {
				buf[k].size = (UD)asz;
			}
		}
		if ( o->sound && k > OB_SND_PCM ) {
			SZ	asz = 0;

			/* an attribute read with no room says how large it is */
			if ( dd > 0 && tk_srea_dev(dd, snd_dn[k], NULL, 0, &asz) >= E_OK ) {
				buf[k].size = (UD)asz;
			}
		}
		if ( k == 1 && o->blksz > 1 && !o->sound && !o->serial && o->ent >= 0 ) {
			DiskInfo	di;
			SZ		asz = 0;

			/* a disk's blocks: record 1 is as long as the unit */
			if ( dd > 0 && tk_srea_dev(dd, TDN_DISKINFO, &di, sizeof(di), &asz) >= E_OK
			  && di.blockcont > 0 ) {
				buf[k].size = (UD)di.blockcont * (UD)o->blksz;
			}
		}
	}
	if ( dd > 0 && dd != o->dd ) {
		tk_cls_dev(dd, 0);
	}
	*p_cnt = k;

	return E_OK;
}

LOCAL ER obd_gat( INT h, UB *json, SZ size, SZ *p_asize )
{
	OBDHND	*o = hnd_of(h);
	T_FSMNT	mnt;
	INT	n;

	if ( o == NULL ) {
		return E_ID;
	}
	if ( o->vops != NULL ) {
		return vdev_gat(o, json, size, p_asize);
	}
	if ( o->ent == ENT_LIST ) {
		n = knl_oj_put(json, 0, (INT)size, "{\"name\":\"devices\",\"device\":{\"kind\":\"list\","
			       "\"virtual\":true,\"count\":");
		n = knl_oj_put_num(json, n, (INT)size, list_count());
		n = knl_oj_put(json, n, (INT)size, "}}");
		if ( n < 0 ) {
			return E_LIMIT;
		}
		if ( p_asize != NULL ) *p_asize = n;
		return E_OK;
	}
	if ( o->ent == ENT_SYSTEM ) {
		n = knl_oj_put(json, 0, (INT)size, "{\"name\":\"system\",\"device\":{\"kind\":\"system\","
			       "\"virtual\":true}}");
		if ( n < 0 ) {
			return E_LIMIT;
		}
		if ( p_asize != NULL ) *p_asize = n;
		return E_OK;
	}
	n = knl_oj_put(json, 0, (INT)size, "{\"name\":");
	n = knl_oj_put_str(json, n, (INT)size,
			   ( o->ent < 0 ) ? (CONST UB *)"clock" : obd[o->ent].name);
	n = knl_oj_put(json, n, (INT)size, ",\"device\":{\"kind\":");
	n = knl_oj_put(json, n, (INT)size, ( o->ent < 0 ) ? "\"clock\",\"virtual\":true"
				   : ( o->blksz > 1 ) ? "\"disk\",\"virtual\":false"
						      : "\"stream\",\"virtual\":false");
	n = knl_oj_put(json, n, (INT)size, ",\"blksz\":");
	n = knl_oj_put_num(json, n, (INT)size, o->blksz);
	n = knl_oj_put(json, n, (INT)size, "}");
	/*
	 * A disk: what is on it (design 7.6), and the volume mounted from
	 * it, where, with what and how (design 12.2.3)
	 */
	if ( o->ent >= 0 && o->blksz > 1 && !o->sound && !o->serial ) {
		T_FSPROBE	*pr = (T_FSPROBE *)Kmalloc(sizeof(T_FSPROBE));

		BOOL		mounted = knl_fs_mount_of((CONST char *)obd[o->ent].name, &mnt);

		n = knl_oj_put(json, n, (INT)size, ",\"tessronos\":{");
		if ( mounted ) {
			n = knl_oj_put(json, n, (INT)size, "\"mount\":{\"path\":");
			n = knl_oj_put_str(json, n, (INT)size, mnt.path);
			n = knl_oj_put(json, n, (INT)size, ",\"fs\":");
			n = knl_oj_put_str(json, n, (INT)size, mnt.fimp);
			n = knl_oj_put(json, n, (INT)size, ( mnt.flags & FS_MNT_RDONLY ) ? ",\"readonly\":true}"
											 : ",\"readonly\":false}");
		}
		if ( pr != NULL && knl_fs_probe((CONST char *)obd[o->ent].name, pr) >= E_OK ) {
			n = knl_oj_put(json, n, (INT)size, mounted ? ",\"format\":{\"kind\":" : "\"format\":{\"kind\":");
			n = knl_oj_put_str(json, n, (INT)size, pr->name);
			n = knl_oj_put(json, n, (INT)size, ",\"label\":");
			n = knl_oj_put_str(json, n, (INT)size, pr->label);
			n = knl_oj_put(json, n, (INT)size, ",\"fs\":");
			n = knl_oj_put_str(json, n, (INT)size, pr->fimp);
			n = knl_oj_put(json, n, (INT)size, "}");
		}
		if ( pr != NULL ) Kfree(pr);
		n = knl_oj_put(json, n, (INT)size, "}");
	}
	n = knl_oj_put(json, n, (INT)size, "}");
	if ( n < 0 ) {
		return E_LIMIT;
	}
	if ( p_asize != NULL ) *p_asize = n;

	return E_OK;
}

LOCAL ER obd_lst( CONST TS_UUID *from, TS_UUID *buf, INT n, INT *p_cnt )
{
	INT	cnt = 0, i;

	CONST TS_UUID	*virt[3];
	INT		k;

	virt[0] = &ob_uuid_clock;
	virt[1] = &ob_uuid_devlist;
	virt[2] = &ob_uuid_system;
	LOCK();
	refresh();
	while ( cnt < n ) {
		CONST TS_UUID	*best = NULL;

		/* the clock, the list and the system among them */
		for ( k = 0; k < 3; k++ ) {
			if ( cnt > 0 && ts_uuid_cmp(virt[k], &buf[cnt - 1]) <= 0 ) continue;
			if ( cnt == 0 && from != NULL && ts_uuid_cmp(virt[k], from) <= 0 ) continue;
			if ( best == NULL || ts_uuid_cmp(virt[k], best) < 0 ) {
				best = virt[k];
			}
		}
		for ( i = 0; i < obd_hi; i++ ) {
			if ( !obd[i].used || !obd[i].seen ) continue;
			if ( cnt > 0 && ts_uuid_cmp(&obd[i].uuid, &buf[cnt - 1]) <= 0 ) continue;
			if ( cnt == 0 && from != NULL && ts_uuid_cmp(&obd[i].uuid, from) <= 0 ) continue;
			if ( best == NULL || ts_uuid_cmp(&obd[i].uuid, best) < 0 ) {
				best = &obd[i].uuid;
			}
		}
		if ( best == NULL ) break;
		buf[cnt++] = *best;
	}
	UNLOCK();
	*p_cnt = cnt;

	return E_OK;
}

/* ---------------------------------------------------------------- mounting a disk's volume */

/*
 * The disk a key names: the key must be the caller's (EX_BADF), the
 * object a disk device (EX_NOTBLK), and the key allow 'ops' on record
 * 1, the blocks, as reading and writing them would (EX_ACCES). Its
 * name and object come back.
 */
LOCAL ER disk_of_key( ID key, UINT ops, UB *name, TS_UUID *p_uuid )
{
	T_RDEV	rd;
	INT	e;

	if ( knl_ob_key_rec(key, 0, -1, p_uuid) < E_OK ) {
		return EX_BADF;
	}
	if ( obd_mtx <= 0 || is_clock(p_uuid) || is_list(p_uuid) ) {
		return EX_NOTBLK;
	}
	LOCK();
	e = ent_by_uuid(p_uuid);
	if ( e >= 0 ) {
		knl_memcpy(name, obd[e].name, OBD_NAME);
	}
	UNLOCK();
	if ( e < 0 || tk_ref_dev(name, &rd) < E_OK || rd.blksz <= 1 || dev_is_sound(name) ) {
		return EX_NOTBLK;
	}
	if ( ops != 0 && knl_ob_key_rec(key, ops, 1, p_uuid) < E_OK ) {
		return EX_ACCES;
	}
	return EX_OK;
}

/* What a mount with these flags needs of the blocks */
LOCAL UINT mount_ops( UINT flags )
{
	return ( ( flags & FS_MNT_RDONLY ) != 0 ) ? OB_OP_READ : ( OB_OP_READ | OB_OP_WRITE );
}

EXPORT ER fs_attach_dev( ID devkey, CONST char *fimpnm, UINT flags )
{
	UB	name[OBD_NAME];
	TS_UUID	u;
	ER	er;

	if ( ( flags & ~(UINT)FS_MNT_RDONLY ) != 0 ) {
		return EX_INVAL;
	}
	er = disk_of_key(devkey, mount_ops(flags), name, &u);
	if ( er < EX_OK ) {
		return er;
	}
	if ( fimpnm == NULL ) {
		/* the fimp of what is on it */
		T_FSPROBE	*pr = (T_FSPROBE *)Kmalloc(sizeof(T_FSPROBE));

		if ( pr == NULL ) {
			return EX_NOMEM;
		}
		er = knl_fs_probe((CONST char *)name, pr);
		fimpnm = ( er >= E_OK ) ? knl_fs_kind_fimp(pr->kind) : "";
		Kfree(pr);
		if ( er < E_OK ) {
			return EX_IO;
		}
		if ( fimpnm[0] == 0 ) {
			return EX_NOTSUP;	/* no fimp mounts this kind */
		}
	}
	er = knl_fs_attach_media(fimpnm, (CONST char *)name, flags);
	if ( er >= EX_OK ) {
		dev_post(&u, OB_E_CHANGE);
	}
	return er;
}

/* Undone by one who could have done it: the same rights on the blocks */
EXPORT ER fs_detach_dev( ID devkey )
{
	UB	name[OBD_NAME];
	T_FSMNT	mnt;
	TS_UUID	u;
	ER	er;

	er = disk_of_key(devkey, 0, name, &u);
	if ( er < EX_OK ) {
		return er;
	}
	if ( !knl_fs_mount_of((CONST char *)name, &mnt) ) {
		return EX_NOENT;
	}
	if ( knl_ob_key_rec(devkey, mount_ops(mnt.flags), 1, &u) < E_OK ) {
		return EX_ACCES;
	}
	er = knl_fs_detach_media((CONST char *)name);
	if ( er >= EX_OK ) {
		dev_post(&u, OB_E_CHANGE);
	}
	return er;
}

LOCAL CONST T_OBMGR obd_mgr = {
	OB_T_DEVICE, 0, "device",
	obd_find, obd_ref, obd_prot, NULL, NULL, NULL,
	obd_open, obd_close, obd_rea, obd_wri, NULL, NULL, NULL, obd_lrc,
	obd_gat, NULL, NULL, obd_lst, NULL,
	NULL, NULL, NULL, NULL,
	NULL, NULL,
	/* no icon of their own */
	NULL, NULL
};

EXPORT ER knl_obdev_init( void )
{
	T_CMTX	cmtx;
	INT	no;

	cmtx.exinf = NULL;
	cmtx.mtxatr = TA_TFIFO;
	obd_mtx = tk_cre_mtx(&cmtx);
	if ( obd_mtx <= 0 ) {
		return (ER)obd_mtx;
	}
	no = knl_ob_regist(&obd_mgr);

	return ( no > 0 ) ? E_OK : (ER)no;
}

/*
 * The devices outside the device management, made objects once the
 * object layer is up: the volume the device box is on is attached by
 * now, so they get the UUIDs they had before.
 */
EXPORT void knl_obdev_start( void )
{
	if ( obd_mtx <= 0 ) {
		return;
	}
	knl_obrand_start();
	knl_obdisp_start();
	knl_obinput_start();
	knl_obgpio_start();
	knl_obusb_start();
}
