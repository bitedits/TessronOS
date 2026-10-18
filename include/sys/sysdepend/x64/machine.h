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
 *	Machine type definition (x64 PC started by UEFI)
 */

#ifndef __SYS_SYSDEPEND_MACHINE_H__
#define __SYS_SYSDEPEND_MACHINE_H__

/*
 * [TYPE]_[CPU]		TARGET SYSTEM
 * X64			a PC with UEFI firmware: QEMU q35 with OVMF, Microsoft
 *			Surface Go 2. One image for all; which machine it is
 *			comes from SMBIOS and ACPI at start.
 */

#define X64			1		/* Target system : x64 PC */
#define CPU_CORE_X86_64		1		/* Target CPU-Core type : x86-64 */

#define TARGET_DIR		x64		/* Sysdepend-Directory name */
#define KNL_SYSDEP_PATH		x64		/* Kernel sysdepend path */

#include "../cpu/core/x86_64/machine.h"

#endif /* __SYS_SYSDEPEND_MACHINE_H__ */
