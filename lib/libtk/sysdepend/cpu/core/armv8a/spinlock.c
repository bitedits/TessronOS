/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	spinlock.c (ARMv8-A / AArch64)
 *	Spin locks, atomic operations and memory barriers (design 8.7).
 *
 *	The spin lock is a ticket lock: a waiter takes a ticket, then
 *	watches the owner counter with a load-exclusive so that the store
 *	of the releasing processor wakes it from WFE.
 */

#include <sys/machine.h>

#ifdef CPU_CORE_ARMV8A

#include <tk/tkernel.h>

EXPORT void InitSpinLock( T_SPLOCK *lock )
{
	lock->next = 0;
	lock->owner = 0;
}

/*
 * The locks each processor holds, a few deep, with the place each was
 * taken from and when: what the lockup report (lockup.c) says a
 * processor that stopped is holding. A lock taken deeper than the table
 * goes is held all the same, only not shown. The processor is the one
 * TPIDR_EL1 names; before the per-processor data is set up it names
 * none, and nothing is kept.
 */
EXPORT T_SPHELD	knl_spin_held[SPIN_HELD_PRC][SPIN_HELD_N];

/*
 * A waiter that has not had the lock for knl_spin_stall_cycles of the
 * system counter calls knl_spin_stall_hook (lockup.c), which says so on
 * the console straight away, and goes on waiting; it is called again
 * each time as long again has passed, and once more with 'waited' 0
 * when the lock comes at last. 0 cycles: never. The generic timer's
 * event stream wakes a waiter from WFE now and then, so that it gets to
 * look at the time while nobody lets go of the lock.
 */
EXPORT UD	knl_spin_stall_cycles = 0;
EXPORT void	(*knl_spin_stall_hook)( T_SPLOCK *lock, UBINT pc, UD waited ) = NULL;

Inline INT spin_prc( void )
{
	UD	p;

	Asm("mrs %0, tpidr_el1" : "=r"(p));
	return ( p != 0 ) ? *(ID *)p - 1 : -1;		/* T_PCPU starts with its prcid */
}

Inline UD spin_now( void )
{
	UD	t;

	Asm("mrs %0, cntpct_el0" : "=r"(t) :: "memory");
	return t;
}

LOCAL void spin_note( T_SPLOCK *lock, UBINT pc )
{
	INT	c = spin_prc(), i;

	if ( c < 0 || c >= SPIN_HELD_PRC ) {
		return;
	}
	for ( i = 0; i < SPIN_HELD_N; i++ ) {
		if ( knl_spin_held[c][i].lock == NULL ) {
			knl_spin_held[c][i].pc = pc;
			knl_spin_held[c][i].since = spin_now();
			knl_spin_held[c][i].lock = lock;
			return;
		}
	}
}

/* Let go of where it was taken: as a rule here, else on the processor that took it */
LOCAL void spin_forget( T_SPLOCK *lock )
{
	INT	c = spin_prc(), i, k;

	if ( c < 0 || c >= SPIN_HELD_PRC ) {
		return;
	}
	for ( k = 0; k < SPIN_HELD_PRC; k++, c = ( c + 1 ) % SPIN_HELD_PRC ) {
		for ( i = SPIN_HELD_N - 1; i >= 0; i-- ) {
			if ( knl_spin_held[c][i].lock == lock ) {
				knl_spin_held[c][i].lock = NULL;
				return;
			}
		}
	}
}

LOCAL void spin_acquire( T_SPLOCK *lock, UBINT pc )
{
	UW	ticket = __atomic_fetch_add(&lock->next, 1, __ATOMIC_RELAXED);
	UW	cur;
	UD	t0 = 0, last = 0, lim = knl_spin_stall_cycles, now;
	BOOL	told = FALSE;

	for (;;) {
		Asm("ldaxr %w0, [%1]" : "=r"(cur) : "r"(&lock->owner) : "memory");
		if ( cur == ticket ) break;
		if ( lim != 0 ) {
			now = spin_now();
			if ( t0 == 0 ) {
				t0 = last = now;
			} else if ( now - last >= lim && knl_spin_stall_hook != NULL ) {
				last = now;
				told = TRUE;
				(*knl_spin_stall_hook)(lock, pc, now - t0);
				continue;	/* the exclusive monitor is armed again first */
			}
		}
		Asm("wfe" ::: "memory");
	}
	if ( told && knl_spin_stall_hook != NULL ) {
		(*knl_spin_stall_hook)(lock, pc, 0);
	}
	spin_note(lock, pc);
}

EXPORT void SpinLock( T_SPLOCK *lock )
{
	spin_acquire(lock, (UBINT)__builtin_return_address(0));
}

LOCAL BOOL spin_try( T_SPLOCK *lock, UBINT pc )
{
	UW	n = __atomic_load_n(&lock->next, __ATOMIC_RELAXED);

	if ( __atomic_load_n(&lock->owner, __ATOMIC_ACQUIRE) != n ) {
		return FALSE;
	}
	if ( !__atomic_compare_exchange_n(&lock->next, &n, n + 1, FALSE,
					  __ATOMIC_ACQUIRE, __ATOMIC_RELAXED) ) {
		return FALSE;
	}
	spin_note(lock, pc);
	return TRUE;
}

EXPORT BOOL SpinTryLock( T_SPLOCK *lock )
{
	return spin_try(lock, (UBINT)__builtin_return_address(0));
}

EXPORT void SpinUnlock( T_SPLOCK *lock )
{
	spin_forget(lock);
	__atomic_store_n(&lock->owner, lock->owner + 1, __ATOMIC_RELEASE);
	Asm("dsb ish\n\tsev" ::: "memory");
}

EXPORT void ISpinLock( T_SPLOCK *lock, UINT *intsts )
{
	*intsts = disint();
	spin_acquire(lock, (UBINT)__builtin_return_address(0));
}

EXPORT BOOL ISpinTryLock( T_SPLOCK *lock, UINT *intsts )
{
	*intsts = disint();
	if ( spin_try(lock, (UBINT)__builtin_return_address(0)) ) {
		return TRUE;
	}
	enaint(*intsts);
	return FALSE;
}

EXPORT void ISpinUnlock( T_SPLOCK *lock, UINT *intsts )
{
	SpinUnlock(lock);
	enaint(*intsts);
}

/*
 * Atomic operations (return values as in the SMP T-Kernel specification:
 * inc/dec/add/sub return the new value, the others the previous value)
 */
EXPORT UW atomic_inc( UW *addr )
{
	return __atomic_add_fetch(addr, 1, __ATOMIC_ACQ_REL);
}

EXPORT UW atomic_dec( UW *addr )
{
	return __atomic_sub_fetch(addr, 1, __ATOMIC_ACQ_REL);
}

EXPORT UW atomic_add( UW *addr, UW val )
{
	return __atomic_add_fetch(addr, val, __ATOMIC_ACQ_REL);
}

EXPORT UW atomic_sub( UW *addr, UW val )
{
	return __atomic_sub_fetch(addr, val, __ATOMIC_ACQ_REL);
}

EXPORT UW atomic_xchg( UW *addr, UW val )
{
	return __atomic_exchange_n(addr, val, __ATOMIC_ACQ_REL);
}

EXPORT UW atomic_cmpxchg( UW *addr, UW val, UW cmp )
{
	__atomic_compare_exchange_n(addr, &cmp, val, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	return cmp;			/* the value found at addr */
}

EXPORT UW atomic_bitset( UW *addr, UW setptn )
{
	return __atomic_fetch_or(addr, setptn, __ATOMIC_ACQ_REL);
}

EXPORT UW atomic_bitclr( UW *addr, UW clrptn )
{
	return __atomic_fetch_and(addr, clrptn, __ATOMIC_ACQ_REL);
}

EXPORT UD atomic64_add( UD *addr, UD val )
{
	return __atomic_add_fetch(addr, val, __ATOMIC_ACQ_REL);
}

EXPORT UD atomic64_xchg( UD *addr, UD val )
{
	return __atomic_exchange_n(addr, val, __ATOMIC_ACQ_REL);
}

EXPORT UD atomic64_cmpxchg( UD *addr, UD val, UD cmp )
{
	__atomic_compare_exchange_n(addr, &cmp, val, FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	return cmp;
}

EXPORT void MemoryBarrier( void )
{
	Asm("dmb ish" ::: "memory");
}

EXPORT void ReadBarrier( void )
{
	Asm("dmb ishld" ::: "memory");
}

EXPORT void WriteBarrier( void )
{
	Asm("dmb ishst" ::: "memory");
}

#endif /* CPU_CORE_ARMV8A */
