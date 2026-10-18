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
 *	Machine type definition (x86-64 core)
 */

#ifndef __SYS_MACHINE_CORE_H__
#define __SYS_MACHINE_CORE_H__

/*
 * CPU core features
 */

#define ALLOW_MISALIGN		1
#define INT_BITWIDTH		32
#define PTR_BITWIDTH		64
#define BIGENDIAN		0			/* Little Endian */

/* Data cache line size */
#define CACHE_LINE_SIZE		64

#endif /* __SYS_MACHINE_CORE_H__ */
