/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	wdog.c (Raspberry Pi 4 and 5)
 *	The hardware watchdog of the power management block (design 14.10)
 *
 *	The PM block of the BCM2711 and the BCM2712 (the Compute Module 5
 *	has the same chip as the Raspberry Pi 5) counts PM_WDOG down in ticks
 *	of 16 microseconds, 20 bits of them -- a little under 17 seconds --
 *	and when it reaches nought does what PM_RSTC says: with WRCFG set to
 *	a full reset, it resets the whole chip, which starts again from the
 *	firmware as at power on. Every write carries the password in its top
 *	byte, or is ignored. The block is at 0x7d200000 of the BCM2712's
 *	peripheral space (0x10_7d20_0000 for the processors) and at
 *	0x7e100000 of the BCM2711's (0xfe100000), as the boards' device
 *	trees give it (the node "watchdog" with "brcm,bcm2712-pm" or
 *	"brcm,bcm2835-pm"). lockup.c feeds it and stops it.
 */

#include <sys/machine.h>

#if defined(RPI5) || defined(RPI4)

#include "kernel.h"
#include "../sysdepend.h"

#define PM_RSTC			(PM_BASE + 0x1c)
#define PM_WDOG			(PM_BASE + 0x24)
#define PM_PASSWORD		0x5a000000U
#define PM_WDOG_TIME_MASK	0x000fffffU	/* ticks of 16us */
#define PM_RSTC_WRCFG_MASK	0x00000030U
#define PM_RSTC_FULL_RESET	0x00000020U
#define PM_RSTC_STOP		0x00000102U	/* the watchdog's reset taken away */

#define WDOG_MAX_MS		16000

/*
 * Whether the watchdog counts down. QEMU's raspi4b resets the machine
 * the moment PM_RSTC is told to, without counting, so there it is not
 * used. That machine is told from a board by its device tree, where
 * QEMU disables the PCI Express root complex it does not have; every
 * Raspberry Pi 4, 400 and Compute Module 4 has it.
 */
LOCAL INT	wdog_have = -1;

LOCAL BOOL wdog_counts( void )
{
	if ( wdog_have < 0 ) {
#ifdef RPI4
		wdog_have = knl_dtb_okay(PCIE_DT_PATH) ? 1 : 0;
#else
		wdog_have = 1;
#endif
	}
	return ( wdog_have != 0 );
}

/* A full reset ms from now, unless fed again before. TRUE: there is a watchdog */
EXPORT BOOL knl_wdog_hw_feed( UW ms )
{
	UW	ticks, rstc;

	if ( !wdog_counts() ) {
		return FALSE;
	}
	if ( ms > WDOG_MAX_MS ) ms = WDOG_MAX_MS;
	ticks = ( ms * 1000 / 16 ) & PM_WDOG_TIME_MASK;
	out_w(PM_WDOG, PM_PASSWORD | ticks);
	rstc = in_w(PM_RSTC) & ~PM_RSTC_WRCFG_MASK;
	out_w(PM_RSTC, PM_PASSWORD | rstc | PM_RSTC_FULL_RESET);
	return TRUE;
}

EXPORT void knl_wdog_hw_stop( void )
{
	if ( wdog_counts() ) {
		out_w(PM_RSTC, PM_PASSWORD | PM_RSTC_STOP);
	}
}

#endif /* RPI5 || RPI4 */
