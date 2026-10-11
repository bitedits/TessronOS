/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	hw_setting.c (Raspberry Pi 4)
 *	Board dependent start-up and shutdown.
 *
 *	The firmware of the Raspberry Pi 4 has no PSCI: the secondary
 *	processors wait in its spin table (smp.c releases them), and a
 *	restart is the power management block's watchdog, set to fire a
 *	full reset after a few ticks. There is no switching the power off
 *	either; what the board can do is to restart into the firmware with
 *	the boot partition number 63, which the firmware takes as "halt" and
 *	stays in, with the processors stopped and the board drawing little,
 *	until the power is cycled.
 */

#include <sys/machine.h>

#ifdef RPI4

#include "kernel.h"
#include <tm/tmonitor.h>
#include "sysdepend.h"

/* Power management: reset control and status, and the watchdog */
#define PM_RSTC			(PM_BASE + 0x1c)
#define PM_RSTS			(PM_BASE + 0x20)
#define PM_WDOG			(PM_BASE + 0x24)
#define PM_PASSWORD		0x5a000000U	/* every write carries it in the top byte */
#define PM_WDOG_TIME_MASK	0x000fffffU	/* ticks of 16us */
#define PM_RSTC_WRCFG_MASK	0x00000030U
#define PM_RSTC_FULL_RESET	0x00000020U
/* the boot partition is kept in every other bit of the status, 0, 2, ... 10 */
#define PM_RSTS_PART_MASK	0x00000555U
#define PM_RSTS_PART_HALT	0x00000555U	/* partition 63 */

#define WDOG_TICKS		10

/*
 * No PSCI on this board: calls answer "not supported", and a processor
 * that would be started through it is started from the spin table.
 */
EXPORT UD knl_psci_call( UD fid, UD a1, UD a2, UD a3 )
{
	return (UD)-1;
}

EXPORT ER knl_psci_cpu_on( UD mpidr, UD entry_pa, UD ctx )
{
	return E_NOSPT;
}

EXPORT void knl_startup_hw( void )
{
	/* nothing to do: the UART and the GIC are usable as the firmware left them */
}

/* A full reset of the chip a few ticks from now */
LOCAL void pm_reset( void )
{
	UW	rstc;

	out_w(PM_WDOG, PM_PASSWORD | ( WDOG_TICKS & PM_WDOG_TIME_MASK ));
	rstc = in_w(PM_RSTC) & ~PM_RSTC_WRCFG_MASK;
	out_w(PM_RSTC, PM_PASSWORD | rstc | PM_RSTC_FULL_RESET);
	Asm("dsb sy");
}

#if USE_SHUTDOWN
EXPORT void knl_shutdown_hw( void )
{
	UW	rsts;

	disint();
	tm_putstring((UB*)"\n<< TessronOS: system halted (power off the board to start again) >>\n");
	rsts = in_w(PM_RSTS) & ~PM_RSTS_PART_MASK;
	out_w(PM_RSTS, PM_PASSWORD | rsts | PM_RSTS_PART_HALT);
	pm_reset();
	while (1) {
		Asm("wfi");
	}
}
#endif /* USE_SHUTDOWN */

EXPORT ER knl_restart_hw( W mode )
{
	UW	rsts;

	switch ( mode ) {
	case -1:	/* reset and re-start */
		disint();
		rsts = in_w(PM_RSTS) & ~PM_RSTS_PART_MASK;	/* the normal boot partition */
		out_w(PM_RSTS, PM_PASSWORD | rsts);
		pm_reset();
		while (1) {
			Asm("wfi");
		}
	default:
		return E_NOSPT;
	}
}

#endif /* RPI4 */
