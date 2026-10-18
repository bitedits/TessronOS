/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ktest_grow.c
 *	GPT partitions made and grown, and TSFS volumes grown with them
 *	(design 7.7)
 *
 *	A blank USB disk of 96MB (volg.img, tools/tests/mkvoldisk.py) is
 *	plugged into the hub through the QEMU monitor. Its first sectors and
 *	its last are cleared and it is plugged in again, so that every run
 *	starts from a disk with nothing on it. Then:
 *
 *	  - a partition is made on the blank disk: a table is made, both
 *	    copies pass their CRCs, and a TSFS volume with the label given
 *	    is on the partition;
 *	  - the table is written again as a card written with a smaller
 *	    image has it, the backup in the middle; objects are put on the
 *	    volume, and the partition and the volume are grown: the backup
 *	    is at the end of the disk now, the device and the volume are as
 *	    large as asked, the check finds nothing, the objects read back,
 *	    and an object larger than the volume was is written;
 *	  - the volume is grown again while it is open;
 *	  - a cut in the power is pretended after each write of a volume's
 *	    grow in turn: opened again, the volume is the size it was or the
 *	    size it was going to, writes, and checks whole;
 *	  - the same after each write of the table: the table that passes is
 *	    the one before or the one after, never a mixture;
 *	  - what the start did with the system volume is consistent.
 *
 *	The disk is taken out at the end.
 */

#include "ktest.h"
#include "tstdlib.h"
#include <ts/fs.h>
#include <ts/blk.h>
#include <ts/gpt.h>
#include <ts/ob.h>
#include <ts/tsfs.h>
#include <ts/tsfsblk.h>
#include <ts/tsfsck.h>
#include <ts/uuid.h>

#ifndef KT_VOL_DIR
#define KT_VOL_DIR	""		/* no images: nothing is plugged in */
#endif

#define G_BYTES		( 96ULL << 20 )
#define G_SECT		( G_BYTES / 512 )
#define MB		2048ULL			/* sectors */
#define NAME_MAX	OB_DV_NAME
#define LIST_MAX	2048
#define WAIT_MS		30000
#define NOBJ		6
#define OBJ_BYTES	( 300 * 1024 )

LOCAL UB	disk[NAME_MAX];
LOCAL BOOL	in = FALSE;
LOCAL UB	*names;
LOCAL TS_UUID	objs[NOBJ];

/* ---------------------------------------------------------------- the disk */

LOCAL BOOL is_whole( CONST UB *n )
{
	INT	k = (INT)knl_strlen((CONST char *)n);

	return (BOOL)( k > 0 && !( n[k - 1] >= '0' && n[k - 1] <= '9' ) );
}

LOCAL BOOL find_disk( void )
{
	T_FSPROBE	pr;
	INT		n = knl_obdev_disks(names, LIST_MAX), i;

	for ( i = 0; i < n; i++ ) {
		UB	*nm = names + i * NAME_MAX;

		if ( is_whole(nm) && knl_fs_probe((CONST char *)nm, &pr) >= E_OK && pr.bytes == G_BYTES ) {
			knl_memcpy(disk, nm, NAME_MAX);
			return TRUE;
		}
	}
	return FALSE;
}

LOCAL BOOL listed( CONST UB *name )
{
	INT	n = knl_obdev_disks(names, LIST_MAX), i;

	for ( i = 0; i < n; i++ ) {
		if ( knl_strcmp((CONST char *)( names + i * NAME_MAX ), (CONST char *)name) == 0 ) return TRUE;
	}
	return FALSE;
}

LOCAL BOOL plug( void )
{
	INT	t;

	tm_printf((UB *)"KTEST QMP {\"execute\":\"human-monitor-command\",\"arguments\":{\"command-line\":"
		  "\"drive_add 0 if=none,id=vdkg,file=%s/volg.img,format=raw\"}}\n", KT_VOL_DIR);
	tk_dly_tsk(300);
	tm_printf((UB *)"KTEST QMP {\"execute\":\"device_add\",\"arguments\":{\"driver\":\"usb-storage\","
		  "\"id\":\"volg\",\"bus\":\"xhci.0\",\"port\":\"4.5\",\"drive\":\"vdkg\",\"removable\":true}}\n");
	for ( t = 0; t < WAIT_MS / 100; t++ ) {
		tk_dly_tsk(100);
		if ( find_disk() ) {
			in = TRUE;
			return TRUE;
		}
	}
	return FALSE;
}

LOCAL BOOL unplug( void )
{
	INT	t;

	tm_printf((UB *)"KTEST QMP {\"execute\":\"device_del\",\"arguments\":{\"id\":\"volg\"}}\n");
	tk_dly_tsk(300);
	tm_printf((UB *)"KTEST QMP {\"execute\":\"human-monitor-command\",\"arguments\":{\"command-line\":"
		  "\"drive_del vdkg\"}}\n");
	for ( t = 0; t < WAIT_MS / 100; t++ ) {
		tk_dly_tsk(100);
		if ( !listed(disk) ) {
			in = FALSE;
			return TRUE;
		}
	}
	return FALSE;
}

/* The name of partition `unit` of the disk */
LOCAL void part_name( INT unit, UB *out )
{
	INT	k = (INT)knl_strlen((CONST char *)disk);

	knl_memcpy(out, disk, k);
	if ( unit >= 10 ) out[k++] = (UB)( '0' + unit / 10 );
	out[k++] = (UB)( '0' + unit % 10 );
	out[k] = 0;
}

LOCAL ER sect_rw( CONST UB *dev, BOOL wr, UD lba, UD n, void *buf )
{
	SZ	asz;
	ID	dd = tk_opn_dev(dev, wr ? TD_UPDATE : TD_READ);
	ER	er;

	if ( dd < E_OK ) return dd;
	er = wr ? tk_swri_dev(dd, (W)lba, buf, (SZ)( n * 512 ), &asz)
		: tk_srea_dev(dd, (W)lba, buf, (SZ)( n * 512 ), &asz);
	tk_cls_dev(dd, 0);
	return er;
}

LOCAL UD dev_sect( CONST UB *dev )
{
	DiskInfo	di;
	SZ		asz;
	ID		dd = tk_opn_dev(dev, TD_READ);
	UD		n = 0;

	if ( dd < E_OK ) return 0;
	if ( tk_srea_dev(dd, TDN_DISKINFO, &di, sizeof(di), &asz) >= E_OK ) n = (UD)di.blockcont;
	tk_cls_dev(dd, 0);
	return n;
}

LOCAL ER rescan( void )
{
	SZ	asz;
	ID	dd = tk_opn_dev(disk, TD_UPDATE);
	ER	er;

	if ( dd < E_OK ) return dd;
	er = tk_swri_dev(dd, TDN_PARTSCAN, NULL, 0, &asz);
	tk_cls_dev(dd, 0);
	knl_obdev_changed();
	return er;
}

/* ---------------------------------------------------------------- the table, as the test writes and reads it */

LOCAL UW crc32( CONST UB *p, UD n )
{
	UW	c = 0xFFFFFFFFU;
	INT	k;

	while ( n-- > 0 ) {
		c ^= *p++;
		for ( k = 0; k < 8; k++ ) c = ( c & 1 ) ? ( c >> 1 ) ^ 0xEDB88320U : ( c >> 1 );
	}
	return ~c;
}

LOCAL UW rd32( CONST UB *p )
{
	return (UW)p[0] | ((UW)p[1] << 8) | ((UW)p[2] << 16) | ((UW)p[3] << 24);
}

LOCAL UD rd64( CONST UB *p )
{
	return (UD)rd32(p) | ((UD)rd32(p + 4) << 32);
}

LOCAL void wr32( UB *p, UW v )
{
	p[0] = (UB)v; p[1] = (UB)( v >> 8 ); p[2] = (UB)( v >> 16 ); p[3] = (UB)( v >> 24 );
}

LOCAL void wr64( UB *p, UD v )
{
	wr32(p, (UW)v);
	wr32(p + 4, (UW)( v >> 32 ));
}

/* A partition as the test lays it down */
typedef struct {
	UD	first, last;
	UB	uniq[16];
} PART;

/*
 * A table of the partitions given, written as an image `end` sectors
 * long has it: the backup right before `end`. The partition GUIDs are
 * kept from one write to the next.
 */
LOCAL ER table_put( CONST PART *p, INT n, UD end )
{
	UB	*buf = (UB *)Kcalloc(34, 512), h[512];
	UD	back = end - 33;
	UW	ecrc;
	INT	i;
	ER	er;

	if ( buf == NULL ) return E_NOMEM;
	/* the protective MBR, sized as the image */
	buf[446 + 2] = 0x02;
	buf[446 + 4] = 0xEE;
	buf[446 + 5] = buf[446 + 6] = buf[446 + 7] = 0xFF;
	wr32(buf + 446 + 8, 1);
	wr32(buf + 446 + 12, (UW)( end - 1 ));
	buf[510] = 0x55;
	buf[511] = 0xAA;
	for ( i = 0; i < n; i++ ) {
		UB	*e = buf + 1024 + i * 128;

		knl_memcpy(e, knl_tsfs_type_guid, 16);
		knl_memcpy(e + 16, p[i].uniq, 16);
		wr64(e + 32, p[i].first);
		wr64(e + 40, p[i].last);
	}
	ecrc = crc32(buf + 1024, 128 * 128);
	knl_memset(h, 0, sizeof(h));
	knl_memcpy(h, "EFI PART", 8);
	wr32(h + 8, 0x00010000);
	wr32(h + 12, 92);
	wr64(h + 24, 1);
	wr64(h + 32, end - 1);
	wr64(h + 40, 34);
	wr64(h + 48, back - 1);
	for ( i = 0; i < 16; i++ ) h[56 + i] = (UB)( 0x40 + i );
	wr64(h + 72, 2);
	wr32(h + 80, 128);
	wr32(h + 84, 128);
	wr32(h + 88, ecrc);
	wr32(h + 16, crc32(h, 92));
	knl_memcpy(buf + 512, h, 512);
	er = sect_rw(disk, TRUE, 0, 34, buf);
	/* the backup */
	wr64(h + 24, end - 1);
	wr64(h + 32, 1);
	wr64(h + 72, back);
	wr32(h + 16, 0);
	wr32(h + 16, crc32(h, 92));
	if ( er >= E_OK ) er = sect_rw(disk, TRUE, back, 32, buf + 1024);
	if ( er >= E_OK ) er = sect_rw(disk, TRUE, end - 1, 1, h);
	Kfree(buf);
	return er;
}

/*
 * One copy of the table read and checked: its header at `lba`, then the
 * entries it names. The partitions it lists go to `p` (up to 4).
 */
LOCAL BOOL table_copy( UD lba, PART *p, INT *p_n, UD *p_alt )
{
	UB	*buf = (UB *)Kmalloc(33 * 512), h[92];
	BOOL	ok = FALSE;
	INT	i, n = 0;

	if ( buf == NULL ) return FALSE;
	if ( sect_rw(disk, FALSE, lba, 1, buf) >= E_OK && buf[0] == 'E' && buf[1] == 'F' && buf[2] == 'I' ) {
		knl_memcpy(h, buf, 92);
		wr32(h + 16, 0);
		if ( rd64(buf + 24) == lba && crc32(h, 92) == rd32(buf + 16) ) {
			UD	elba = rd64(buf + 72);
			UW	ecrc = rd32(buf + 88);

			*p_alt = rd64(buf + 32);
			if ( sect_rw(disk, FALSE, elba, 32, buf) >= E_OK && crc32(buf, 128 * 128) == ecrc ) {
				ok = TRUE;
				for ( i = 0; i < 128 && n < 4; i++ ) {
					UB	*e = buf + i * 128;

					if ( rd64(e) == 0 && rd64(e + 8) == 0 ) continue;
					p[n].first = rd64(e + 32);
					p[n].last = rd64(e + 40);
					knl_memcpy(p[n].uniq, e + 16, 16);
					n++;
				}
			}
		}
	}
	Kfree(buf);
	*p_n = n;
	return ok;
}

/* Both copies pass, the backup at the end of the disk, and they list the same partitions */
LOCAL BOOL tables_ok( PART *p, INT *p_n )
{
	PART	q[4];
	UD	alt = 0, alt2 = 0, nsect = dev_sect(disk);
	INT	n = 0, m = 0, i;

	if ( !table_copy(1, p, &n, &alt) || alt != nsect - 1 || !table_copy(nsect - 1, q, &m, &alt2) || alt2 != 1 ) {
		tm_printf((UB *)"  table: primary %d alt %d, backup %d\n", n, (INT)alt, m);
		return FALSE;
	}
	if ( n != m ) return FALSE;
	for ( i = 0; i < n; i++ ) {
		if ( p[i].first != q[i].first || p[i].last != q[i].last ) return FALSE;
	}
	*p_n = n;
	return TRUE;
}

/* ---------------------------------------------------------------- objects */

LOCAL UB pat( UW seed, UD i )
{
	return (UB)( ( i * 131 + seed * 7 ) ^ ( i >> 9 ) );
}

LOCAL ER obj_put( ID vol, UW seed, UD bytes, TS_UUID *p_u )
{
	T_COBJ	c;
	UB	*x;
	SZ	asz;
	UD	i;
	INT	pos = 0;
	ID	od;
	ER	er;

	c.json = (CONST UB *)"{\"name\":\"grow\"}";
	c.jsonsz = (SZ)knl_strlen((CONST char *)c.json);
	er = ts_cre_obj(vol, &c, p_u);
	if ( er < E_OK ) return er;
	od = ts_opn_obj(vol, p_u, TFO_READ | TFO_WRITE);
	if ( od < E_OK ) return od;
	x = (UB *)Kmalloc((SZ)bytes);
	if ( x == NULL ) {
		ts_cls_obj(od);
		return E_NOMEM;
	}
	for ( i = 0; i < bytes; i++ ) x[i] = pat(seed, i);
	er = ts_ins_rec(od, -1, 15, 0, &pos);
	if ( er >= E_OK ) er = ts_wri_rec(od, pos, 0, x, (SZ)bytes, &asz);
	Kfree(x);
	ts_cls_obj(od);
	return er;
}

LOCAL BOOL obj_same( ID vol, UW seed, UD bytes, CONST TS_UUID *u )
{
	UB	*x;
	SZ	asz = 0;
	UD	i;
	ID	od;
	BOOL	ok;

	od = ts_opn_obj(vol, u, TFO_READ);
	if ( od < E_OK ) return FALSE;
	x = (UB *)Kmalloc((SZ)bytes);
	ok = (BOOL)( x != NULL && ts_rea_rec(od, 0, 0, x, (SZ)bytes, &asz) >= E_OK && (UD)asz == bytes );
	for ( i = 0; ok && i < bytes; i++ ) {
		if ( x[i] != pat(seed, i) ) ok = FALSE;
	}
	if ( x != NULL ) Kfree(x);
	ts_cls_obj(od);
	return ok;
}

LOCAL INT problems( CONST UB *part )
{
	T_TSFSCK	r;
	ER		er;

	knl_memset(&r, 0, sizeof(r));
	er = ob_chk_run((CONST char *)part, 0, &r, NULL, NULL);
	if ( er < E_OK ) {
		tm_printf((UB *)"  check of %s: %d\n", part, (INT)er);
		return 1000;
	}
	if ( r.problems != 0 ) {
		INT	k;

		for ( k = 0; k < TSFSCK_NKIND; k++ ) {
			if ( r.found[k] != 0 ) tm_printf((UB *)"  check of %s: kind %d x%d\n", part, k, r.found[k]);
		}
	}
	return r.problems;
}

LOCAL UD vol_blocks( CONST UB *part )
{
	T_TSFSBLK_SB	sb;
	ID		v = ts_opn_vol_blk((CONST char *)part);
	UD		n = 0;

	if ( v <= 0 ) return 0;
	if ( ts_ref_vol_blk(v, &sb) >= E_OK ) n = sb.total_blocks;
	ts_cls_vol_blk(v);
	return n;
}

/* ---------------------------------------------------------------- the tests */

LOCAL void test_plug( void )
{
	UB	*z;

	if ( KT_VOL_DIR[0] == 0 ) KT_SKIP("no disk images (make KTEST=1 builds them)");
	KT_ASSERT(plug());
	if ( !in ) return;
	/* what an earlier run left: the tables cleared, and the disk plugged in again */
	z = (UB *)Kcalloc(64, 512);
	KT_ASSERT(z != NULL);
	if ( z == NULL ) return;
	KT_ASSERT_ER(sect_rw(disk, TRUE, 0, 64, z), E_OK);
	KT_ASSERT_ER(sect_rw(disk, TRUE, G_SECT - 64, 64, z), E_OK);
	KT_ASSERT_ER(sect_rw(disk, TRUE, 36 * MB - 64, 64, z), E_OK);
	Kfree(z);
	KT_ASSERT(unplug());
	KT_ASSERT(plug());
	tm_printf((UB *)"  volg.img is %s\n", disk);
}

LOCAL void test_fresh( void )
{
	T_GPTFREE	fr[4];
	T_FSPROBE	pr;
	PART		p[4];
	UB		part[NAME_MAX], want[NAME_MAX];
	UD		nsect = 0;
	BOOL		fresh = FALSE;
	INT		n;

	if ( !in ) KT_SKIP("no disk");
	n = knl_gpt_free((CONST char *)disk, fr, 4, &nsect, &fresh);
	KT_ASSERT_EQ(n, 1);
	KT_ASSERT(fresh);
	KT_ASSERT_EQ(nsect, G_SECT);
	KT_ASSERT_EQ(fr[0].first, MB);
	KT_ASSERT_EQ(fr[0].last, G_SECT - MB - 1);
	KT_ASSERT_ER(knl_tsfs_part_make((CONST char *)disk, MB, 16ULL << 20, "GROWA", part, sizeof(part)), E_OK);
	part_name(0, want);
	KT_ASSERT_EQ(knl_strcmp((CONST char *)part, (CONST char *)want), 0);
	KT_ASSERT(tables_ok(p, &n));
	KT_ASSERT_EQ(n, 1);
	KT_ASSERT_EQ(p[0].first, MB);
	KT_ASSERT_EQ(p[0].last, 17 * MB - 1);
	KT_ASSERT_ER(knl_fs_probe((CONST char *)part, &pr), E_OK);
	KT_ASSERT_EQ(pr.kind, FS_KIND_TSFS);
	KT_ASSERT_EQ(knl_strcmp((CONST char *)pr.label, "GROWA"), 0);
	KT_ASSERT_EQ(dev_sect(part), 16 * MB);
	/* what is left of the disk after it */
	n = knl_gpt_free((CONST char *)disk, fr, 4, NULL, &fresh);
	KT_ASSERT_EQ(n, 1);
	KT_ASSERT(!fresh);
	KT_ASSERT_EQ(fr[0].first, 17 * MB);
}

/* The table of a card written with a 36MB image: the backup in the middle */
LOCAL void test_image( void )
{
	T_GPTPART	gp;
	PART		p[4];
	UB		part[NAME_MAX];
	UD		from = 0, to = 0, nsect;
	ID		vol;
	INT		n = 0, i;

	if ( !in ) KT_SKIP("no disk");
	KT_ASSERT(tables_ok(p, &n));
	KT_ASSERT_ER(table_put(p, 1, 36 * MB), E_OK);
	KT_ASSERT(rescan() >= E_OK);
	part_name(0, part);
	KT_ASSERT_ER(knl_gpt_part((CONST char *)part, &gp), E_OK);
	KT_ASSERT(gp.tsfs);
	KT_ASSERT(gp.tail);
	KT_ASSERT_EQ(gp.last, 17 * MB - 1);
	KT_ASSERT_EQ(gp.grow_last, G_SECT - MB - 1);	/* to the end, not to where the image ended */

	vol = ts_opn_vol((CONST char *)part, TSFS_STORE_BLK);
	KT_ASSERT(vol > 0);
	if ( vol <= 0 ) return;
	for ( i = 0; i < NOBJ; i++ ) KT_ASSERT_ER(obj_put(vol, (UW)i, OBJ_BYTES, &objs[i]), E_OK);
	KT_ASSERT_ER(ts_cls_vol(vol), E_OK);

	KT_ASSERT_ER(knl_tsfs_part_grow((CONST char *)part, 48ULL << 20, &from, &to), E_OK);
	KT_ASSERT_EQ(from, 16ULL << 20);
	KT_ASSERT_EQ(to, 48ULL << 20);
	KT_ASSERT(tables_ok(p, &n));
	KT_ASSERT_EQ(n, 1);
	KT_ASSERT_EQ(p[0].last, 49 * MB - 1);
	nsect = dev_sect(part);
	KT_ASSERT_EQ(nsect, 48 * MB);
	KT_ASSERT_EQ(vol_blocks(part), ( 48ULL << 20 ) / 4096);
	KT_ASSERT_EQ(problems(part), 0);

	vol = ts_opn_vol((CONST char *)part, TSFS_STORE_BLK);
	KT_ASSERT(vol > 0);
	if ( vol <= 0 ) return;
	for ( i = 0; i < NOBJ; i++ ) KT_ASSERT(obj_same(vol, (UW)i, OBJ_BYTES, &objs[i]));
	/* larger than the volume was: it takes the groups added */
	{
		TS_UUID	big;

		KT_ASSERT_ER(obj_put(vol, 99, 20ULL << 20, &big), E_OK);
		KT_ASSERT(obj_same(vol, 99, 20ULL << 20, &big));
	}
	KT_ASSERT_ER(ts_cls_vol(vol), E_OK);
	KT_ASSERT_EQ(problems(part), 0);
}

/* Grown while it is open: the size a volume says follows, and it goes on writing */
LOCAL void test_live( void )
{
	T_RVOL	rv;
	UB	part[NAME_MAX];
	UD	from = 0, to = 0;
	TS_UUID	u;
	ID	vol;

	if ( !in ) KT_SKIP("no disk");
	part_name(0, part);
	vol = ts_opn_vol((CONST char *)part, TSFS_STORE_BLK);
	KT_ASSERT(vol > 0);
	if ( vol <= 0 ) return;
	KT_ASSERT_ER(knl_tsfs_part_grow((CONST char *)part, 64ULL << 20, &from, &to), E_OK);
	KT_ASSERT_EQ(from, 48ULL << 20);
	KT_ASSERT_EQ(to, 64ULL << 20);
	KT_ASSERT_ER(ts_ref_vol(vol, &rv), E_OK);
	KT_ASSERT_EQ(rv.blocks, ( 64ULL << 20 ) / 4096);
	KT_ASSERT_ER(obj_put(vol, 7, 12ULL << 20, &u), E_OK);
	KT_ASSERT(obj_same(vol, 7, 12ULL << 20, &u));
	KT_ASSERT(obj_same(vol, 0, OBJ_BYTES, &objs[0]));
	KT_ASSERT_ER(ts_cls_vol(vol), E_OK);
	KT_ASSERT_EQ(problems(part), 0);
	/* nothing after it to grow into on this table but the free stretch: grown to the end of it */
	KT_ASSERT_ER(knl_tsfs_part_grow((CONST char *)part, 64ULL << 20, &from, &to), E_LIMIT);
}

/*
 * The second partition, laid down by the test as an 8MB volume on an
 * 8MB partition, then the partition made 24MB: the volume is to grow.
 */
LOCAL ER cut_setup( PART *p, UB *part2 )
{
	ER	er;
	ID	vol;
	TS_UUID	u;

	p[1].first = 65 * MB;
	p[1].last = 73 * MB - 1;
	er = table_put(p, 2, G_SECT);
	if ( er >= E_OK ) er = rescan();
	if ( er >= E_OK ) er = ts_format_blk((CONST char *)part2, "GROWC");
	if ( er < E_OK ) return er;
	vol = ts_opn_vol((CONST char *)part2, TSFS_STORE_BLK);
	if ( vol <= 0 ) return E_OBJ;
	er = obj_put(vol, 3, OBJ_BYTES, &u);
	objs[1] = u;
	ts_cls_vol(vol);
	if ( er < E_OK ) return er;
	p[1].last = 89 * MB - 1;
	er = table_put(p, 2, G_SECT);
	if ( er >= E_OK ) er = rescan();
	return er;
}

LOCAL void test_cut_volume( void )
{
	T_TSFSBLK_SB	sb;
	PART		p[4];
	UB		part2[NAME_MAX];
	UD		w = 0, k, small = ( 8ULL << 20 ) / 4096, large = ( 24ULL << 20 ) / 4096;
	INT		n = 0, bad = 0, olds = 0, news = 0;
	ID		bv, vol;

	if ( !in ) KT_SKIP("no disk");
	KT_ASSERT(tables_ok(p, &n));
	if ( n < 1 ) return;
	knl_memset(p[1].uniq, 0x5A, 16);
	part_name(1, part2);

	/* how many writes the grow makes */
	KT_ASSERT_ER(cut_setup(p, part2), E_OK);
	bv = ts_opn_vol_blk((CONST char *)part2);
	KT_ASSERT(bv > 0);
	if ( bv <= 0 ) return;
	KT_ASSERT_ER(ts_cut_blk(bv, 0, NULL), E_OK);
	KT_ASSERT_ER(ts_grow_vol_blk(bv, 0, NULL, NULL), E_OK);
	KT_ASSERT_ER(ts_cut_blk(bv, 0, &w), E_OK);
	ts_cls_vol_blk(bv);
	KT_ASSERT(w > 5);
	tm_printf((UB *)"  the grow makes %d writes\n", (INT)w);

	for ( k = 1; k <= w; k++ ) {
		if ( cut_setup(p, part2) < E_OK ) { bad++; break; }
		bv = ts_opn_vol_blk((CONST char *)part2);
		if ( bv <= 0 ) { bad++; break; }
		ts_cut_blk(bv, k, NULL);
		(void)ts_grow_vol_blk(bv, 0, NULL, NULL);
		ts_cls_vol_blk(bv);		/* nothing more reaches the medium */

		bv = ts_opn_vol_blk((CONST char *)part2);	/* a grow cut short is finished here */
		if ( bv <= 0 ) { bad++; tm_printf((UB *)"  cut %d: no open\n", (INT)k); continue; }
		ts_ref_vol_blk(bv, &sb);
		ts_cls_vol_blk(bv);
		if ( ( sb.state & TSFSBLK_ST_ERROR ) != 0 || ( sb.incompat & TSFSBLK_INCOMPAT_GROWING ) != 0
		  || ( sb.total_blocks != small && sb.total_blocks != large ) ) {
			bad++;
			tm_printf((UB *)"  cut %d: state %x incompat %x, %d blocks\n", (INT)k, sb.state,
				  (UW)sb.incompat, (INT)sb.total_blocks);
			continue;
		}
		if ( sb.total_blocks == small ) olds++; else news++;
		vol = ts_opn_vol((CONST char *)part2, TSFS_STORE_BLK);
		if ( vol <= 0 || !obj_same(vol, 3, OBJ_BYTES, &objs[1]) ) {
			bad++;
			tm_printf((UB *)"  cut %d: the object is not there\n", (INT)k);
		} else {
			TS_UUID	u;

			if ( obj_put(vol, 4, 4ULL << 20, &u) < E_OK ) {
				bad++;
				tm_printf((UB *)"  cut %d: no room written\n", (INT)k);
			}
		}
		if ( vol > 0 ) ts_cls_vol(vol);
		if ( problems(part2) != 0 ) {
			bad++;
			tm_printf((UB *)"  cut %d: the check found something\n", (INT)k);
		}
	}
	tm_printf((UB *)"  %d cuts: %d as it was, %d grown\n", (INT)w, olds, news);
	KT_ASSERT_EQ(bad, 0);
	KT_ASSERT(news > 0);
}

LOCAL void test_cut_table( void )
{
	T_GPTPART	gp;
	PART		p[4], q[4];
	UB		part2[NAME_MAX];
	UW		w = 0, k;
	UD		alt;
	INT		n = 0, bad = 0, olds = 0, news = 0;

	if ( !in ) KT_SKIP("no disk");
	KT_ASSERT(tables_ok(p, &n));
	if ( n < 2 ) return;
	part_name(1, part2);
	p[1].last = 73 * MB - 1;

	/* how many writes a grow of the table makes, from a card's image */
	KT_ASSERT_ER(table_put(p, 2, 80 * MB), E_OK);
	KT_ASSERT(rescan() >= E_OK);
	knl_gpt_cut(0, NULL);
	KT_ASSERT_ER(knl_gpt_grow((CONST char *)part2, 81 * MB - 1), E_OK);
	knl_gpt_cut(0, &w);
	KT_ASSERT(w >= 4);
	tm_printf((UB *)"  the table's grow makes %d writes\n", (INT)w);

	for ( k = 1; k <= w; k++ ) {
		KT_ASSERT_ER(table_put(p, 2, 80 * MB), E_OK);
		if ( rescan() < E_OK ) { bad++; break; }
		knl_gpt_cut(k, NULL);
		(void)knl_gpt_grow((CONST char *)part2, 81 * MB - 1);
		knl_gpt_cut(0, NULL);
		/* the copy the kernel reads, and what it says */
		if ( knl_gpt_part((CONST char *)part2, &gp) < E_OK ) {
			bad++;
			tm_printf((UB *)"  cut %d: no table passes\n", (INT)k);
			continue;
		}
		if ( gp.last == 73 * MB - 1 ) {
			olds++;
		} else if ( gp.last == 81 * MB - 1 ) {
			news++;
		} else {
			bad++;
			tm_printf((UB *)"  cut %d: the partition ends at %d\n", (INT)k, (INT)gp.last);
		}
		/* the start's own copy passes, or names the backup that does */
		if ( !table_copy(1, q, &n, &alt) && !table_copy(G_SECT - 1, q, &n, &alt) ) {
			bad++;
			tm_printf((UB *)"  cut %d: neither copy passes\n", (INT)k);
		}
	}
	tm_printf((UB *)"  %d cuts: %d as it was, %d grown\n", (INT)w, olds, news);
	KT_ASSERT_EQ(bad, 0);
	KT_ASSERT(olds > 0);
	KT_ASSERT(news > 0);
	/* written whole at last: the driver follows */
	KT_ASSERT_ER(table_put(p, 2, G_SECT), E_OK);
	KT_ASSERT(rescan() >= E_OK);
	KT_ASSERT_ER(knl_gpt_grow((CONST char *)part2, 81 * MB - 1), E_OK);
	KT_ASSERT_EQ(dev_sect(part2), 16 * MB);
	KT_ASSERT(tables_ok(q, &n));
	KT_ASSERT_EQ(n, 2);
}

/* What the start did with the system volume */
LOCAL void test_sysvol( void )
{
	T_GPTPART	gp;
	UD		from = 0, to = 0;

	knl_sysvol_grown(&from, &to);
	tm_printf((UB *)"  the system volume: %d MB -> %d MB at the start\n", (INT)( from >> 20 ), (INT)( to >> 20 ));
#ifdef CNF_OB_SYSVOL
	if ( knl_gpt_part(CNF_OB_SYSVOL, &gp) >= E_OK ) {
		KT_ASSERT(to <= ( gp.last - gp.first + 1 ) * 512);
		KT_ASSERT(from <= to);
		if ( to > from ) {
			KT_ASSERT(gp.tail);
		}
	}
#endif
}

LOCAL void test_out( void )
{
	if ( !in ) KT_SKIP("no disk");
	KT_ASSERT(unplug());
}

EXPORT void ktest_grow( void )
{
	names = (UB *)Kmalloc((SZ)NAME_MAX * LIST_MAX);
	if ( names == NULL ) return;
	KT_RUN(test_plug);
	KT_RUN(test_fresh);
	KT_RUN(test_image);
	KT_RUN(test_live);
	KT_RUN(test_cut_volume);
	KT_RUN(test_cut_table);
	KT_RUN(test_sysvol);
	KT_RUN(test_out);
	Kfree(names);
}
