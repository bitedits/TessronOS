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
 *	A thread's processor (x86-64). The thread pointer is the FS base,
 *	which the program sets itself (WRFSBASE, which the kernel lets user
 *	mode use) and the kernel keeps with the task.
 *
 *	The TLS is variant II: the thread's copy of the TLS image ends where
 *	the thread pointer points, rounded up to the image's alignment, and
 *	the control block starts there. The compiler reaches a thread local
 *	variable at the thread pointer minus the image's size so rounded
 *	plus the variable's offset in the image. The control block's first
 *	word is its own address, as the ABI asks (%fs:0 is the thread
 *	pointer); the second is the thread's record (PTH), the third where
 *	it ends (pth_thread.c).
 */

#ifndef __PTH_CPU_X86_64_H__
#define __PTH_CPU_X86_64_H__

#include <stddef.h>

#define PTH_TCB_SIZE	32		/* the control block */
#define PTH_TCB_SELF	0		/* its word with its own address */
#define PTH_TCB_OWNER	1		/* its word with the thread's record */
#define PTH_TCB_EXIT	2		/* its word with where the thread ends */

static inline void *pth_tp_get( void )
{
	void	*tp;

	__asm__ volatile ( "rdfsbase %0" : "=r"(tp) );
	return tp;
}

static inline void pth_tp_set( void *tp )
{
	__asm__ volatile ( "wrfsbase %0" :: "r"(tp) : "memory" );
}

/* A moment's pause in a loop that waits for another processor */
static inline void pth_relax( void )
{
	__asm__ volatile ( "pause" );
}

static inline size_t pth_tls_up( size_t v, size_t a )
{
	return ( v + a - 1 ) & ~( a - 1 );
}

/*
 * A thread's block of TLS and control block, made at 'at' (aligned to
 * align, the TLS image's alignment and at least 16): where the thread
 * pointer goes, where the copy of the image goes, both from 'at', and
 * the bytes of the whole for an image of memsz bytes
 */
static inline size_t pth_tls_tp_off( size_t align, size_t memsz )
{
	return pth_tls_up(memsz, align);
}

static inline size_t pth_tls_data_off( size_t align, size_t memsz )
{
	(void)align;
	(void)memsz;
	return 0;
}

static inline size_t pth_tls_bytes( size_t align, size_t memsz )
{
	return pth_tls_tp_off(align, memsz) + PTH_TCB_SIZE;
}

#endif /* __PTH_CPU_X86_64_H__ */
