/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	board.c (ARMv8-A / AArch64)
 *	Which board this is, and what the drivers have to know of it
 *	(include/ts/board.h, docs/tessronos/03-platform.md 3.5).
 *
 *	The device tree read at boot names the board in its root node; on a
 *	Raspberry Pi the firmware's board revision, asked for later
 *	(kernel/sysdepend/rpi5/devinit.c), names it again and decides: the
 *	device tree is whichever file config.txt picked, the revision is
 *	burnt into the board.
 *
 *	What differs between the Raspberry Pi 5 and the Compute Module 5,
 *	as their device trees and the module's datasheet give it:
 *	  - the SD host controller (sdio1) has the card slot on the Pi 5,
 *	    with its detect switch on always-on pin 5; the module's eMMC,
 *	    8 lines at a fixed 1.8V and nothing to detect, on the CM5; the
 *	    carrier's slot without a detect line on the CM5 Lite, whose
 *	    lines switch between 3.3V and 1.8V as the Pi 5's do
 *	  - always-on pins 5 and 6 select the Wi-Fi antenna on the module
 *	    and are the board's, not a card detect
 *	  - the Ethernet PHY answers at MDIO address 1 on the Pi 5 and at 0
 *	    on the module
 *
 *	The Raspberry Pi 4 family has an image of its own (TARGET=_RPI4_)
 *	but the same way of telling its boards apart. Of what the drivers
 *	need, the Raspberry Pi 4 Model B and the Raspberry Pi 400 differ only
 *	in their name and in what the LED on GPIO 42 is (the activity LED,
 *	the 400's power LED); the Compute Module 4 has its eMMC, or with the
 *	Lite the carrier's SD slot, on the same controller with eight data
 *	lines wired, and its PHY at MDIO address 0. None of them has a card
 *	detect line the controller can use (broken-cd).
 *	The decoders here are plain functions of their arguments so that the
 *	tests can give them any device tree and any revision.
 */

#include <sys/machine.h>

#ifdef CPU_CORE_ARMV8A

#include "kernel.h"
#include "../../../sysdepend.h"
#include <tm/tmonitor.h>
#include <ts/board.h>

EXPORT T_BOARD	knl_board;

/*
 * The boards known: the root compatible that names each (the first
 * string of the list; the model tells the Lite apart), the board type of
 * the revision, and what the drivers need.
 */
typedef struct {
	UW		kind;
	CONST char	*compat;
	UW		rev_type;	/* bits 4-11 of a new style revision, 0xFFFF none */
	CONST char	*name;
	UB		sd;
	UB		sd_width;
	BOOL		sd_1v8;
	UB		eth_phy;
	UW		aon_own;
	BOOL		led_power;	/* the board's one LED is its power LED: left on */
} BOARD_DEF;

#define AON_PIN(n)	( 1UL << (n) )

LOCAL CONST BOARD_DEF	board_def[] = {
	{ BOARD_QEMU_VIRT, "linux,dummy-virt", 0xFFFF, "QEMU virt",
	  BOARD_SD_PCI, 8, FALSE, 0, 0, FALSE },
	{ BOARD_RPI5, "raspberrypi,5-model-b", 0x17, "Raspberry Pi 5",
	  BOARD_SD_CD, 4, FALSE, 1, AON_PIN(5), FALSE },
	{ BOARD_CM5, "raspberrypi,5-compute-module", 0x18, "Raspberry Pi Compute Module 5",
	  BOARD_SD_EMMC, 8, TRUE, 0, AON_PIN(5) | AON_PIN(6), FALSE },
	{ BOARD_CM5LITE, "raspberrypi,5-compute-module", 0x1a, "Raspberry Pi Compute Module 5 Lite",
	  BOARD_SD_NOCD, 4, FALSE, 0, AON_PIN(5) | AON_PIN(6), FALSE },
	{ BOARD_RPI4B, "raspberrypi,4-model-b", 0x11, "Raspberry Pi 4 Model B",
	  BOARD_SD_NOCD, 4, FALSE, 1, 0, FALSE },
	{ BOARD_RPI400, "raspberrypi,400", 0x13, "Raspberry Pi 400",
	  BOARD_SD_NOCD, 4, FALSE, 1, 0, TRUE },
	{ BOARD_CM4, "raspberrypi,4-compute-module", 0x14, "Raspberry Pi Compute Module 4",
	  BOARD_SD_NOCD, 8, FALSE, 0, 0, FALSE },
};
#define N_BOARD_DEF	( sizeof(board_def) / sizeof(board_def[0]) )

/* The board a build falls back on when it is told nothing it knows */
#if defined(RPI5)
#define BOARD_DEFAULT	BOARD_RPI5
#elif defined(RPI4)
#define BOARD_DEFAULT	BOARD_RPI4B
#else
#define BOARD_DEFAULT	BOARD_QEMU_VIRT
#endif

/*
 * The SoC an image is built for, as the root compatible names it. A
 * device tree that names another one belongs to another board: its
 * memory, its peripherals and its interrupts are not where this image
 * looks for them.
 */
#if defined(RPI5)
#define BOARD_SOC	"brcm,bcm2712"
#elif defined(RPI4)
#define BOARD_SOC	"brcm,bcm2711"
#endif

LOCAL BOOL same( CONST char *a, CONST char *b )
{
	while ( *a != '\0' && *a == *b ) {
		a++;
		b++;
	}
	return ( *a == *b );
}

/* Whether 'word' is in 's' */
LOCAL BOOL contains( CONST UB *s, CONST char *word )
{
	INT	i;

	for ( ; *s != '\0'; s++ ) {
		for ( i = 0; word[i] != '\0' && s[i] == (UB)word[i]; i++ ) ;
		if ( word[i] == '\0' ) {
			return TRUE;
		}
	}
	return FALSE;
}

EXPORT UW knl_board_by_dt( CONST UB *compat, INT len, CONST UB *model )
{
	INT	at = 0, i;

	/* each string of the list in turn, the most particular first */
	while ( at < len ) {
		CONST char	*one = (CONST char *)compat + at;
		INT		n;

		for ( n = 0; at + n < len && compat[at + n] != '\0'; n++ ) ;
		if ( at + n >= len ) {
			break;				/* not terminated inside the list */
		}
		for ( i = 0; i < (INT)N_BOARD_DEF; i++ ) {
			if ( !same(board_def[i].compat, one) ) {
				continue;
			}
			/* the module and its Lite share a compatible; the model says which */
			if ( board_def[i].kind == BOARD_CM5 || board_def[i].kind == BOARD_CM5LITE ) {
				return ( model != NULL && contains(model, "Lite") ) ? BOARD_CM5LITE : BOARD_CM5;
			}
			return board_def[i].kind;
		}
		at += n + 1;
	}
	return BOARD_UNKNOWN;
}

/*
 * A new style revision has bit 23 set; the board type is bits 4 to 11
 * (Raspberry Pi revision codes).
 */
#define REV_NEW_STYLE	0x00800000U
#define REV_TYPE(r)	( ( (r) >> 4 ) & 0xFF )

EXPORT UW knl_board_by_rev( UW rev )
{
	INT	i;

	if ( ( rev & REV_NEW_STYLE ) == 0 ) {
		return BOARD_UNKNOWN;
	}
	for ( i = 0; i < (INT)N_BOARD_DEF; i++ ) {
		if ( board_def[i].rev_type == REV_TYPE(rev) ) {
			return board_def[i].kind;
		}
	}
	return BOARD_UNKNOWN;
}

EXPORT void knl_board_set( UW kind, UW rev )
{
	CONST BOARD_DEF	*d = NULL, *dflt = NULL;
	BOOL		con = knl_board.console_rp1;
	INT		i;

	for ( i = 0; i < (INT)N_BOARD_DEF; i++ ) {
		if ( board_def[i].kind == kind ) d = &board_def[i];
		if ( board_def[i].kind == BOARD_DEFAULT ) dflt = &board_def[i];
	}
	if ( d == NULL ) {
		d = dflt;		/* unknown: behave as the build's own board */
	}
	knl_board.kind     = ( kind == BOARD_UNKNOWN ) ? BOARD_UNKNOWN : d->kind;
	knl_board.rev      = rev;
	knl_board.name     = d->name;
	knl_board.sd       = d->sd;
	knl_board.sd_width = d->sd_width;
	knl_board.sd_1v8   = d->sd_1v8;
	knl_board.eth_phy  = d->eth_phy;
	knl_board.aon_own  = d->aon_own;
	knl_board.led_power = d->led_power;
	knl_board.console_rp1 = con;
	if ( kind == BOARD_UNKNOWN && knl_dtb_model[0] != '\0' ) {
		knl_board.name = (CONST char *)knl_dtb_model;
	}
}

EXPORT BOOL knl_bootarg( CONST UB *args, CONST char *key, UB *val, INT max )
{
	CONST UB	*p = args;
	INT		i, n;

	while ( *p != '\0' ) {
		while ( *p == ' ' || *p == '\t' || *p == '\n' ) p++;
		for ( i = 0; key[i] != '\0' && p[i] == (UB)key[i]; i++ ) ;
		if ( key[i] == '\0' && p[i] == '=' ) {
			p += i + 1;
			for ( n = 0; p[n] != '\0' && p[n] != ' ' && p[n] != '\t' && p[n] != '\n'; n++ ) {
				if ( n < max - 1 ) val[n] = p[n];
			}
			if ( max > 0 ) val[( n < max - 1 ) ? n : max - 1] = '\0';
			return TRUE;
		}
		while ( *p != '\0' && *p != ' ' && *p != '\t' && *p != '\n' ) p++;
	}
	return FALSE;
}

/*
 * tessronos.console=rp1 on the command line (cmdline.txt, which the
 * firmware puts into /chosen/bootargs) asks for the console on UART0 of
 * the RP1, the header's pins 8 and 10. It is only a request: the port
 * has a clock only when config.txt also has enable_rp1_uart=1, which is
 * why the two are written together (etc/boot/config.txt).
 */
EXPORT void knl_board_init( void )
{
	UB	v[8];

	knl_memset(&knl_board, 0, sizeof(knl_board));
	if ( knl_bootarg(knl_bootargs, "tessronos.console", v, sizeof(v)) ) {
		knl_board.console_rp1 = same((CONST char *)v, "rp1");
	}
	knl_board_set(knl_board_by_dt(knl_dtb_compat, knl_dtb_compat_len, knl_dtb_model), 0);
#ifdef BOARD_SOC
	if ( knl_dtb_compat_len > 0 && !knl_board_has_compat(knl_dtb_compat, knl_dtb_compat_len, BOARD_SOC) ) {
		tm_printf((UB *)"TessronOS: this image is for a board with the %s; the device tree "
			  "names \"%s\" (%s). Write the image made for this board, or check "
			  "device_tree in config.txt. Stopped.\n",
			  BOARD_SOC, knl_dtb_compat, knl_dtb_model);
		for (;;) {
			Asm("wfe");
		}
	}
#endif
}

EXPORT BOOL knl_board_has_compat( CONST UB *compat, INT len, CONST char *want )
{
	INT	at = 0;

	while ( at < len ) {
		INT	n;

		for ( n = 0; at + n < len && compat[at + n] != '\0'; n++ ) ;
		if ( at + n >= len ) {
			break;
		}
		if ( same((CONST char *)compat + at, want) ) {
			return TRUE;
		}
		at += n + 1;
	}
	return FALSE;
}

EXPORT void knl_board_set_rev( UW rev )
{
	UW	kind = knl_board_by_rev(rev);

	knl_board_set(( kind != BOARD_UNKNOWN ) ? kind : knl_board.kind, rev);
}

#endif /* CPU_CORE_ARMV8A */
