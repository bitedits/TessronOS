/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	syslib.h
 *	System library dependent definition (ARMv8-A / AArch64)
 */

#ifndef __TK_SYSLIB_DEPEND_CORE_H__
#define __TK_SYSLIB_DEPEND_CORE_H__

#include <tk/errno.h>
#include <sys/sysdef.h>

/*
 * Interrupt enable/disable (PSTATE.I)
 *	disint() returns the previous I mask so that enaint() can restore it.
 */
IMPORT UW disint( void );
IMPORT void enaint( UW intsts );

#define DI(intsts)	( (intsts) = (UINT)disint() )
#define EI(intsts)	( enaint((UW)intsts) )
#define isDI(intsts)	( (intsts) != 0 )

/*
 * Interrupt mode (SetIntMode)
 */
#define	IM_LEVEL	0x00	/* level (high) detection */
#define	IM_EDGE		0x01	/* rising edge detection */

/*
 * Spin locks, atomic operations, barriers (spinlock.c, design 8.7)
 */
typedef struct {
	UW	next;		/* next ticket */
	UW	owner;		/* ticket being served */
} T_SPLOCK;

IMPORT void InitSpinLock( T_SPLOCK *lock );
IMPORT void SpinLock( T_SPLOCK *lock );
IMPORT BOOL SpinTryLock( T_SPLOCK *lock );
IMPORT void SpinUnlock( T_SPLOCK *lock );
IMPORT void ISpinLock( T_SPLOCK *lock, UINT *intsts );
IMPORT BOOL ISpinTryLock( T_SPLOCK *lock, UINT *intsts );
IMPORT void ISpinUnlock( T_SPLOCK *lock, UINT *intsts );

/*
 * What each processor holds, and what a waiter that waits too long
 * calls (spinlock.c, the lockup report of design 8.10)
 */
typedef struct {
	T_SPLOCK * volatile lock;	/* NULL: the entry is free */
	UD	pc;			/* where it was taken */
	UD	since;			/* the system counter then */
} T_SPHELD;

#define SPIN_HELD_PRC	8
#define SPIN_HELD_N	4

IMPORT T_SPHELD	knl_spin_held[SPIN_HELD_PRC][SPIN_HELD_N];
IMPORT UD	knl_spin_stall_cycles;
IMPORT void	(*knl_spin_stall_hook)( T_SPLOCK *lock, UBINT pc, UD waited );
IMPORT UW atomic_inc( UW *addr );
IMPORT UW atomic_dec( UW *addr );
IMPORT UW atomic_add( UW *addr, UW val );
IMPORT UW atomic_sub( UW *addr, UW val );
IMPORT UW atomic_xchg( UW *addr, UW val );
IMPORT UW atomic_cmpxchg( UW *addr, UW val, UW cmp );
IMPORT UW atomic_bitset( UW *addr, UW setptn );
IMPORT UW atomic_bitclr( UW *addr, UW clrptn );
IMPORT UD atomic64_add( UD *addr, UD val );
IMPORT UD atomic64_xchg( UD *addr, UD val );
IMPORT UD atomic64_cmpxchg( UD *addr, UD val, UD cmp );
IMPORT void MemoryBarrier( void );
IMPORT void ReadBarrier( void );
IMPORT void WriteBarrier( void );

/*
 * Barriers and cache maintenance for drivers and the common parts of
 * the kernel, inline (docs/tessronos/04-kernel.md 4.8). What a driver
 * needs of the processor around a device is said with these, so that
 * another architecture gives its own meaning to the same names.
 *
 *	DeviceBarrier	every earlier access to memory and to a device's
 *			registers is complete before any later one (DSB SY):
 *			before telling a device of what was written for it,
 *			and after writing a register that must take effect
 *	SmpMemBarrier	among the processors, earlier loads and stores
 *			before later ones (DMB ISH)
 *	SmpWriteBarrier	among the processors, earlier stores before
 *			later ones (DMB ISHST)
 *	CpuRelax	a moment's pause in a loop that spins (YIELD)
 *	DCacheFlushRange what the processor wrote of [p, p+len) out to
 *			memory, and no line of it left in the cache, so
 *			that a bus master that is not coherent with the
 *			cache sees what was written and the processor then
 *			sees what the master wrote (DC CIVAC by line)
 */
#define DCACHE_LINE	64		/* the smallest data cache line of the processors */

Inline void DeviceBarrier( void )
{
	__asm__ volatile ( "dsb sy" ::: "memory" );
}

Inline void SmpMemBarrier( void )
{
	__asm__ volatile ( "dmb ish" ::: "memory" );
}

Inline void SmpWriteBarrier( void )
{
	__asm__ volatile ( "dmb ishst" ::: "memory" );
}

Inline void CpuRelax( void )
{
	__asm__ volatile ( "yield" );
}

Inline void DCacheFlushRange( CONST void *p, UBINT len )
{
	UBINT	a = (UBINT)p & ~(UBINT)( DCACHE_LINE - 1 ), end = (UBINT)p + len;

	__asm__ volatile ( "dsb sy" ::: "memory" );
	for ( ; a < end; a += DCACHE_LINE ) {
		__asm__ volatile ( "dc civac, %0" :: "r"(a) : "memory" );
	}
	__asm__ volatile ( "dsb sy" ::: "memory" );
}

/*
 * The system counter (CNTPCT_EL0), which runs at the same rate on every
 * processor and never goes back: ReadSysCounterSync keeps the read
 * behind the instructions before it (ISB), ReadSysCounter does not.
 * SysCounterFreq is its rate in Hz (CNTFRQ_EL0), 0 when the firmware
 * left it unset. CpuHwId is the processor's own number as the hardware
 * names it (the affinity fields of MPIDR_EL1), known before the kernel
 * has numbered the processors.
 */
Inline UD ReadSysCounter( void )
{
	UD	c;

	__asm__ volatile ( "mrs %0, cntpct_el0" : "=r"(c) );
	return c;
}

Inline UD ReadSysCounterSync( void )
{
	UD	c;

	__asm__ volatile ( "isb; mrs %0, cntpct_el0" : "=r"(c) :: "memory" );
	return c;
}

Inline UD SysCounterFreq( void )
{
	UD	f;

	__asm__ volatile ( "mrs %0, cntfrq_el0" : "=r"(f) );
	return f;
}

Inline UW CpuHwId( void )
{
	UD	m;

	__asm__ volatile ( "mrs %0, mpidr_el1" : "=r"(m) );
	return (UW)( m & 0xFFFFFF );
}


/*
 * I/O port access
 *	Addresses are 40 bit physical (or kernel virtual) values, hence UBINT.
 */
Inline void out_d( UBINT port, UD data )
{
	*(_UD*)port = data;
}
Inline void out_w( UBINT port, UW data )
{
	*(_UW*)port = data;
}
Inline void out_h( UBINT port, UH data )
{
	*(_UH*)port = data;
}
Inline void out_b( UBINT port, UB data )
{
	*(_UB*)port = data;
}

Inline UD in_d( UBINT port )
{
	return *(_UD*)port;
}
Inline UW in_w( UBINT port )
{
	return *(_UW*)port;
}
Inline UH in_h( UBINT port )
{
	return *(_UH*)port;
}
Inline UB in_b( UBINT port )
{
	return *(_UB*)port;
}

Inline void and_w( UBINT port, UW data )
{
	*(_UW*)port &= data;
}
Inline void and_h( UBINT port, UH data )
{
	*(_UH*)port &= data;
}
Inline void and_b( UBINT port, UB data )
{
	*(_UB*)port &= data;
}

Inline void or_w( UBINT port, UW data )
{
	*(_UW*)port |= data;
}
Inline void or_h( UBINT port, UH data )
{
	*(_UH*)port |= data;
}
Inline void or_b( UBINT port, UB data )
{
	*(_UB*)port |= data;
}

#endif /* __TK_SYSLIB_DEPEND_CORE_H__ */
