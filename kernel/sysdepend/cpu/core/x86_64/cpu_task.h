/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cpu_task.h
 *	CPU-dependent task control (x86-64 core)
 */

#ifndef _SYSDEPEND_CPU_CORE_CPUTASK_
#define _SYSDEPEND_CPU_CORE_CPUTASK_

#include "sysman/pfalloc.h"
#include "sysman/space.h"

/*
 * System stack frame of a task that is not running: the frame every
 * entry to the kernel makes (vector.S), the same for an interrupt, an
 * exception, the dispatcher's software interrupt and SYSCALL.
 *	high address
 *	  +-------------------+
 *	  | ss, rsp           |  \
 *	  | rflags, cs, rip   |   | pushed by the processor (SYSCALL: by vector.S)
 *	  | err, vec          |  /  error code (0 when none), vector
 *	  | r15 .. rax        |     every general register
 *	  +-------------------+ <- tskctxb.ssp
 *	low address
 * It is 176 bytes, a multiple of 16, so that the stack stays aligned
 * for the C code below it. A frame of SYSCALL has the 7th and 8th
 * arguments of the call in place of err and vec (svc_x86_64.c).
 */
typedef struct {
	UD	rax, rbx, rcx, rdx, rsi, rdi, rbp;
	UD	r8, r9, r10, r11, r12, r13, r14, r15;
	UD	vec, err;
	UD	rip, cs, rflags, rsp, ss;
} T_EXCFRAME;

typedef T_EXCFRAME	SStackFrame;

#define SSF_EXC_SIZE		(22 * 8)	/* 176 */

#define DORMANT_STACK_SIZE	( sizeof(SStackFrame) + 0x10 )

/*
 * The system call's function code, its arguments (0 the first) and the
 * register its value goes back in, as the SVC gateway reads the frame
 */
#define KNL_SVC_FNCD(f)		( (f)->rax )
#define KNL_SVC_ARG(f, n)	( ( n ) == 0 ? (f)->rdi : ( n ) == 1 ? (f)->rsi : ( n ) == 2 ? (f)->rdx :	\
				  ( n ) == 3 ? (f)->r10 : ( n ) == 4 ? (f)->r8 : ( n ) == 5 ? (f)->r9 :		\
				  ( n ) == 6 ? (f)->vec : (f)->err )
#define KNL_SVC_RET(f)		( (f)->rax )

#if USE_FPU
/*
 * The x87, SSE and AVX state of a task created with TA_FPU, kept in an
 * area at the top of its system stack: an XSAVE area of knl_xsave_size
 * bytes, 64 byte aligned (the FXSAVE image alone on a processor without
 * XSAVE).
 */
#define knl_fpu_ctx	(knl_pcpu()->fpu_ctx)	/* Task that owns the registers (per processor) */

Inline void *knl_fpu_area( TCB *tcb )
{
	return (void *)( ( (UBINT)tcb->isstack & ~(UBINT)63 ) - knl_xsave_size );
}
#endif /* USE_FPU */

/*
 * Whether a task can be started: one of protection level 2/3 only with
 * the user stack knl_tcb_sysdep_cre gave it. tk_sta_tsk refuses any
 * other (E_SYS); it never runs in the kernel's mode in its place.
 */
#define knl_task_startable(tcb) \
	( ( (tcb)->tskatr & TA_RNG3 ) < TA_RNG2 || (tcb)->tskctxb.ustk != NULL )

/* Where a task of protection level 2/3 without a user stack would start (cpu_cntl.c) */
IMPORT void knl_task_no_ustack( INT stacd, void *exinf );

/*
 * Create stack frame for task startup
 *	Protection level 0/1: runs in the kernel's mode on the system stack.
 *	Protection level 2/3: runs in user mode on a user stack in the system
 *	process space, or in its process's space (knl_tsk_set_space); the
 *	system stack takes interrupts, exceptions and system calls.
 *	The task starts with interrupts enabled, as a function just called:
 *	its stack pointer 8 below a multiple of 16, where a return address
 *	would be.
 */
Inline void knl_setup_context( TCB *tcb )
{
	SStackFrame	*ssp;
	UINT		rng = tcb->tskatr & TA_RNG3;
	UBINT		top = (UBINT)tcb->isstack & ~(UBINT)63;

#if USE_FPU
	if ( (tcb->tskatr & TA_FPU) != 0 ) {
		UB	*fpu = (UB *)knl_fpu_area(tcb);

		knl_memset(fpu, 0, knl_xsave_size);
		*(UH *)( fpu + 0 ) = FCW_INIT;		/* the x87 control word */
		*(UW *)( fpu + 24 ) = MXCSR_INIT;
		top = (UBINT)fpu;
	}
#endif /* USE_FPU */
	ssp = (SStackFrame *)top - 1;
	knl_memset(ssp, 0, sizeof(*ssp));

	ssp->rip	= (UD)(UBINT)tcb->task;		/* Task startup address */
	ssp->rflags	= RFL_FIXED | RFL_IF;
	tcb->tskctxb.fsbase = 0;

	if ( rng >= TA_RNG2 && tcb->tskctxb.ustk != NULL ) {
		/* user task: user stack allocated by knl_tcb_sysdep_cre() */
		ssp->cs		= SEL_UCODE;
		ssp->ss		= SEL_UDATA;
		ssp->rsp	= ( (UD)(UBINT)tcb->tskctxb.usp & ~(UD)15 ) - 8;
		if ( tcb->tskctxb.ttbr0 == knl_ttbr0_null ) {
			/* not created for a process: the system process space */
			tcb->tskctxb.ttbr0 = knl_sysprc_ttbr0;
		}
	} else {
		if ( rng >= TA_RNG2 ) {
			/* no user stack: never its own code, nor a process's space */
			ssp->rip = (UD)(UBINT)knl_task_no_ustack;
		}
		ssp->cs		= SEL_KCODE;
		ssp->ss		= SEL_KDATA;
		ssp->rsp	= (UD)(UBINT)( ssp + 1 ) - 8;
		tcb->tskctxb.ttbr0 = knl_ttbr0_null;
	}
	tcb->tskctxb.ssp = ssp;				/* System stack */
}

/*
 * Set task startup code
 *	rdi = stacd, rsi = exinf
 */
Inline void knl_setup_stacd( TCB *tcb, INT stacd )
{
	SStackFrame	*ssp = tcb->tskctxb.ssp;

	ssp->rdi = (UD)(BINT)stacd;
	ssp->rsi = (UD)(UBINT)tcb->exinf;
}

/*
 * Delete task context
 */
Inline void knl_cleanup_context( TCB *tcb )
{
#if	USE_FPU
	if ( (tcb->tskatr & TA_FPU) != 0 ) {
		if ( knl_fpu_ctx == tcb ) {
			knl_fpu_ctx = NULL;
		}
	}
#endif	/* USE_FPU */
	/* the user stack is kept for a restart; knl_tcb_sysdep_del() releases it */
}

#endif /* _SYSDEPEND_CPU_CORE_CPUTASK_ */
