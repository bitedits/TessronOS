/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	wusec_x86_64.c
 *	Busy wait on the time stamp counter. Before the kernel has measured
 *	its rate (knl_cntfrq), a rate of 3GHz is taken: the wait is then
 *	longer than asked on a slower processor, never shorter than about a
 *	third of it.
 */

#include <sys/machine.h>

#ifdef CPU_CORE_X86_64

#include <tk/tkernel.h>

EXPORT void WaitUsec( UW usec )
{
	UD	frq = ( knl_cntfrq != 0 ) ? knl_cntfrq : 3000000000ULL;
	UD	ticks = ( (UD)usec * frq ) / 1000000ULL;
	UD	start = ReadSysCounterSync();

	while ( ReadSysCounterSync() - start < ticks ) {
		CpuRelax();
	}
}

#endif /* CPU_CORE_X86_64 */
