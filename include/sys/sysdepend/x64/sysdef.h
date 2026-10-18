/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sysdef.h
 *	System dependencies definition (x64 PC). Included also from assembler program.
 */

#ifndef __SYS_SYSDEF_DEPEND_H__
#define __SYS_SYSDEF_DEPEND_H__

#include "../cpu/core/x86_64/sysdef.h"

/* Low-power mode: number of consecutive idle counts before entering (unused) */
#define LOWPOW_LIMIT		0x7fff

/*
 * PCI Express: the configuration space is one flat window, where the
 * MCFG table says (devinit.c sets knl_pcie_ecam_pa); the firmware gave
 * every function its windows and every bridge its buses, and the kernel
 * keeps them (sysman/pcie.c)
 */
#ifndef _in_asm_source_
extern unsigned long long	knl_pcie_ecam_pa;
#endif
#define PCIE_ECAM_PA		knl_pcie_ecam_pa
#define PCIE_ECAM_BASE		DEV_BASE(knl_pcie_ecam_pa)
#define PCIE_KEEP_FW_BARS	1

#endif /* __SYS_SYSDEF_DEPEND_H__ */
