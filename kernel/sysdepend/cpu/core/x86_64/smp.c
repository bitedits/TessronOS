/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	smp.c (x86-64)
 *	Multiprocessor support (design chapter 8, first stage):
 *	per-CPU data and the processor's own tables (GDT, TSS, IDT, the
 *	SYSCALL registers), the big kernel lock, the global scheduler that
 *	assigns ready tasks to processors, IPIs, the start of the other
 *	processors by INIT and start-up IPIs, tk_get_prc.
 */

#include <sys/machine.h>

#ifdef CPU_CORE_X86_64

#include "kernel.h"
#include "../../../sysdepend.h"
#include "../../../../tkernel/task.h"
#include "../../../../tkernel/ready_queue.h"
#include "../../../../tkernel/timer.h"
#include "sysman/pfalloc.h"
#include "sysman/space.h"
#include <tm/tmonitor.h>

/*
 * Per-CPU data. The GS base of each processor points at its own element.
 */
EXPORT T_PCPU	knl_pcpu_tbl[CNF_MAX_PRCID] __attribute__((aligned(64)));
EXPORT INT	knl_num_prc = 1;		/* processors online (PRC 1..n) */
EXPORT UD	knl_secondary_sp[CNF_MAX_PRCID];	/* initial SP of each secondary (tramp.S) */

LOCAL UB	knl_pcpu_tmp_stack[CNF_MAX_PRCID][CNF_TMP_STACK_SIZE] __attribute__((aligned(16)));
LOCAL UB	knl_pcpu_irq_stack[CNF_MAX_PRCID][CNF_EXC_STACK_SIZE] __attribute__((aligned(16)));

IMPORT UD	__tmp_stack_start;		/* linker: boot stack of the primary */

/* ------------------------------------------------------------------------ */
/*
 * Big kernel lock
 *	One ticket spin lock for the whole kernel, taken with interrupts
 *	disabled. It is re-entrant on the holding processor so that a
 *	handler called from inside a critical section (cyclic handler,
 *	SVC from an interrupt) can call kernel APIs.
 */
EXPORT T_SPLOCK		knl_klock;
EXPORT volatile ID	knl_klock_holder = 0;	/* PRC ID, 0 = free */
LOCAL UW		knl_klock_depth = 0;

/*
 * Where the lock was last taken from, when, and how many times it has
 * been taken: what the lockup report (lockup.c) says of a lock held
 * too long. Written by the holder only.
 */
#if USE_KTEST
/*
 * For the tests (ktest_smp.c, test_wait_result): turns of a loop made
 * after a critical section lets go of the lock, and between a wait
 * being ended and its result being written (cpu_status.h, wait.c). They
 * make the moments in which two processors meet long enough to be met
 * every time. 0 outside that test.
 */
EXPORT volatile UW	knl_kt_unlock_spin = 0;
EXPORT volatile UW	knl_kt_release_spin = 0;
EXPORT volatile UW	knl_kt_exit_spin = 0;	/* a task's end between letting go of the lock and leaving its stack */
#endif

EXPORT volatile UBINT	knl_klock_pc = 0;
EXPORT volatile UD	knl_klock_since = 0;
EXPORT volatile UD	knl_klock_count = 0;

#if CNF_KLOCK_STAT
/*
 * What the lock costs, place by place (design 8.6.2, include/ts/klstat.h).
 * Kept under the lock itself, so nothing here needs to be atomic.
 */
#include <ts/klstat.h>
#include <ts/time.h>

#define KL_SITES	96

LOCAL T_KLSITE	kl_site[KL_SITES];
LOCAL T_KLSITE	kl_all;
LOCAL T_KLSITE	kl_rest;		/* places that found the table full */
LOCAL INT	kl_nsite = 0;
LOCAL T_KLSITE	*kl_held;		/* where the lock was taken from now */
LOCAL UD	kl_since;		/* and when */

Inline UD kl_now( void )
{
	return ReadSysCounterSync();
}

LOCAL T_KLSITE *kl_site_of( UBINT pc )
{
	INT	i;

	for ( i = 0; i < kl_nsite; i++ ) {
		if ( kl_site[i].pc == pc ) return &kl_site[i];
	}
	if ( kl_nsite == KL_SITES ) {
		return &kl_rest;
	}
	kl_site[kl_nsite].pc = pc;
	return &kl_site[kl_nsite++];
}

EXPORT void knl_klock_stat_clear( void )
{
	UINT	s = knl_lock_kernel();

	knl_memset(kl_site, 0, sizeof(kl_site));
	knl_memset(&kl_all, 0, sizeof(kl_all));
	knl_memset(&kl_rest, 0, sizeof(kl_rest));
	kl_nsite = 0;
	kl_held = &kl_rest;			/* this taking is not counted */
	knl_unlock_kernel(s);
}

EXPORT INT knl_klock_stat( T_KLSITE *all, T_KLSITE *sites, INT n )
{
	UINT	s = knl_lock_kernel();
	INT	i, k, got, used[KL_SITES];

	if ( all != NULL ) *all = kl_all;
	got = kl_nsite;
	for ( i = 0; i < kl_nsite; i++ ) used[i] = 0;
	for ( k = 0; sites != NULL && k < n && k < kl_nsite; k++ ) {
		INT	best = -1;

		for ( i = 0; i < kl_nsite; i++ ) {
			if ( !used[i] && ( best < 0 || kl_site[i].wait + kl_site[i].hold
						       > kl_site[best].wait + kl_site[best].hold ) ) {
				best = i;
			}
		}
		used[best] = 1;
		sites[k] = kl_site[best];
	}
	knl_unlock_kernel(s);
	return got;
}
#endif

EXPORT UINT knl_lock_kernel( void )
{
	UINT	intsts = disint();
	ID	me = knl_pcpu()->prcid;

	if ( knl_klock_holder == me ) {
		knl_klock_depth++;
		return intsts;
	}
#if CNF_KLOCK_STAT
	{
		UD		t0 = kl_now(), t1;
		BOOL		waited = !SpinTryLock(&knl_klock);
		T_KLSITE	*s;

		if ( waited ) {
			SpinLock(&knl_klock);
		}
		t1 = kl_now();
		s = kl_site_of((UBINT)__builtin_return_address(0));
		s->acq++;
		kl_all.acq++;
		if ( waited ) {
			s->cont++;
			s->wait += t1 - t0;
			kl_all.cont++;
			kl_all.wait += t1 - t0;
		}
		kl_held = s;
		kl_since = t1;
	}
#else
	SpinLock(&knl_klock);
#endif
	knl_klock_holder = me;
	knl_klock_depth = 1;
	knl_klock_pc = (UBINT)__builtin_return_address(0);
	knl_klock_since = ReadSysCounter();
	knl_klock_count++;
	return intsts;
}

EXPORT void knl_unlock_kernel_keep( void )
{
	if ( --knl_klock_depth == 0 ) {
#if CNF_KLOCK_STAT
		UD	held = kl_now() - kl_since;

		if ( kl_held != NULL ) kl_held->hold += held;
		kl_all.hold += held;
#endif
		knl_klock_holder = 0;
		SpinUnlock(&knl_klock);
	}
}

/*
 * Dispatcher: take the task this processor should run (dispatch.S)
 *	Done under the kernel lock so that schedtsk is consistent with the
 *	ready queue and the critical section that made the task ready has
 *	finished writing what the task reads on return (wait result, message
 *	pointer ...). Returns NULL when there is nothing to run yet or the
 *	task's context is still held by the processor that ran it last.
 *	Called with interrupts disabled on the temporary stack.
 */
/*
 * The time each task runs (TCB runcyc): the task a processor took and
 * when, charged with what has gone by when the processor gives its
 * context up (knl_acct_leave, from dispatch.S as it goes to its
 * temporary stack). Only the processor itself writes its own entry,
 * with interrupts disabled.
 */
LOCAL TCB	*acct_tsk[CNF_MAX_PRCID];
LOCAL UD	acct_at[CNF_MAX_PRCID];
LOCAL UD	acct_busy[CNF_MAX_PRCID];	/* each processor's counts spent running tasks */

Inline UD acct_now( void )
{
	return ReadSysCounterSync();
}

EXPORT void knl_acct_leave( void )
{
	INT	i = knl_pcpu()->prcid - 1;
	TCB	*t;

	if ( i < 0 || i >= CNF_MAX_PRCID ) {
		return;
	}
	t = acct_tsk[i];
	if ( t != NULL ) {
		UD	now = acct_now();

		if ( now > acct_at[i] ) {
			t->runcyc += now - acct_at[i];
			acct_busy[i] += now - acct_at[i];
		}
		acct_tsk[i] = NULL;
	}
}

/*
 * The processors online and the time each has run tasks (ts_ref_cpu,
 * design 7.3): a task running now is counted up to now
 */
EXPORT ER ts_ref_cpu( T_TSCPU *pk )
{
	UD	now = acct_now(), f = knl_cntfrq;
	INT	i;

	knl_memset(pk, 0, sizeof(*pk));
	pk->ncpu = knl_num_prc;
	pk->freq = (UW)f;
	pk->now = now;
	for ( i = 0; i < CNF_MAX_PRCID && i < TS_CPU_MAX; i++ ) {
		UD	b = __atomic_load_n(&acct_busy[i], __ATOMIC_RELAXED);
		UD	at = __atomic_load_n(&acct_at[i], __ATOMIC_RELAXED);

		if ( __atomic_load_n(&acct_tsk[i], __ATOMIC_RELAXED) != NULL && now > at ) b += now - at;
		pk->busy[i] = b;
	}
	return E_OK;
}

#if CNF_STK_WATCH
/*
 * make STKWATCH=1 (design 4.7.6): while a task is not running, nothing
 * may write over the return addresses its frames hold. The dispatcher
 * notes them as the task's processor gives its context up (dispatch.S,
 * before another processor may take the task) and reads them again as
 * a processor takes it. A difference means something wrote into the
 * stack of a task that was not running; it is said there and then,
 * with where and what, rather than when the task returns into it.
 * Only a context of the kernel's mode has frames of the kernel's; a
 * word is noted only when it points into the kernel's code.
 */
IMPORT UB	__kernel_start[], __text_end[];

EXPORT void knl_stk_snap( TCB *t )
{
	T_EXCFRAME *f = (T_EXCFRAME *)t->tskctxb.ssp;
	UBINT	hi = (UBINT)t->isstack, lo = hi - (UBINT)t->sstksz, fp, next;
	UD	v;
	INT	i, n = 0;

	t->swn = 0;
	if ( (UBINT)f < lo || (UBINT)( f + 1 ) > hi || ( f->cs & SEL_RPL3 ) != 0 ) {
		return;
	}
	fp = (UBINT)f->rbp;
	for ( i = 0; i < 32 && n < STK_WATCH_N; i++ ) {
		if ( fp < lo || fp + 16 > hi || ( fp & 0xf ) != 0 ) {
			break;
		}
		v = ((UD *)fp)[1];
		if ( v >= (UD)(UBINT)__kernel_start && v < (UD)(UBINT)__text_end ) {
			t->swaddr[n] = &((UD *)fp)[1];
			t->swval[n] = v;
			n++;
		}
		next = (UBINT)((UD *)fp)[0];
		if ( next <= fp ) {
			break;
		}
		fp = next;
	}
	t->swn = n;
	t->swprc = (UB)knl_pcpu()->prcid;
	t->swat = ReadSysCounter();
}

LOCAL void stk_check( TCB *t )
{
	INT	i;

	for ( i = 0; i < t->swn; i++ ) {
		if ( *t->swaddr[i] != t->swval[i] ) {
			knl_stack_overwritten(t, t->swaddr[i], t->swval[i], t->swprc, t->swat);
		}
	}
}
#endif

EXPORT TCB *knl_dispatch_pick( void )
{
	T_PCPU	*p = knl_pcpu();
	TCB	*tcb;

	(void)knl_lock_kernel();
	tcb = p->schedtsk;
	if ( tcb != NULL ) {
		if ( __atomic_load_n(&tcb->runprc, __ATOMIC_ACQUIRE) != 0 ) {
			tcb = NULL;
		} else {
			tcb->runprc = (UB)p->prcid;
			p->ctxtsk = tcb;
			p->ctxsw_cnt++;
			if ( p->prcid >= 1 && p->prcid <= CNF_MAX_PRCID ) {
				acct_tsk[p->prcid - 1] = tcb;
				acct_at[p->prcid - 1] = acct_now();
			}
			tcb->slice_at = acct_now();	/* its turn runs from here */
		}
	}
	knl_unlock_kernel_keep();

	/*
	 * A task that overran its stack wrote over whatever the allocator
	 * handed out below it. Nothing else can be trusted after that, so
	 * say which task it was and stop here rather than let the damage
	 * surface somewhere unrelated.
	 */
	if ( tcb != NULL && *TaskStackFoot(tcb) != TASK_STK_MAGIC ) {
		knl_stack_broken(tcb);
	}
#if CNF_STK_WATCH
	if ( tcb != NULL ) {
		stk_check(tcb);
	}
#endif

	return tcb;
}

EXPORT void knl_unlock_kernel( UINT intsts )
{
	knl_unlock_kernel_keep();
	enaint(intsts);
}

EXPORT BOOL knl_kernel_locked( void )
{
	return ( knl_klock_holder == knl_pcpu()->prcid );
}

/* ------------------------------------------------------------------------ */
/*
 * IPI
 */
EXPORT void knl_send_ipi( UW prcmask, UINT sgino )
{
	UW	targets = prcmask & ((1U << CNF_MAX_PRCID) - 1);
	INT	i;

	for ( i = 0; i < CNF_MAX_PRCID && targets != 0; i++ ) {
		if ( ( targets & ( 1U << i ) ) == 0 ) continue;
		targets &= ~( 1U << i );
		if ( knl_pcpu_tbl[i].state != PC_ONLINE && knl_pcpu_tbl[i].state != PC_BOOTING ) continue;
		knl_apic_send(knl_pcpu_tbl[i].apic_id, INTVEC_IPI(sgino), APIC_DM_FIXED);
	}
}

/*
 * SGI_DISPATCH: nothing to do here. The interrupt return path compares
 * ctxtsk with schedtsk and dispatches (int_asm.S).
 */
LOCAL void knl_ipi_dispatch_handler( UINT intno, UW iar )
{
	knl_pcpu()->ipi_cnt++;
	knl_apic_eoi();
}

/*
 * Every other processor that runs a task of the space 'ttbr0' takes an
 * interrupt before this returns. Taking an interrupt serializes the
 * processor: instructions it fetched before code of the space was
 * rewritten are thrown away. A processor that starts a task of the space
 * later enters it by IRET or SYSRET after a change of CR3, which
 * serialize it too. The interrupt is the reschedule request,
 * which changes nothing when there is nothing to change; its handler
 * counts it, and the count is what is waited for. Called with
 * interrupts enabled, so that two processors doing this at once each
 * take the other's. A processor that does not answer within a bound
 * (one stopped) is not waited for longer.
 */
#define SYNC_SPINS	20000000

EXPORT void knl_sync_cores( UD ttbr0 )
{
	T_PCPU	*me = knl_pcpu();
	UD	seen[CNF_MAX_PRCID];
	UW	mask = 0;
	INT	i, n;

	Asm("mfence" ::: "memory");
	for ( i = 0; i < knl_num_prc; i++ ) {
		T_PCPU	*p = &knl_pcpu_tbl[i];
		TCB	*t = p->ctxtsk;

		if ( p == me || p->state != PC_ONLINE || t == NULL || t->tskctxb.ttbr0 != ttbr0 ) {
			continue;
		}
		seen[i] = *(volatile UD *)&p->ipi_cnt;
		mask |= 1U << i;
	}
	if ( mask == 0 ) return;
	knl_send_ipi(mask, SGI_DISPATCH);
	for ( n = 0; n < SYNC_SPINS && mask != 0; n++ ) {
		for ( i = 0; i < knl_num_prc; i++ ) {
			if ( ( mask & ( 1U << i ) ) != 0
			  && *(volatile UD *)&knl_pcpu_tbl[i].ipi_cnt != seen[i] ) {
				mask &= ~( 1U << i );
			}
		}
		CpuRelax();
	}
}

/*
 * SGI_STOP: park this processor (panic, shutdown)
 */
LOCAL void knl_ipi_stop_handler( UINT intno, UW iar )
{
	knl_apic_eoi();
	disint();
	for (;;) {
		Asm("hlt");
	}
}

/* SGI_SYNC: an interrupt taken is all that is asked (knl_sync_cores) */
LOCAL void knl_ipi_sync_handler( UINT intno, UW iar )
{
	knl_pcpu()->ipi_cnt++;
	knl_apic_eoi();
}

/* ------------------------------------------------------------------------ */
/*
 * Scheduler: assign the ready tasks to the online processors (design 8.3)
 *	Called inside the kernel lock after every change of the ready queue.
 *	A processor with dispatch disabled keeps its current task. A task
 *	that is running stays on its processor when it is allowed to; the
 *	other processors take the remaining tasks in priority order. Every
 *	processor whose schedtsk changed, except the caller, gets an IPI.
 */
LOCAL BOOL knl_task_assigned( TCB *tcb, TCB **assign, INT n )
{
	INT	i;

	for ( i = 0; i < n; i++ ) {
		if ( assign[i] == tcb ) return TRUE;
	}
	return FALSE;
}

/* Processors whose task has just given way at the end of its turn (slice_expire) */
LOCAL UW	slice_yield = 0;

/*
 * A running task keeps its processor. Another takes, first, one whose
 * task has just given way, then an idle one, so that a turn moves no
 * task that keeps running to another processor.
 */
LOCAL INT knl_pick_prc( TCB *tcb, UW cand )
{
	INT	i;

	if ( tcb->runprc != 0 && (cand & (1U << (tcb->runprc - 1))) != 0 ) {
		return tcb->runprc - 1;
	}
	for ( i = 0; i < CNF_MAX_PRCID; i++ ) {
		if ( ( cand & slice_yield & (1U << i) ) != 0 ) return i;
	}
	for ( i = 0; i < CNF_MAX_PRCID; i++ ) {
		if ( ( cand & (1U << i) ) != 0 && knl_pcpu_tbl[i].ctxtsk == NULL ) return i;
	}
	for ( i = 0; i < CNF_MAX_PRCID; i++ ) {
		if ( cand & (1U << i) ) return i;
	}
	return -1;
}

/* ------------------------------------------------------------------------ */
/*
 * Turns among the tasks of one priority (design 4.3.1)
 *	Priorities 1..CNF_RT_PRI_LAST are the real-time band: strict
 *	priority, and FIFO within one, unless tk_chg_slt gave a task a
 *	time. Above it, the tasks of processes and of the desktop take
 *	turns of CNF_SLICE_MS with the others of their priority. The tasks
 *	of the kernel and of the drivers are never made to give way.
 *
 *	The clock stays tickless: a single timer event is set only while a
 *	task that takes turns has a processor and another task of its
 *	priority that could run there has none (the end of knl_reschedule).
 *	When it fires, each such task whose turn is over goes behind the
 *	others of its priority, as many of them as wait, and the processors
 *	are assigned again. Everything is decided holding the kernel lock.
 */
IMPORT UB	__dt_text_start[], __dt_text_end[];

LOCAL TMEB	slice_tmeb;
LOCAL BOOL	slice_armed = FALSE;

/* Whether t takes turns at all: a task of a process, or one of the desktop */
LOCAL BOOL slice_taker( TCB *t )
{
	UBINT	a = (UBINT)t->task;

	if ( t->owner > 0 && ( t->tskatr & TA_RNG3 ) >= TA_RNG2 ) {
		return TRUE;
	}
	return (BOOL)( a >= (UBINT)__dt_text_start && a < (UBINT)__dt_text_end );
}

/* t's turn (ms), 0 when it is not made to give way */
LOCAL UW slice_ms( TCB *t )
{
	BOOL	rt;

	if ( !slice_taker(t) ) {
		return 0;
	}
	rt = (BOOL)( ext_tskpri(t->priority) <= CNF_RT_PRI_LAST );
	if ( t->slice_set ) {
		return ( t->slicetime == 0 && !rt ) ? CNF_SLICE_MS : t->slicetime;
	}
	return rt ? 0 : CNF_SLICE_MS;
}

Inline UD slice_cyc( UW ms )
{
	return knl_cntfrq / 1000 * ms;
}

/* Whether a ready task of t's priority that has no processor in run[] could run on processor i */
LOCAL INT slice_waiters( TCB *t, INT i, TCB **run, INT n )
{
	QUEUE	*h = &knl_ready_queue.tskque[t->priority], *q;
	TCB	*u;
	INT	w = 0;

	for ( q = h->next; q != h; q = q->next ) {
		u = (TCB *)q;
		if ( u != t && !knl_task_assigned(u, run, n)
		  && ( u->assprc == 0 || ( u->assprc & ( 1U << i ) ) != 0 ) ) {
			w++;
		}
	}
	return w;
}

LOCAL void slice_expire( void *arg )
{
	TCB	*run[CNF_MAX_PRCID], *t;
	UB	gone[NUM_TSKPRI];
	UD	now = acct_now();
	INT	n = knl_num_prc, i;
	UW	ms;

	(void)arg;
	slice_armed = FALSE;
	for ( i = 0; i < NUM_TSKPRI; i++ ) gone[i] = 0;
	for ( i = 0; i < n; i++ ) {
		T_PCPU	*p = &knl_pcpu_tbl[i];

		run[i] = ( p->state == PC_ONLINE && p->dispatch_disabled < DDS_DISABLE ) ? p->schedtsk : NULL;
	}
	slice_yield = 0;
	for ( i = 0; i < n; i++ ) {
		t = run[i];
		if ( t == NULL || t->state != TS_READY || t->runprc == 0 || ( ms = slice_ms(t) ) == 0 ) {
			continue;
		}
		if ( now - t->slice_at < slice_cyc(ms) ) {
			continue;			/* its turn is not over */
		}
		if ( slice_waiters(t, i, run, n) <= (INT)gone[t->priority] ) {
			continue;			/* as many have given way as wait */
		}
		(void)knl_ready_queue_move_last(&knl_ready_queue, t);
		gone[t->priority]++;
		t->slice_at = now;
		slice_yield |= 1U << i;
	}
	knl_reschedule();
	slice_yield = 0;
}

/*
 * At the end of knl_reschedule: the timer event set when a task that
 * takes turns runs while another of its priority waits for it, at the
 * end of the nearest such turn
 */
LOCAL void slice_arm( TCB **assign, INT n )
{
	UD	now, left, near = 0;
	UW	ms;
	INT	i;
	TCB	*t;

	if ( slice_armed ) {
		return;
	}
	now = acct_now();
	for ( i = 0; i < n; i++ ) {
		t = assign[i];
		if ( t == NULL || knl_pcpu_tbl[i].dispatch_disabled >= DDS_DISABLE
		  || ( ms = slice_ms(t) ) == 0 || slice_waiters(t, i, assign, n) == 0 ) {
			continue;
		}
		left = slice_cyc(ms);
		if ( t->runprc != 0 && now - t->slice_at < left ) {
			left -= now - t->slice_at;
		}
		if ( near == 0 || left < near ) {
			near = left;
		}
	}
	if ( near != 0 ) {
		if ( near < slice_cyc(1) ) near = slice_cyc(1);
		knl_timer_insert_rel_ns(&slice_tmeb, near * 1000000ULL / ( knl_cntfrq / 1000 ), slice_expire, NULL);
		slice_armed = TRUE;
	}
}

EXPORT void knl_reschedule( void )
{
	TCB	*assign[CNF_MAX_PRCID];
	T_PCPU	*me = knl_pcpu();
	T_PCPU	*p;
	RDYQUE	*rq = &knl_ready_queue;
	UW	free = 0, ipi = 0;
	INT	n_free = 0, n = knl_num_prc, i, pri, prc;
	QUEUE	*q;
	TCB	*tcb;

	for ( i = 0; i < n; i++ ) {
		p = &knl_pcpu_tbl[i];
		assign[i] = NULL;
		if ( p->state != PC_ONLINE ) continue;
		if ( p->dispatch_disabled >= DDS_DISABLE && p->ctxtsk != NULL ) {
			assign[i] = p->ctxtsk;		/* held by tk_dis_dsp */
		} else {
			free |= 1U << i;
			n_free++;
		}
	}

	if ( n_free > 0 ) {
		/* The task holding the object lock goes first */
		tcb = rq->klocktsk;
		if ( tcb != NULL && !knl_task_assigned(tcb, assign, n) ) {
			prc = knl_pick_prc(tcb, ( tcb->assprc != 0 ? tcb->assprc : ~0U ) & free);
			if ( prc >= 0 ) {
				assign[prc] = tcb;
				free &= ~(1U << prc);
				n_free--;
			}
		}
		for ( pri = rq->top_priority; pri < NUM_TSKPRI && n_free > 0; pri++ ) {
			for ( q = rq->tskque[pri].next; q != &rq->tskque[pri] && n_free > 0; q = q->next ) {
				tcb = (TCB*)q;
				if ( knl_task_assigned(tcb, assign, n) ) continue;
				prc = knl_pick_prc(tcb, ( tcb->assprc != 0 ? tcb->assprc : ~0U ) & free);
				if ( prc < 0 ) continue;	/* its processors are all taken */
				assign[prc] = tcb;
				free &= ~(1U << prc);
				n_free--;
			}
		}
	}

	for ( i = 0; i < n; i++ ) {
		p = &knl_pcpu_tbl[i];
		if ( p->state != PC_ONLINE ) continue;
		if ( p->schedtsk != assign[i] ) {
			p->schedtsk = assign[i];
			if ( p != me ) ipi |= 1U << i;
		}
	}
	slice_arm(assign, n);
	if ( ipi != 0 ) {
		knl_send_ipi(ipi, SGI_DISPATCH);
	}
}

/* ------------------------------------------------------------------------ */
/*
 * Per-CPU initialization
 */
IMPORT UD	knl_gdt[];			/* boot.S */
IMPORT UB	knl_gdt_desc[];
IMPORT void	knl_idt_load( void );		/* interrupt.c */

/* The processor's own APIC ID, before its Local APIC is set up: CPUID's */
LOCAL UW cpu_apic_id( void )
{
	UW	a, b, c, d;

	cpuid(0, 0, &a, &b, &c, &d);
	if ( a >= 0xb ) {
		cpuid(0xb, 0, &a, &b, &c, &d);
		if ( a != 0 || b != 0 ) {
			return d;
		}
	}
	cpuid(1, 0, &a, &b, &c, &d);
	return b >> 24;
}

/*
 * This processor's tables and registers: its TSS (its stacks for NMI,
 * the double fault and the machine check, with the T_PCPU at the top of
 * each for knl_nmi_common) in its slot of the GDT, the IDT, the GS base,
 * SYSCALL, and the paging, x87/SSE/AVX and SMAP setup (each processor
 * as the boot processor)
 */
EXPORT void knl_cpu_init( T_PCPU *p )
{
	INT	i = p->prcid - 1, k;
	UD	base = (UD)(UBINT)&p->tss, lim = sizeof(T_TSS) - 1;
	UH	sel = (UH)GDT_TSS_SEL(i);

	knl_memset(&p->tss, 0, sizeof(p->tss));
	for ( k = 0; k < 3; k++ ) {
		UB	*top = &p->ist_stack[k][IST_STACK_SIZE - 16];

		*(T_PCPU **)top = p;
		p->tss.ist[k] = (UD)(UBINT)top;		/* IST 1, 2, 3: IST_DF, IST_NMI, IST_MC */
	}
	p->tss.iopb = sizeof(T_TSS);			/* no I/O permission bitmap */
	knl_gdt[sel / 8] = ( lim & 0xffff ) | ( ( base & 0xffffff ) << 16 ) | ( (UD)0x89 << 40 )
			 | ( ( ( lim >> 16 ) & 0xf ) << 48 ) | ( ( ( base >> 24 ) & 0xff ) << 56 );
	knl_gdt[sel / 8 + 1] = base >> 32;
	Asm("lgdt %0" :: "m"(*(UB (*)[10])knl_gdt_desc));
	Asm("ltr %0" :: "r"(sel));
	knl_idt_load();

	wrmsr(MSR_GS_BASE, (UD)(UBINT)p);
	wrmsr(MSR_KERNEL_GS_BASE, 0);			/* the user's GS base while in the kernel */
	wrmsr(MSR_STAR, ( (UD)GDT_UCODE32 << 48 ) | ( (UD)GDT_KCODE << 32 ));
	wrmsr(MSR_LSTAR, (UD)(UBINT)knl_syscall_entry);
	wrmsr(MSR_SFMASK, RFL_IF | RFL_TF | RFL_DF | RFL_AC | RFL_NT);
	knl_mmu_cpu_init();
	knl_fpu_cpu_init();
	knl_pan_setup();
}

LOCAL void knl_pcpu_setup( INT i, void *tmp_stack_top )
{
	T_PCPU	*p = &knl_pcpu_tbl[i];

	p->self = p;
	p->prcid = i + 1;
	p->apic_id = cpu_apic_id();
	p->mpidr = p->apic_id;
	p->ctxtsk = NULL;
	p->schedtsk = NULL;
	p->dispatch_disabled = DDS_ENABLE;
	p->taskindp = 0;
	p->cur_ttbr0 = 0;
	p->fpu_ctx = NULL;
	p->tmp_stack = tmp_stack_top;
	p->irq_stack = &knl_pcpu_irq_stack[i][CNF_EXC_STACK_SIZE];
	p->ctxsw_cnt = 0;
	p->ipi_cnt = 0;
	p->tlb_req = 0;
	p->tlb_ack = 0;
	p->kstack = (UD)(UBINT)tmp_stack_top;
	knl_cpu_init(p);
}

/*
 * Primary processor: called first thing in reset_main()
 */
EXPORT void knl_smp_init_primary( void )
{
	INT	i;

	for ( i = 0; i < CNF_MAX_PRCID; i++ ) {
		knl_pcpu_tbl[i].self = &knl_pcpu_tbl[i];
		knl_pcpu_tbl[i].state = PC_OFFLINE;
		knl_pcpu_tbl[i].prcid = i + 1;
	}
	knl_pcpu_setup(0, &__tmp_stack_start);
	knl_pcpu_tbl[0].state = PC_ONLINE;
	knl_num_prc = 1;
}

/*
 * Register the IPI handlers (called by knl_init_interrupt)
 */
EXPORT void knl_smp_init_interrupt( void )
{
	knl_define_inthdr(INTVEC_IPI(SGI_DISPATCH), TA_HLNG, (FP)knl_ipi_dispatch_handler);
	knl_define_inthdr(INTVEC_IPI(SGI_STOP), TA_HLNG, (FP)knl_ipi_stop_handler);
	knl_define_inthdr(INTVEC_IPI(SGI_TIMER), TA_HLNG, (FP)knl_timer_ipi_handler);
	knl_define_inthdr(INTVEC_IPI(SGI_SYNC), TA_HLNG, (FP)knl_ipi_sync_handler);
	knl_lockup_init_interrupt();
}

/* ------------------------------------------------------------------------ */
/*
 * Secondary processors
 *
 * Each starts in real mode at the page below 1MB the loader set aside
 * (tramp.S), which takes it through protected mode into long mode on a
 * PML4 of its own: the identity map of the first gigabyte, which the
 * page is in, and the kernel's half. It is started alone, after the
 * last one has come online, so the page's few words (the stack and the
 * slot) are its own while it reads them.
 */
IMPORT UB	knl_tramp_start[], knl_tramp_end[];	/* tramp.S */
IMPORT UB	knl_tramp_gdt_desc[], knl_tramp_far32[], knl_tramp_far64[];
IMPORT UB	knl_tramp_cr3[], knl_tramp_stack[], knl_tramp_entry[], knl_tramp_arg[];
IMPORT UB	knl_tramp_gdt[], knl_tramp_pm32[], knl_tramp_lm64[];

#define TRAMP_OFF(sym)	( (UBINT)(sym) - (UBINT)knl_tramp_start )

LOCAL UD	tramp_pa;

/*
 * C entry of a secondary processor (tramp.S), on the temporary stack of
 * its slot, interrupts disabled, on the trampoline's PML4
 */
EXPORT void knl_secondary_main( INT i )
{
	T_PCPU	*p = &knl_pcpu_tbl[i];

	knl_pcpu_setup(i, &knl_pcpu_tmp_stack[i][CNF_TMP_STACK_SIZE]);

	/* Process space: the empty table, like an idle primary */
	Asm("mov %0, %%cr3" :: "r"(knl_mmu_root_cr3(knl_ttbr0_null)) : "memory");
	p->cur_ttbr0 = knl_ttbr0_null;

	knl_apic_cpu_init();			/* its timer stopped: PRC 1's serves the queue */
	knl_lockup_cpu_init();

	__atomic_store_n(&p->state, PC_ONLINE, __ATOMIC_RELEASE);

	/* Pick up work that was queued before this processor came online */
	{
		UINT	s = knl_lock_kernel();
		knl_reschedule();
		knl_unlock_kernel(s);
	}

	knl_force_dispatch();			/* idle loop / first task, no return */
	for (;;) {
		Asm("hlt");
	}
}

/*
 * The trampoline's page made: its code, the GDT's and far pointers'
 * addresses as the page has them, and the PML4 after it (the loader gave
 * the page; the three tables are the three pages that follow it, below
 * 1MB too)
 */
LOCAL BOOL tramp_setup( void )
{
	UB	*t;
	UD	*pml4, *pdpt, *pd, *k;
	INT	j;

	tramp_pa = knl_bootinfo->tramp_pa;
	if ( tramp_pa == 0 || tramp_pa + 4 * PAGE_SIZE > 0x100000 ) {
		return FALSE;
	}
	t = (UB *)PA2VA(tramp_pa);
	knl_memcpy(t, knl_tramp_start, (SZ)( knl_tramp_end - knl_tramp_start ));
	*(UW *)( t + TRAMP_OFF(knl_tramp_gdt_desc) + 2 ) = (UW)( tramp_pa + TRAMP_OFF(knl_tramp_gdt) );
	*(UW *)( t + TRAMP_OFF(knl_tramp_far32) ) = (UW)( tramp_pa + TRAMP_OFF(knl_tramp_pm32) );
	*(UW *)( t + TRAMP_OFF(knl_tramp_far64) ) = (UW)( tramp_pa + TRAMP_OFF(knl_tramp_lm64) );

	pml4 = (UD *)PA2VA(tramp_pa + PAGE_SIZE);
	pdpt = (UD *)PA2VA(tramp_pa + 2 * PAGE_SIZE);
	pd = (UD *)PA2VA(tramp_pa + 3 * PAGE_SIZE);
	knl_memset(pml4, 0, PAGE_SIZE);
	knl_memset(pdpt, 0, PAGE_SIZE);
	for ( j = 0; j < PT_ENTRIES; j++ ) {
		pd[j] = ( (UD)j << L2_SHIFT ) | PTE_P | PTE_RW | PTE_PS;	/* the first 1GB, executable */
	}
	pdpt[0] = ( tramp_pa + 3 * PAGE_SIZE ) | PTE_P | PTE_RW;
	pml4[0] = ( tramp_pa + 2 * PAGE_SIZE ) | PTE_P | PTE_RW;
	k = (UD *)PA2VA(knl_pt_l0_k_pa);
	for ( j = PT_USER_ENTRIES; j < PT_ENTRIES; j++ ) {
		pml4[j] = k[j];
	}
	*(UW *)( t + TRAMP_OFF(knl_tramp_cr3) ) = (UW)( tramp_pa + PAGE_SIZE );
	*(UD *)( t + TRAMP_OFF(knl_tramp_entry) ) = (UD)(UBINT)knl_secondary_main;
	return TRUE;
}

/*
 * Start the secondary processors (called from the kernel initialization
 * after the timer is running, interrupts disabled): the processors of
 * the MADT, the boot processor first, each with INIT, a wait of 10ms,
 * and two start-up IPIs 200us apart
 */
EXPORT void knl_smp_start_secondaries( void )
{
#if USE_SMP
	INT	i, n = knl_acpi.ncpu;
	UD	t0;
	UB	*t;

	if ( n > CNF_MAX_PRCID ) n = CNF_MAX_PRCID;
	if ( n <= 1 || !tramp_setup() ) {
		tm_printf((UB*)"TessronOS: 1 of %d CPUs online\n", ( n > 0 ) ? n : 1);
		return;
	}
	t = (UB *)PA2VA(tramp_pa);

	for ( i = 1; i < n; i++ ) {
		T_PCPU	*p = &knl_pcpu_tbl[i];
		UW	id = knl_acpi.cpu[i].apic_id;

		knl_secondary_sp[i] = (UD)(UBINT)&knl_pcpu_tmp_stack[i][CNF_TMP_STACK_SIZE];
		*(UD *)( t + TRAMP_OFF(knl_tramp_stack) ) = knl_secondary_sp[i];
		*(UD *)( t + TRAMP_OFF(knl_tramp_arg) ) = (UD)i;
		p->state = PC_BOOTING;
		p->apic_id = id;
		Asm("mfence" ::: "memory");

		knl_apic_send(id, 0, APIC_DM_INIT);
		WaitUsec(10000);
		knl_apic_send(id, (UW)( tramp_pa >> 12 ), APIC_DM_SIPI);
		WaitUsec(200);
		knl_apic_send(id, (UW)( tramp_pa >> 12 ), APIC_DM_SIPI);

		t0 = knl_get_mono_ns();
		while ( __atomic_load_n(&p->state, __ATOMIC_ACQUIRE) != PC_ONLINE ) {
			if ( knl_get_mono_ns() - t0 > 200000000ULL ) break;	/* 200ms */
			CpuRelax();
		}
		if ( p->state != PC_ONLINE ) {
			tm_printf((UB*)"TessronOS: CPU%d (APIC %d) did not come online\n", i, id);
			p->state = PC_OFFLINE;
			continue;
		}
		knl_num_prc = i + 1;
	}
	tm_printf((UB*)"TessronOS: %d of %d CPUs online\n", knl_num_prc, knl_acpi.ncpu);
#endif /* USE_SMP */
}

/* ------------------------------------------------------------------------ */
/*
 * tk_get_prc: ID of the processor executing the caller
 */
SYSCALL ID tk_get_prc( void )
{
	return knl_pcpu()->prcid;
}

#endif /* CPU_CORE_X86_64 */
