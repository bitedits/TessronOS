/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cpu_uaccess.h (x86-64)
 *	The kernel's way to the pages of a process: supervisor mode access
 *	prevention (SMAP).
 */

#ifndef _SYSDEPEND_CPU_CORE_UACCESS_
#define _SYSDEPEND_CPU_CORE_UACCESS_

#include <sys/machine.h>
#include <tk/typedef.h>
#include <sys/sysdef.h>

/*
 * The same rules as the other architecture's PAN (docs/tessronos/
 * 04-kernel.md 4.6.1). On a processor that has SMAP (CPUID 7:EBX bit
 * 20), CR4.SMAP is set from the start: a load or store of the kernel's
 * to a user page faults, and the fault handler records it, while
 * RFLAGS.AC is clear -- which every entry to the kernel makes it
 * (SFMASK for SYSCALL, the CLAC of vector.S for the others). Where the
 * kernel reaches a process's memory on purpose, after knl_prc_user_ok
 * has made sure of it, it opens the way with knl_ua_open (STAC) and
 * closes it with knl_ua_close (CLAC), or copies through knl_ua_get and
 * knl_ua_put. RFLAGS goes with the task in its frames, so a call that
 * blocks or is preempted with the way open finds it open when it goes
 * on, and an interrupt taken meanwhile runs with it shut. Without SMAP
 * all of this does nothing. The names are the other architecture's.
 */
IMPORT BOOL	knl_pan_on;			/* SMAP in use (all processors) */
IMPORT UW	knl_pan_faults;			/* accesses refused to the kernel so far */
IMPORT void	knl_pan_setup( void );		/* each processor, as it starts */

Inline BOOL knl_ua_ac( void )
{
	UD	fl;

	Asm("pushfq; popq %0" : "=r"(fl) :: "memory");
	return ( fl & RFL_AC ) != 0;
}

/* The way to the process's pages opened: TRUE when it was shut and is to be shut again */
Inline BOOL knl_ua_open( void )
{
	if ( knl_pan_on && !knl_ua_ac() ) {
		Asm("stac" ::: "memory");
		return TRUE;
	}
	return FALSE;
}

Inline void knl_ua_close( BOOL opened )
{
	if ( opened ) {
		Asm("clac" ::: "memory");
	}
}

/* Whether the way is open on this processor now (always TRUE without SMAP) */
Inline BOOL knl_ua_is_open( void )
{
	return !knl_pan_on || knl_ua_ac();
}

/*
 * The value of an expression worked out with the way open, and a
 * statement made with it open: for the uses of a process's memory that
 * follow a check of it.
 */
#define UACCESS(expr)	({ BOOL ua_o_ = knl_ua_open();			\
			   __typeof__(expr) ua_r_ = (expr);		\
			   knl_ua_close(ua_o_); ua_r_; })
#define UACCESS_DO(stmt) do { BOOL ua_o_ = knl_ua_open(); stmt;	\
			      knl_ua_close(ua_o_); } while ( 0 )

/* len bytes copied in from the process's usrc, out to its udst (cpu_cntl.c) */
IMPORT void	knl_ua_get( void *dst, CONST void *usrc, SZ len );
IMPORT void	knl_ua_put( void *udst, CONST void *src, SZ len );

#endif /* _SYSDEPEND_CPU_CORE_UACCESS_ */
