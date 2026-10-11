/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ktest_board.c
 *	Which board the system takes itself to be on (include/ts/board.h,
 *	kernel/sysdepend/cpu/core/armv8a/board.c): the root node of a device
 *	tree read out of the bytes of one, the kinds the compatible, the
 *	model and the firmware's board revision name, what each kind tells
 *	the drivers, and the word of the command line that moves the
 *	console.
 *
 *	QEMU has no Compute Module 5, so the device trees are made here, in
 *	memory, with the root node the firmware's files have: the Raspberry
 *	Pi 5's, the Compute Module 5's and its Lite's, the Raspberry Pi 4's,
 *	the 400's and the Compute Module 4's, QEMU's own, and one the table
 *	does not know. Each has a child node after the root's own properties
 *	with a compatible of its own, which the reading must not take for
 *	the root's.
 *
 *	The device tree the machine came up with is asked as a driver asks
 *	it (knl_dtb_prop, knl_dtb_ranges): the root's compatible, and a
 *	ranges or dma-ranges property decoded with the cells of its node and
 *	of the parent, whatever the board.
 */

#include "ktest.h"
#include "tstdlib.h"
#include <ts/board.h>

IMPORT ER knl_dtb_root( CONST UB *fdt, UB *compat, INT cmax, INT *p_clen, UB *model, INT mmax );
IMPORT CONST UB	*knl_dtb_prop( CONST char *path, CONST char *prop, INT *p_len );
IMPORT UW	knl_dtb_u32( CONST char *path, CONST char *prop, UW dflt );
IMPORT BOOL	knl_dtb_okay( CONST char *path );
IMPORT ER	knl_dtb_ranges( CONST char *path, CONST char *prop, INT index,
				UD *p_child, UD *p_parent, UD *p_size );
IMPORT UB	knl_dtb_compat[];
IMPORT INT	knl_dtb_compat_len;

/* ---------------------------------------------------------------- a device tree in memory */

#define FDT_MAGIC	0xd00dfeed
#define FDT_BEGIN_NODE	1
#define FDT_END_NODE	2
#define FDT_PROP	3
#define FDT_END		9

#define FDT_MAX		1024
#define STR_MAX		128

typedef struct {
	UB	st[FDT_MAX];		/* the structure block */
	INT	sn;
	UB	str[STR_MAX];		/* the strings block */
	INT	strn;
	UB	out[FDT_MAX + STR_MAX + 64];
} FDTB;

LOCAL FDTB	fb;

LOCAL void put32( UB *p, UW v )
{
	p[0] = (UB)( v >> 24 );
	p[1] = (UB)( v >> 16 );
	p[2] = (UB)( v >> 8 );
	p[3] = (UB)v;
}

LOCAL void st32( UW v )
{
	put32(fb.st + fb.sn, v);
	fb.sn += 4;
}

LOCAL void st_bytes( CONST UB *p, INT len )
{
	INT	i;

	for ( i = 0; i < len; i++ ) fb.st[fb.sn++] = p[i];
	while ( ( fb.sn & 3 ) != 0 ) fb.st[fb.sn++] = 0;
}

LOCAL INT str_len( CONST char *s )
{
	INT	n = 0;

	while ( s[n] != '\0' ) n++;
	return n;
}

LOCAL BOOL mem_eq( CONST UB *a, CONST UB *b, INT n )
{
	while ( n-- > 0 ) {
		if ( *a++ != *b++ ) return FALSE;
	}
	return TRUE;
}

/* where a property's name is in the strings block, adding it when new */
LOCAL INT str_off( CONST char *name )
{
	INT	i, n = str_len(name);

	for ( i = 0; i + n < fb.strn; i++ ) {
		if ( mem_eq(fb.str + i, (CONST UB *)name, n + 1) ) return i;
	}
	i = fb.strn;
	knl_memcpy(fb.str + i, name, (SZ)n + 1);
	fb.strn += n + 1;
	return i;
}

LOCAL void begin( CONST char *name )
{
	st32(FDT_BEGIN_NODE);
	st_bytes((CONST UB *)name, str_len(name) + 1);
}

LOCAL void prop( CONST char *name, CONST void *val, INT len )
{
	st32(FDT_PROP);
	st32((UW)len);
	st32((UW)str_off(name));
	st_bytes((CONST UB *)val, len);
}

LOCAL void prop_u32( CONST char *name, UW v )
{
	UB	b[4];

	put32(b, v);
	prop(name, b, 4);
}

/*
 * A tree with the root node the firmware's file for a board has:
 * #address-cells, compatible (a list, 'clen' bytes), model, and then the
 * nodes /chosen and /soc, the latter with a compatible of its own.
 */
LOCAL CONST UB *make_fdt( CONST char *compat, INT clen, CONST char *model )
{
	UW	off_st = 40 + 16, off_str, total;

	knl_memset(&fb, 0, sizeof(fb));
	begin("");
	prop_u32("#address-cells", 2);
	prop("compatible", compat, clen);
	if ( model != NULL ) {
		prop("model", model, str_len(model) + 1);
	}
	begin("chosen");
	prop("bootargs", "console=serial0", 16);
	st32(FDT_END_NODE);
	begin("soc@107c000000");
	prop("compatible", "simple-bus", 11);
	st32(FDT_END_NODE);
	st32(FDT_END_NODE);
	st32(FDT_END);

	off_str = off_st + (UW)fb.sn;
	total = off_str + (UW)fb.strn;
	put32(fb.out + 0, FDT_MAGIC);
	put32(fb.out + 4, total);
	put32(fb.out + 8, off_st);
	put32(fb.out + 12, off_str);
	put32(fb.out + 16, 40);			/* the memory reservation block: empty */
	put32(fb.out + 20, 17);			/* version */
	put32(fb.out + 24, 16);			/* last compatible version */
	put32(fb.out + 28, 0);
	put32(fb.out + 32, (UW)fb.strn);
	put32(fb.out + 36, (UW)fb.sn);
	knl_memcpy(fb.out + off_st, fb.st, (SZ)fb.sn);
	knl_memcpy(fb.out + off_str, fb.str, (SZ)fb.strn);
	return fb.out;
}

/* The root nodes of the firmware's device trees (bcm2712-rpi-*.dtb) */
#define C_PI5		"raspberrypi,5-model-b\0brcm,bcm2712"
#define C_CM5		"raspberrypi,5-compute-module\0brcm,bcm2712"
#define C_QEMU		"linux,dummy-virt"
#define C_PI500		"raspberrypi,500\0brcm,bcm2712"
#define M_PI5		"Raspberry Pi 5"
#define M_CM5		"Raspberry Pi Compute Module 5"
#define M_CM5L		"Raspberry Pi Compute Module 5 Lite"
#define M_PI500		"Raspberry Pi 500"

/* ... and of bcm2711-rpi-*.dtb */
#define C_PI4B		"raspberrypi,4-model-b\0brcm,bcm2711"
#define C_PI400		"raspberrypi,400\0brcm,bcm2711"
#define C_CM4		"raspberrypi,4-compute-module\0brcm,bcm2711"
#define M_PI4B		"Raspberry Pi 4 Model B"
#define M_PI400		"Raspberry Pi 400"
#define M_CM4		"Raspberry Pi Compute Module 4"

LOCAL BOOL same_str( CONST UB *a, CONST char *b )
{
	while ( *a != '\0' && *a == (UB)*b ) {
		a++;
		b++;
	}
	return ( *a == (UB)*b );
}

/* The kind a made tree names, read back through knl_dtb_root as at boot */
LOCAL UW kind_of( CONST char *compat, INT clen, CONST char *model )
{
	UB	c[128], m[64];
	INT	n = 0;

	if ( knl_dtb_root(make_fdt(compat, clen, model), c, sizeof(c), &n, m, sizeof(m)) < E_OK ) {
		return 0xFFFF;
	}
	return knl_board_by_dt(c, n, m);
}

/* ---------------------------------------------------------------- tests */

/* The root's compatible and model, not the child nodes' */
LOCAL void test_dt_root( void )
{
	UB	c[128], m[64], small[40];
	INT	n = -1;

	KT_ASSERT_ER(knl_dtb_root(make_fdt(C_CM5, sizeof(C_CM5), M_CM5L), c, sizeof(c), &n, m, sizeof(m)), E_OK);
	KT_ASSERT_EQ(n, sizeof(C_CM5));
	KT_ASSERT(mem_eq(c, (CONST UB *)C_CM5, sizeof(C_CM5)));
	KT_ASSERT(same_str(m, M_CM5L));

	/* no model: empty, and the compatible still read */
	KT_ASSERT_ER(knl_dtb_root(make_fdt(C_QEMU, sizeof(C_QEMU), NULL), c, sizeof(c), &n, m, sizeof(m)), E_OK);
	KT_ASSERT_EQ(n, sizeof(C_QEMU));
	KT_ASSERT_EQ(m[0], 0);

	/* a list longer than the room: cut after its last whole string */
	KT_ASSERT_ER(knl_dtb_root(make_fdt(C_CM5, sizeof(C_CM5), M_CM5), small, sizeof(small), &n, m, sizeof(m)), E_OK);
	KT_ASSERT_EQ(n, str_len("raspberrypi,5-compute-module") + 1);
	KT_ASSERT(same_str(small, "raspberrypi,5-compute-module"));

	/* not a device tree */
	knl_memset(c, 0, sizeof(c));
	KT_ASSERT_ER(knl_dtb_root(c, c, sizeof(c), &n, m, sizeof(m)), E_NOEXS);
	KT_ASSERT_EQ(n, 0);
}

/* The kinds the root nodes of the firmware's files name */
LOCAL void test_by_dt( void )
{
	KT_ASSERT_EQ(kind_of(C_PI5, sizeof(C_PI5), M_PI5), BOARD_RPI5);
	KT_ASSERT_EQ(kind_of(C_CM5, sizeof(C_CM5), M_CM5), BOARD_CM5);
	KT_ASSERT_EQ(kind_of(C_CM5, sizeof(C_CM5), M_CM5L), BOARD_CM5LITE);
	KT_ASSERT_EQ(kind_of(C_CM5, sizeof(C_CM5), NULL), BOARD_CM5);
	KT_ASSERT_EQ(kind_of(C_QEMU, sizeof(C_QEMU), NULL), BOARD_QEMU_VIRT);
	KT_ASSERT_EQ(kind_of(C_PI500, sizeof(C_PI500), M_PI500), BOARD_UNKNOWN);
	KT_ASSERT_EQ(kind_of(C_PI4B, sizeof(C_PI4B), M_PI4B), BOARD_RPI4B);
	KT_ASSERT_EQ(kind_of(C_PI400, sizeof(C_PI400), M_PI400), BOARD_RPI400);
	KT_ASSERT_EQ(kind_of(C_CM4, sizeof(C_CM4), M_CM4), BOARD_CM4);

	/* the SoC an image is built for, looked for in the list */
	KT_ASSERT(knl_board_has_compat((CONST UB *)C_PI4B, sizeof(C_PI4B), "brcm,bcm2711"));
	KT_ASSERT(knl_board_has_compat((CONST UB *)C_CM5, sizeof(C_CM5), "brcm,bcm2712"));
	KT_ASSERT(!knl_board_has_compat((CONST UB *)C_CM4, sizeof(C_CM4), "brcm,bcm2712"));
	KT_ASSERT(!knl_board_has_compat((CONST UB *)C_QEMU, sizeof(C_QEMU), "brcm,bcm2711"));
	KT_ASSERT(!knl_board_has_compat((CONST UB *)"brcm,bcm27", 10, "brcm,bcm27"));

	/* the board's string need not be first; the SoC's alone names none */
	KT_ASSERT_EQ(knl_board_by_dt((CONST UB *)"vendor,carrier\0raspberrypi,5-compute-module",
				     sizeof("vendor,carrier\0raspberrypi,5-compute-module"),
				     (CONST UB *)M_CM5), BOARD_CM5);
	KT_ASSERT_EQ(knl_board_by_dt((CONST UB *)"brcm,bcm2712", sizeof("brcm,bcm2712"), NULL),
		     BOARD_UNKNOWN);
	/* a string not ended inside the list is not taken */
	KT_ASSERT_EQ(knl_board_by_dt((CONST UB *)"raspberrypi,5-model-b", 21, NULL), BOARD_UNKNOWN);
	KT_ASSERT_EQ(knl_board_by_dt((CONST UB *)"", 0, NULL), BOARD_UNKNOWN);
}

/* The kinds the firmware's board revisions name (Raspberry Pi revision codes) */
LOCAL void test_by_rev( void )
{
	KT_ASSERT_EQ(knl_board_by_rev(0xd04170), BOARD_RPI5);		/* 8GB */
	KT_ASSERT_EQ(knl_board_by_rev(0xc04170), BOARD_RPI5);		/* 4GB */
	KT_ASSERT_EQ(knl_board_by_rev(0xb04171), BOARD_RPI5);		/* 2GB, D0 */
	KT_ASSERT_EQ(knl_board_by_rev(0xd04180), BOARD_CM5);
	KT_ASSERT_EQ(knl_board_by_rev(0xb04180), BOARD_CM5);
	KT_ASSERT_EQ(knl_board_by_rev(0xd041a0), BOARD_CM5LITE);
	KT_ASSERT_EQ(knl_board_by_rev(0xe041a0), BOARD_CM5LITE);
	KT_ASSERT_EQ(knl_board_by_rev(0xd04190), BOARD_UNKNOWN);	/* 500 */
	KT_ASSERT_EQ(knl_board_by_rev(0xc03111), BOARD_RPI4B);		/* 4B 4GB, 1.1 */
	KT_ASSERT_EQ(knl_board_by_rev(0xd03115), BOARD_RPI4B);		/* 4B 8GB, 1.5 */
	KT_ASSERT_EQ(knl_board_by_rev(0xc03130), BOARD_RPI400);
	KT_ASSERT_EQ(knl_board_by_rev(0xb03140), BOARD_CM4);
	KT_ASSERT_EQ(knl_board_by_rev(0xd03141), BOARD_CM4);
	KT_ASSERT_EQ(knl_board_by_rev(0xa03150), BOARD_UNKNOWN);	/* CM4S */
	KT_ASSERT_EQ(knl_board_by_rev(0x000e), BOARD_UNKNOWN);		/* old style */
	KT_ASSERT_EQ(knl_board_by_rev(0), BOARD_UNKNOWN);
}

/* What each kind tells the drivers */
LOCAL void test_set( void )
{
	T_BOARD	keep = knl_board;

	knl_board_set(BOARD_CM5, 0xd04180);
	KT_ASSERT_EQ(knl_board.kind, BOARD_CM5);
	KT_ASSERT_EQ(knl_board.rev, 0xd04180);
	KT_ASSERT(same_str((CONST UB *)knl_board.name, M_CM5));
	KT_ASSERT_EQ(knl_board.sd, BOARD_SD_EMMC);
	KT_ASSERT_EQ(knl_board.sd_width, 8);
	KT_ASSERT(knl_board.sd_1v8);
	KT_ASSERT_EQ(knl_board.eth_phy, 0);
	KT_ASSERT_EQ(knl_board.aon_own, ( 1 << 5 ) | ( 1 << 6 ));	/* the antenna select */

	knl_board_set(BOARD_CM5LITE, 0xd041a0);
	KT_ASSERT_EQ(knl_board.kind, BOARD_CM5LITE);
	KT_ASSERT(same_str((CONST UB *)knl_board.name, M_CM5L));
	KT_ASSERT_EQ(knl_board.sd, BOARD_SD_NOCD);
	KT_ASSERT(!knl_board.sd_1v8);
	KT_ASSERT_EQ(knl_board.eth_phy, 0);
	KT_ASSERT_EQ(knl_board.aon_own, ( 1 << 5 ) | ( 1 << 6 ));

	knl_board_set(BOARD_RPI5, 0xd04170);
	KT_ASSERT_EQ(knl_board.kind, BOARD_RPI5);
	KT_ASSERT(same_str((CONST UB *)knl_board.name, M_PI5));
	KT_ASSERT_EQ(knl_board.sd, BOARD_SD_CD);
	KT_ASSERT_EQ(knl_board.sd_width, 4);
	KT_ASSERT(!knl_board.sd_1v8);
	KT_ASSERT_EQ(knl_board.eth_phy, 1);
	KT_ASSERT_EQ(knl_board.aon_own, 1 << 5);			/* the card detect */

	knl_board_set(BOARD_CM4, 0xd03141);
	KT_ASSERT_EQ(knl_board.kind, BOARD_CM4);
	KT_ASSERT(same_str((CONST UB *)knl_board.name, M_CM4));
	KT_ASSERT_EQ(knl_board.sd, BOARD_SD_NOCD);		/* broken-cd: looked for */
	KT_ASSERT_EQ(knl_board.sd_width, 8);
	KT_ASSERT(!knl_board.sd_1v8);
	KT_ASSERT_EQ(knl_board.eth_phy, 0);
	KT_ASSERT(!knl_board.led_power);

	knl_board_set(BOARD_RPI400, 0xc03130);
	KT_ASSERT(same_str((CONST UB *)knl_board.name, M_PI400));
	KT_ASSERT_EQ(knl_board.sd, BOARD_SD_NOCD);
	KT_ASSERT_EQ(knl_board.sd_width, 4);
	KT_ASSERT_EQ(knl_board.eth_phy, 1);
	KT_ASSERT(knl_board.led_power);			/* its one LED is the power LED */

	knl_board_set(BOARD_RPI4B, 0xd03115);
	KT_ASSERT(same_str((CONST UB *)knl_board.name, M_PI4B));
	KT_ASSERT_EQ(knl_board.eth_phy, 1);
	KT_ASSERT_EQ(knl_board.aon_own, 0);
	KT_ASSERT(!knl_board.led_power);

	/* a revision the table knows decides over the device tree's kind ... */
	knl_board_set(BOARD_CM5, 0);
	knl_board_set_rev(0xd041a0);
	KT_ASSERT_EQ(knl_board.kind, BOARD_CM5LITE);
	KT_ASSERT_EQ(knl_board.rev, 0xd041a0);
	/* ... and one it does not know leaves the kind as it was */
	knl_board_set(BOARD_RPI5, 0);
	knl_board_set_rev(0xd04190);
	KT_ASSERT_EQ(knl_board.kind, BOARD_RPI5);
	KT_ASSERT_EQ(knl_board.rev, 0xd04190);

	/* an unknown kind behaves as the build's own board and still has a name */
	knl_board_set(BOARD_UNKNOWN, 0);
	KT_ASSERT_EQ(knl_board.kind, BOARD_UNKNOWN);
	KT_ASSERT(knl_board.name != NULL);
#if defined(QEMU_VIRT)
	KT_ASSERT_EQ(knl_board.sd, BOARD_SD_PCI);
#elif defined(RPI4)
	KT_ASSERT_EQ(knl_board.sd, BOARD_SD_NOCD);
#else
	KT_ASSERT_EQ(knl_board.sd, BOARD_SD_CD);
#endif

	knl_board = keep;
}

/* The word of the command line that moves the console */
LOCAL void test_bootarg( void )
{
	UB	v[8];

	KT_ASSERT(knl_bootarg((CONST UB *)"reboot=w coherent_pool=1M tessronos.console=rp1 quiet",
			      "tessronos.console", v, sizeof(v)));
	KT_ASSERT(same_str(v, "rp1"));
	KT_ASSERT(knl_bootarg((CONST UB *)"tessronos.console=debug\n", "tessronos.console", v, sizeof(v)));
	KT_ASSERT(same_str(v, "debug"));
	/* a word that only begins or ends like it is another word */
	KT_ASSERT(!knl_bootarg((CONST UB *)"xtessronos.console=rp1 tessronos.consolex=rp1",
			       "tessronos.console", v, sizeof(v)));
	KT_ASSERT(knl_bootarg((CONST UB *)"xtessronos.console=a  tessronos.console=rp1",
			      "tessronos.console", v, sizeof(v)));
	KT_ASSERT(same_str(v, "rp1"));
	/* a value longer than the room is cut, not overrun */
	KT_ASSERT(knl_bootarg((CONST UB *)"tessronos.console=abcdefghijk", "tessronos.console", v, 4));
	KT_ASSERT(same_str(v, "abc"));
	KT_ASSERT(knl_bootarg((CONST UB *)"tessronos.console=", "tessronos.console", v, sizeof(v)));
	KT_ASSERT_EQ(v[0], 0);
	KT_ASSERT(!knl_bootarg((CONST UB *)"", "tessronos.console", v, sizeof(v)));
}

/* This machine as it came up */
LOCAL void test_this( void )
{
	tm_printf((UB *)"  board %s, kind %d, revision %x\n",
		  knl_board.name, (INT)knl_board.kind, knl_board.rev);
	KT_ASSERT(knl_board.name != NULL);
#if defined(QEMU_VIRT)
	KT_ASSERT_EQ(knl_board.kind, BOARD_QEMU_VIRT);
	KT_ASSERT(same_str((CONST UB *)knl_board.name, "QEMU virt"));
	KT_ASSERT_EQ(knl_board.sd, BOARD_SD_PCI);
	KT_ASSERT(!knl_board.console_rp1);
#elif defined(RPI4)
	KT_ASSERT(knl_board.kind == BOARD_RPI4B || knl_board.kind == BOARD_RPI400
		  || knl_board.kind == BOARD_CM4 || knl_board.kind == BOARD_UNKNOWN);
	KT_ASSERT(!knl_board.console_rp1);
#else
	KT_ASSERT(knl_board.kind == BOARD_RPI5 || knl_board.kind == BOARD_CM5
		  || knl_board.kind == BOARD_CM5LITE || knl_board.kind == BOARD_UNKNOWN);
	KT_ASSERT(knl_board.rev != 0);
#endif
}

/* The device tree the machine came up with, asked as a driver asks it */
LOCAL void test_dt_prop( void )
{
	INT		len = -1;
	CONST UB	*v;
	UD		child = 1, parent = 1, size = 1;

	v = knl_dtb_prop("/", "compatible", &len);
	KT_ASSERT(v != NULL);
	KT_ASSERT(len > 0 && v[len - 1] == '\0');
	KT_ASSERT(mem_eq(v, knl_dtb_compat, len < knl_dtb_compat_len ? len : knl_dtb_compat_len));
	KT_ASSERT_EQ(knl_dtb_u32("/", "#address-cells", 0), 2);
	KT_ASSERT_EQ(knl_dtb_u32("/", "no-such-property", 77), 77);
	KT_ASSERT(knl_dtb_prop("/no-such-node", "compatible", &len) == NULL);
	KT_ASSERT_EQ(len, 0);
	KT_ASSERT(knl_dtb_prop("/chosen/no-such-node", "bootargs", NULL) == NULL);
	KT_ASSERT_ER(knl_dtb_ranges("/no-such-node", "ranges", 0, &child, &parent, &size), E_NOEXS);
	KT_ASSERT(knl_dtb_okay("/"));
	KT_ASSERT(!knl_dtb_okay("/no-such-node"));
#if defined(QEMU_VIRT)
	/*
	 * The PCIe host's second window: PCI memory 0x1000_0000 at the same
	 * physical address. Its child addresses are three cells, the first
	 * the kind, which is dropped.
	 */
	KT_ASSERT_ER(knl_dtb_ranges("/pcie", "ranges", 1, &child, &parent, &size), E_OK);
	tm_printf((UB *)"  pcie ranges[1] %x%08x -> %x%08x size %x%08x\n",
		  (UW)( child >> 32 ), (UW)child, (UW)( parent >> 32 ), (UW)parent,
		  (UW)( size >> 32 ), (UW)size);
	KT_ASSERT_EQ(child, 0x10000000);
	KT_ASSERT_EQ(parent, 0x10000000);
	KT_ASSERT(size >= 0x10000000);
	KT_ASSERT_ER(knl_dtb_ranges("/pcie", "ranges", 9, &child, &parent, &size), E_NOEXS);
#elif defined(RPI4)
	/* the EMMC2's view of memory: the low 1GB at 0xC000_0000, or (C0) all of it from 0 */
	KT_ASSERT_ER(knl_dtb_ranges("/emmc2bus", "dma-ranges", 0, &child, &parent, &size), E_OK);
	tm_printf((UB *)"  emmc2bus dma-ranges %08x -> %08x size %08x\n",
		  (UW)child, (UW)parent, (UW)size);
	KT_ASSERT(( child == 0xC0000000 && parent == 0 && size == 0x40000000 )
		  || ( child == 0 && parent == 0 && size >= 0x40000000 ));
	/* the PCIe link's outbound window */
	if ( knl_dtb_ranges("/scb/pcie@7d500000", "ranges", 0, &child, &parent, &size) >= E_OK ) {
		KT_ASSERT_EQ(child, 0xC0000000);
		KT_ASSERT_EQ(parent, 0x600000000ULL);
		KT_ASSERT_EQ(size, 0x40000000);
	}
#endif
}

EXPORT void ktest_board( void )
{
	KT_RUN(test_dt_root);
	KT_RUN(test_by_dt);
	KT_RUN(test_by_rev);
	KT_RUN(test_set);
	KT_RUN(test_bootarg);
	KT_RUN(test_this);
	KT_RUN(test_dt_prop);
}
