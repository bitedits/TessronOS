/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	exc_hdl.c (x86-64)
 *	Exceptions: the faults the kernel puts right, the faults of a
 *	process, NMI, and the report of what stops the system.
 */

#include <sys/machine.h>

#ifdef CPU_CORE_X86_64

#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <kernel.h>
#include "../../../sysdepend.h"
#include "cpu_task.h"
#include <ts/proc.h>
#include <ts/lockup.h>
#include "cpu_ufault.h"

/*
 * What is said of an exception goes to the console at once, without the
 * console's lock (tm_printf_urgent): the processor that holds that lock
 * may be this one, stopped in the middle of a line, or another that
 * will never let go of it. The screen console is brought back for it.
 */
#if USE_EXCEPTION_DBG_MSG
#define EXCEPTION_DBG_MSG(...)	tm_printf_urgent((UB*)__VA_ARGS__)
#else
#define EXCEPTION_DBG_MSG(...)
#endif

IMPORT BOOL	knl_tlb_nmi( void );		/* mmu.c */
IMPORT BOOL	knl_lockup_nmi( T_EXCFRAME *f );	/* lockup.c */
IMPORT void	knl_lockup_halted( void );

/*
 * This processor stops. The others go on; they find it taking no
 * interrupts and say so (lockup.c).
 */
LOCAL void halt( void )
{
	disint();
	knl_lockup_halted();
	while (1) {
		Asm("hlt");
	}
}

/* A report of a stopping exception under way on each processor: one met while saying it stops at once */
LOCAL volatile BOOL	exc_saying[CNF_MAX_PRCID];

LOCAL INT exc_cpu( void )
{
	return knl_pcpu()->prcid - 1;
}

LOCAL void exc_begin( void )
{
	INT	c = exc_cpu();

	knl_fbcon_resume();
	if ( c >= 0 && c < CNF_MAX_PRCID ) {
		if ( exc_saying[c] ) {
			tm_urgent_puts((UB*)"\n!EXCEPTION! another one while the last was being told: stopped\n");
			halt();
		}
		exc_saying[c] = TRUE;
	}
}

LOCAL CONST char *exc_name( UW v )
{
	static CONST char *n[32] = {
		"#DE divide error", "#DB debug", "NMI", "#BP breakpoint", "#OF overflow",
		"#BR bound", "#UD invalid opcode", "#NM no x87", "#DF double fault",
		"coprocessor overrun", "#TS invalid TSS", "#NP segment not present",
		"#SS stack fault", "#GP general protection", "#PF page fault", "15",
		"#MF x87 error", "#AC alignment check", "#MC machine check",
		"#XM SIMD error", "#VE virtualization", "#CP control protection",
		"22", "23", "24", "25", "26", "27", "#HV", "#VC", "#SX", "31"
	};

	return ( v < 32 ) ? n[v] : "interrupt";
}

Inline UD read_cr2( void )
{
	UD	v;

	Asm("mov %%cr2, %0" : "=r"(v));
	return v;
}

/*
 * A task wrote past the foot of its stack (kernel.h). Whatever lay below
 * the stack is gone with it, so this says which task and how large its
 * stack was, and goes no further.
 */
EXPORT void knl_stack_broken( TCB *tcb )
{
	disint();
	knl_fbcon_resume();
	tm_printf_urgent((UB*)"\n!STACK! CPU%d: task=%d ran past the foot of its %d byte stack\n",
		exc_cpu(), (tcb != NULL) ? tcb->tskid : 0,
		(tcb != NULL) ? (INT)tcb->sstksz : 0);
	knl_lockup_say_task(" ", tcb);
	halt();
}

#if CNF_STK_WATCH
IMPORT TCB	knl_tcb_table[];

EXPORT void knl_stack_overwritten( TCB *tcb, UD *at, UD was, UB prc, UD since )
{
	UD	now = ReadSysCounterSync(), *p;
	INT	i;

	disint();
	if ( knl_num_prc > 1 ) {
		knl_send_ipi(( ( 1U << knl_num_prc ) - 1 ) & ~( 1U << exc_cpu() ), SGI_STOP);
	}
	tm_printf_urgent((UB*)"\n!STKWATCH! CPU%d: task=%d: the return address at %lx was %lx"
			 " when CPU%d gave its context up %d us ago, and is %lx\n",
			 exc_cpu(), tcb->tskid, (UD)(UBINT)at, was, prc,
			 (INT)( ( now - since ) * 1000000 / ( knl_cntfrq != 0 ? knl_cntfrq : 1 ) ), *at);
	knl_lockup_say_task(" ", tcb);
	for ( p = at - 8; p < at + 8; p += 2 ) {
		tm_printf_urgent((UB*)"  %lx: %016lx %016lx\n", (UD)(UBINT)p, p[0], p[1]);
	}
	for ( i = 0; i < NUM_TSKID; i++ ) {
		TCB	*t = &knl_tcb_table[i];
		UBINT	w = (UBINT)t->wercd;

		if ( t->state != 0 && w != 0
		  && w + 0x200 > (UBINT)at && w < (UBINT)at + 0x200 ) {
			tm_printf_urgent((UB*)"  task %d state %d wid %d: its wait result goes to %lx\n",
					 t->tskid, t->state, t->wid, (UD)w);
		}
	}
	for ( i = 0; i < knl_num_prc; i++ ) {
		TCB	*t = knl_pcpu_tbl[i].ctxtsk;

		tm_printf_urgent((UB*)"  CPU%d runs task %d\n", i, ( t != NULL ) ? t->tskid : 0);
	}
	halt();
}
#endif

/*
 * An exception that stops the system: what it was and where, the
 * registers, the task, where it was called from
 */
LOCAL void exc_report( T_EXCFRAME *f, UD cr2 )
{
	UINT	s;

	disint();
	exc_begin();
	s = tm_urgent_begin();
	EXCEPTION_DBG_MSG("\n!EXCEPTION! CPU%d %s (vector %d) error %lx at %04lx:%016lx RFLAGS=%lx CR2=%lx\n",
		exc_cpu(), exc_name((UW)f->vec), (INT)f->vec, f->err, f->cs, f->rip, f->rflags, cr2);
	EXCEPTION_DBG_MSG(" task=%d indp=%d\n",
		(knl_ctxtsk != NULL) ? knl_ctxtsk->tskid : 0, knl_taskindp);
	knl_lockup_say_task(" ", knl_ctxtsk);
	EXCEPTION_DBG_MSG(" rax=%016lx rbx=%016lx rcx=%016lx rdx=%016lx\n", f->rax, f->rbx, f->rcx, f->rdx);
	EXCEPTION_DBG_MSG(" rsi=%016lx rdi=%016lx rbp=%016lx rsp=%016lx\n", f->rsi, f->rdi, f->rbp, f->rsp);
	EXCEPTION_DBG_MSG(" r8 =%016lx r9 =%016lx r10=%016lx r11=%016lx\n", f->r8, f->r9, f->r10, f->r11);
	EXCEPTION_DBG_MSG(" r12=%016lx r13=%016lx r14=%016lx r15=%016lx\n", f->r12, f->r13, f->r14, f->r15);
	if ( ( f->cs & SEL_RPL3 ) == 0 ) {
		knl_lockup_say_frames(" ", (UBINT)f->rbp);	/* taken in the kernel: where it was called from */
	}
	tm_urgent_end(s);
	halt();
}

/*
 * The ARM name of this function is kept for the common parts that call
 * it: a stopping exception, given its frame
 */
EXPORT void knl_exception_handler( void *frame, UD esr, UD far )
{
	(void)esr;
	exc_report((T_EXCFRAME *)frame, far);
}

/*
 * A page fault taken in the kernel. An access refused by SMAP to a page
 * a process may reach, met while the kernel works for it in a call, is
 * recorded, and the call goes on to its end with the way open (it would
 * leave a volume, a store or a lock half done otherwise); the process
 * ends as the call returns (knl_prc_kpan). An access to the running
 * process's half that is not mapped, or not to be written, is the
 * process's doing: the address is given a page of nothing and the access
 * is made again, and the process ends as the call returns
 * (knl_prc_kfault, design 9.11). A fault in the device window maps its
 * gigabyte. Anything else stops the system. Returns only when put
 * right.
 */
#define USER_HALF_END	U64(0x0000800000000000)

/* The last access SMAP refused, for the tests */
EXPORT UD	knl_pan_last_far = 0;
EXPORT UD	knl_pan_last_elr = 0;

LOCAL BOOL pan_refused( T_EXCFRAME *f, UD far )
{
	UD	cr3, ent;

	if ( !knl_pan_on || ( f->err & ( PFERR_P | PFERR_U | PFERR_I ) ) != PFERR_P
	  || ( f->rflags & RFL_AC ) != 0 || far >= USER_HALF_END ) {
		return FALSE;
	}
	Asm("mov %%cr3, %0" : "=r"(cr3));
	ent = knl_pt_entry(cr3 & MMU_ROOT_PA, (UBINT)far & ~(UBINT)0xfff);
	return ( ent != 0 && ( ent & PTE_US ) != 0 );
}

LOCAL BOOL pan_fault( T_EXCFRAME *f, UD far )
{
	TCB	*t = knl_ctxtsk;
	UD	esr = ( (UD)f->vec << 32 ) | f->err;

	knl_pan_faults++;
	knl_pan_last_far = far;
	knl_pan_last_elr = f->rip;
	tm_printf((UB*)"\n!PAN! the kernel touched a user page without making sure of it:"
		  " CR2=%lx RIP=%lx task=%d indp=%d\n",
		  far, f->rip, ( t != NULL ) ? t->tskid : 0, knl_taskindp);
	if ( t == NULL || knl_taskindp > 0 ) {
		return FALSE;
	}
	(void)knl_prc_kpan(t->tskctxb.ttbr0, t->tskid, (UBINT)far, esr, f->rip, f->rbp);
	f->rflags |= RFL_AC;		/* the way open until the call is over */
	return TRUE;
}

EXPORT void knl_kernel_fault( void *frame, UD esr, UD far )
{
	T_EXCFRAME	*f = (T_EXCFRAME *)frame;

	if ( f->vec == VEC_PF ) {
		if ( knl_dev_fault((UBINT)far) ) {
			return;
		}
		if ( pan_refused(f, far) ) {
			if ( pan_fault(f, far) ) {
				return;
			}
			exc_report(f, far);
		}
		if ( ( f->err & ( PFERR_RSVD | PFERR_I ) ) == 0 && far < USER_HALF_END
		  && knl_ctxtsk != NULL && knl_taskindp == 0
		  && knl_prc_kfault(knl_ctxtsk->tskctxb.ttbr0, knl_ctxtsk->tskid, (UBINT)far,
				    esr, f->rip, f->rbp) ) {
			return;
		}
	}
	exc_report(f, far);
}

/*
 * Fault of a task in user mode: report and terminate the task; the
 * kernel and the other tasks go on. Runs on the task's system stack with
 * the frame at 'frame'. An invalid opcode ends the process with
 * TS_ABORT_ILL, every other exception with TS_ABORT_FAULT; what
 * happened is recorded with the process's end for the desktop
 * (knl_prc_abort).
 *
 * A page fault the process's memory can put right first goes to
 * knl_prc_ufault, as a call of the task would (interrupts enabled, the
 * task marked as inside the kernel); then this returns, and the access
 * is made again. A data fault it does not put right goes to the
 * program's entry for faults when it named one (knl_prc_flt_give).
 */
IMPORT BOOL knl_prc_ufault( UBINT va, UD esr );

EXPORT void knl_el0_fault( void *frame, UD esr, UD far )
{
	T_EXCFRAME	*f = (T_EXCFRAME *)frame;
	T_PRCFLT	info;
	TCB		*me = knl_ctxtsk;
	BOOL		fixed;

	if ( f->vec == VEC_PF && me != NULL && knl_taskindp == 0 ) {
		me->svcdepth++;
		enaint(0);
		fixed = knl_prc_ufault((UBINT)far, esr);
		if ( !fixed ) {
			/* the program's own entry for it (ts_def_flt) takes it */
			fixed = knl_prc_flt_give(frame, esr, far);
		}
		disint();
		me->svcdepth--;
		if ( fixed ) {
			return;
		}
	}

	EXCEPTION_DBG_MSG("\n!EL0 fault! task=%d %s (vector %d) error %lx CR2=%lx RIP=%lx RSP=%lx\n",
		(knl_ctxtsk != NULL) ? knl_ctxtsk->tskid : 0,
		exc_name((UW)f->vec), (INT)f->vec, f->err, far, f->rip, f->rsp);
	if ( knl_ctxtsk == NULL || knl_taskindp > 0 ) {
		halt();
	}
	enaint(0);		/* tk_ext_tsk is a task-context call: run it with interrupts enabled */
	knl_memset(&info, 0, sizeof(info));
	info.kind = TS_FLT_EL0;
	info.tskid = knl_ctxtsk->tskid;
	info.esr = esr;
	info.far = far;
	info.elr = f->rip;
	info.lr = f->rbp;
	/* a task of a process takes the whole process down (design 9.11) */
	knl_prc_abort(knl_ctxtsk->tskctxb.ttbr0,
		( f->vec == 6 ) ? TS_ABORT_ILL : TS_ABORT_FAULT, &info);
	tk_ext_tsk();		/* no return */
	halt();
}

/*
 * Every exception (vector.S): its vector and error code make the
 * "syndrome" the common parts are handed (cpu_ufault.h), CR2 the address
 * met
 */
EXPORT void knl_exception_entry( T_EXCFRAME *f )
{
	UD	far = ( f->vec == VEC_PF ) ? read_cr2() : 0;
	UD	esr = ( (UD)f->vec << 32 ) | ( f->err & 0xffffffffULL );

	if ( ( f->cs & SEL_RPL3 ) != 0 ) {
		knl_el0_fault(f, esr, far);
	} else {
		knl_kernel_fault(f, esr, far);
	}
}

/*
 * NMI, the double fault, the machine check (vector.S, on their own
 * stacks): an NMI is a request of another processor's (the translations
 * to drop, where this processor is) or the hardware's; the other two
 * stop the system
 */
EXPORT void knl_nmi_entry( T_EXCFRAME *f )
{
	if ( f->vec == 2 ) {
		BOOL	mine = knl_tlb_nmi();

		mine = knl_lockup_nmi(f) || mine;
		if ( mine ) {
			return;
		}
		tm_printf_urgent((UB*)"\n!NMI! CPU%d at %lx (none was asked for)\n", exc_cpu(), f->rip);
		return;
	}
	exc_report(f, read_cr2());
}

WEAK_FUNC EXPORT void Default_Handler( UINT intno )
{
	if ( intno == INTVEC_APIC_ERROR ) {
		knl_apic_eoi();
		return;
	}
	knl_fbcon_resume();
	EXCEPTION_DBG_MSG("\n!Undefined interrupt! CPU%d interrupt %d\n", exc_cpu(), intno);
	halt();
}

#endif	/* CPU_CORE_X86_64 */
