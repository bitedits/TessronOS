/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	hw_setting.c (x64 PC)
 *	Board dependent start-up and shutdown: off through ACPI (\_S5),
 *	a reset through the FADT's reset register.
 */

#include <sys/machine.h>

#ifdef X64

#include "kernel.h"
#include <tm/tmonitor.h>
#include "sysdepend.h"

EXPORT void knl_startup_hw( void )
{
	/* nothing to do: the firmware left the machine as the kernel takes it */
}

#if USE_SHUTDOWN
EXPORT void knl_shutdown_hw( void )
{
	disint();
	tm_putstring((UB*)"\n<< TessronOS: system off >>\n");
	(void)knl_acpi_poweroff();
	tm_putstring((UB*)"TessronOS: the machine did not turn off (no \\_S5 found)\n");
	while (1) {
		Asm("hlt");
	}
}
#endif /* USE_SHUTDOWN */

EXPORT ER knl_restart_hw( W mode )
{
	switch ( mode ) {
	case -1:	/* reset and re-start */
		disint();
		(void)knl_acpi_reset();
		while (1) {
			Asm("hlt");
		}
	default:
		return E_NOSPT;
	}
}

#endif /* X64 */
