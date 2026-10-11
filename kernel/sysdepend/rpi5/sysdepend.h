/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sysdepend.h
 *	System-dependent definition (rpi5)
 */

#ifndef _SYSDEPEND_TARGET_SYSDEPEND_
#define _SYSDEPEND_TARGET_SYSDEPEND_

#include "../cpu/bcm2712/sysdepend.h"

/*
 * The PCI Express root complex the RP1 hangs on (kernel/sysdepend/rpi5/
 * pcie_rc.c). Brings the link up when the firmware left it in reset and
 * finds what is on it; answers how many functions were found, or 0 when
 * the link is down.
 */
IMPORT INT knl_pcie_rc_init( void );
IMPORT ER  knl_fan_init( void );		/* fan.c */

/* The firmware's property interface (kernel/sysdepend/rpi/mbox.c) */
#include "../rpi/rpi_fw.h"

#endif /* _SYSDEPEND_TARGET_SYSDEPEND_ */
