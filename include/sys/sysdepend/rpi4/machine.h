/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	machine.h
 *	Machine type definition (Raspberry Pi 4, Raspberry Pi 400,
 *	Compute Module 4)
 */

#ifndef __SYS_SYSDEPEND_MACHINE_H__
#define __SYS_SYSDEPEND_MACHINE_H__

/*
 * [TYPE]_[CPU]		TARGET SYSTEM
 * RPI4			Raspberry Pi 4 family (Broadcom BCM2711, Cortex-A72 ×4)
 */

#define RPI4			1		/* Target system : Raspberry Pi 4, 400, Compute Module 4 */
#define CPU_BCM2711		1		/* Target SoC : Broadcom BCM2711 */
#define CPU_CORE_ARMV8A		1		/* Target CPU-Core type : ARMv8-A (AArch64) */
#define CPU_CORE_ACA72		1		/* Target CPU-Core : Arm Cortex-A72 */
#define RPI_VCFW		1		/* The VideoCore firmware: property mailbox, screen, board revision */

#define TARGET_DIR		rpi4		/* Sysdepend-Directory name */
#define KNL_SYSDEP_PATH		rpi4		/* Kernel sysdepend path */

#include "../cpu/bcm2711/machine.h"

#endif /* __SYS_SYSDEPEND_MACHINE_H__ */
