/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	pth_cpu.h
 *	A thread's processor (ARMv8-A / AArch64). The thread pointer is
 *	TPIDR_EL0, which the kernel keeps with the task.
 *
 *	The TLS is variant I: the thread pointer points at a control block
 *	of 16 bytes, and the thread's copy of the TLS image follows it,
 *	at the image's alignment. The compiler reaches a thread local
 *	variable at the thread pointer plus 16 rounded up to that alignment
 *	plus the variable's offset in the image. The control block's first
 *	word is the thread's record (PTH), the second where it ends
 *	(pth_thread.c).
 */

#ifndef __PTH_CPU_ARMV8A_H__
#define __PTH_CPU_ARMV8A_H__

#include <stddef.h>

#define PTH_TCB_SIZE	16		/* the control block */
#define PTH_TCB_OWNER	0		/* its word with the thread's record */
#define PTH_TCB_EXIT	1		/* its word with where the thread ends */

static inline void *pth_tp_get( void )
{
	void	*tp;

	__asm__ volatile ( "mrs %0, tpidr_el0" : "=r"(tp) );
	return tp;
}

static inline void pth_tp_set( void *tp )
{
	__asm__ volatile ( "msr tpidr_el0, %0" :: "r"(tp) : "memory" );
}

/* A moment's pause in a loop that waits for another processor */
static inline void pth_relax( void )
{
	__asm__ volatile ( "yield" );
}

/*
 * A thread's block of control block and TLS, made at 'at' (aligned to
 * align, the TLS image's alignment and at least 16): where the thread
 * pointer goes, where the copy of the image goes, both from 'at', and
 * the bytes of the whole for an image of memsz bytes
 */
static inline size_t pth_tls_tp_off( size_t align, size_t memsz )
{
	(void)align;
	(void)memsz;
	return 0;
}

static inline size_t pth_tls_up( size_t v, size_t a )
{
	return ( v + a - 1 ) & ~( a - 1 );
}

static inline size_t pth_tls_data_off( size_t align, size_t memsz )
{
	(void)memsz;
	return pth_tls_up(PTH_TCB_SIZE, align);
}

static inline size_t pth_tls_bytes( size_t align, size_t memsz )
{
	return pth_tls_data_off(align, memsz) + memsz;
}

#endif /* __PTH_CPU_ARMV8A_H__ */
