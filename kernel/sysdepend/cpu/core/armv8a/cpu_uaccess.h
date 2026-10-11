/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cpu_uaccess.h
 *	The kernel's way to the pages of a process (ARMv8-A / AArch64 core).
 *	Stands on the basic types alone, so that any part of the kernel can
 *	take it in.
 */

#ifndef _SYSDEPEND_CPU_CORE_UACCESS_
#define _SYSDEPEND_CPU_CORE_UACCESS_

#include <sys/machine.h>
#include <tk/typedef.h>
#include <sys/sysdef.h>

/*
 * Privileged Access Never (docs/tessronos/04-kernel.md 4.6.1). On a
 * processor that has PAN (ID_AA64MMFR1_EL1.PAN), PSTATE.PAN is set from
 * the start and by every exception taken to EL1 (SCTLR_EL1.SPAN clear):
 * a load or store of the kernel's to a page EL0 may reach faults, and
 * knl_kernel_fault records it. Where the kernel reaches a process's
 * memory on purpose -- after knl_prc_user_ok has made sure of it, in a
 * call of the process's -- it opens the way with knl_ua_open and closes
 * it again with knl_ua_close, or copies through knl_ua_get and
 * knl_ua_put. PSTATE goes with the
 * task in its exception frames, so a call that blocks or is preempted
 * with the way open finds it open when it goes on, and an interrupt
 * taken meanwhile runs with it shut. Without PAN all of this does
 * nothing, and the kernel reaches the pages as it always did.
 */
IMPORT BOOL	knl_pan_on;			/* PAN in use (all processors) */
IMPORT UW	knl_pan_faults;			/* accesses refused to the kernel so far */
IMPORT void	knl_pan_setup( void );		/* each processor, as it starts */

/* PSTATE.PAN named as a system register, which every assembler takes */
#define PAN_SYSREG	"S3_0_C4_C2_3"

/* The way to the process's pages opened: TRUE when it was shut and is to be shut again */
Inline BOOL knl_ua_open( void )
{
	UD	pan = 0;

	if ( knl_pan_on ) {
		Asm("mrs %0, " PAN_SYSREG : "=r"(pan) :: "memory");
		if ( pan != 0 ) {
			Asm("msr " PAN_SYSREG ", xzr" ::: "memory");
		}
	}
	return ( pan != 0 );
}

Inline void knl_ua_close( BOOL opened )
{
	if ( opened ) {
		Asm("msr " PAN_SYSREG ", %0" :: "r"((UD)PSR_PAN) : "memory");
	}
}

/* Whether the way is open on this processor now (always TRUE without PAN) */
Inline BOOL knl_ua_is_open( void )
{
	UD	pan = 0;

	if ( knl_pan_on ) {
		Asm("mrs %0, " PAN_SYSREG : "=r"(pan));
	}
	return ( pan == 0 );
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
