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
 *	System-dependent definition (rpi4)
 */

#ifndef _SYSDEPEND_TARGET_SYSDEPEND_
#define _SYSDEPEND_TARGET_SYSDEPEND_

#include "../cpu/bcm2711/sysdepend.h"

/* The firmware's property interface (kernel/sysdepend/rpi/mbox.c) */
#include "../rpi/rpi_fw.h"

/*
 * The BCM2711 PCI Express root complex (kernel/sysdepend/rpi4/pcie_rc.c).
 * Brings the link up from reset, walks what is on it and, on the boards
 * whose xHCI is a VL805, has the firmware load its program. Answers how
 * many functions were found, 0 when the link stays down.
 */
IMPORT INT knl_pcie_rc_init( void );

/* The GENET Ethernet controller (device/net/eth_genet.c) */
IMPORT INT knl_genet_init( void );

#endif /* _SYSDEPEND_TARGET_SYSDEPEND_ */
