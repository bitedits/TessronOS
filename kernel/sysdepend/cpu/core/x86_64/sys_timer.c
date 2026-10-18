/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys_timer.c (x86-64)
 *	The monotonic clock and the rate of the time stamp counter.
 *
 *	The rate is what CPUID leaf 15h says when it says it all (the
 *	crystal's rate and the ratio to it); otherwise it is measured, over
 *	50ms, against the HPET, else the ACPI PM timer, else channel 2 of
 *	the 8254.
 */

#include <sys/machine.h>

#ifdef CPU_CORE_X86_64

#include "kernel.h"
#include <tm/tmonitor.h>
#include "../../../sysdepend.h"
#include "sys_timer.h"

EXPORT UD	knl_hrt_base = 0;
EXPORT UD	knl_hrt_min_delta = 64;

/*
 * Monotonic clock: nanoseconds since the timer was started
 */
EXPORT UD knl_get_mono_ns( void )
{
	return knl_cycles_to_ns(knl_read_cntpct() - knl_hrt_base);
}

#define CAL_MS		50

/* The HPET's main counter: its period (fs) from its capabilities */
LOCAL UD cal_hpet( void )
{
	UBINT	h = DEV_BASE(knl_acpi.hpet_addr);
	UD	period = in_d(h + 0x00) >> 32, ticks, c0, c1, t0, t1;

	if ( period == 0 || period > 100000000ULL ) {
		return 0;
	}
	out_d(h + 0x10, in_d(h + 0x10) | 1);		/* the counter running */
	ticks = (UD)CAL_MS * 1000000000000ULL / period;
	c0 = in_d(h + 0xf0);
	t0 = ReadSysCounterSync();
	do {
		c1 = in_d(h + 0xf0);
	} while ( c1 - c0 < ticks );
	t1 = ReadSysCounterSync();
	return (UD)( (unsigned __int128)( t1 - t0 ) * 1000000000000000ULL / ( ( c1 - c0 ) * period ) );
}

/* The ACPI PM timer: 3.579545 MHz, 24 or 32 bits */
LOCAL UD cal_pmtmr( void )
{
	UH	port = (UH)knl_acpi.pm_tmr;
	UW	mask = knl_acpi.pm_tmr32 ? 0xffffffffU : 0x00ffffffU;
	UW	c0, c, last;
	UD	done = 0, want = 3579545ULL * CAL_MS / 1000, t0, t1;

	c0 = last = in_port_w(port) & mask;
	t0 = ReadSysCounterSync();
	while ( done < want ) {
		c = in_port_w(port) & mask;
		done += ( c - last ) & mask;
		last = c;
	}
	t1 = ReadSysCounterSync();
	(void)c0;
	return ( t1 - t0 ) * 3579545ULL / done;
}

/* Channel 2 of the 8254, gated by port 61h, counting down from 1.193182 MHz */
LOCAL UD cal_pit( void )
{
	UW	n = 1193182 * CAL_MS / 1000;
	UD	t0, t1;
	UB	g;

	g = in_port_b(0x61);
	out_port_b(0x61, ( g & ~0x02 ) | 0x01);		/* gate on, speaker off */
	out_port_b(0x43, 0xb0);				/* channel 2, lo/hi, mode 0 */
	out_port_b(0x42, (UB)n);
	out_port_b(0x42, (UB)( n >> 8 ));
	t0 = ReadSysCounterSync();
	while ( ( in_port_b(0x61) & 0x20 ) == 0 ) {
		;					/* OUT2 goes high at the count's end */
	}
	t1 = ReadSysCounterSync();
	out_port_b(0x61, g);
	return ( t1 - t0 ) * 1000 / CAL_MS;
}

EXPORT void knl_tsc_calibrate( void )
{
	UW		a, b, c, d;
	UD		f = 0;
	CONST char	*how = "CPUID";

	cpuid(0, 0, &a, &b, &c, &d);
	if ( a >= 0x15 ) {
		cpuid(0x15, 0, &a, &b, &c, &d);
		if ( a != 0 && b != 0 && c != 0 ) {
			f = (UD)c * b / a;
		}
	}
	if ( f == 0 && knl_acpi.hpet_addr != 0 ) {
		f = cal_hpet();
		how = "HPET";
	}
	if ( f == 0 && knl_acpi.pm_tmr != 0 ) {
		f = cal_pmtmr();
		how = "PM timer";
	}
	if ( f == 0 ) {
		f = cal_pit();
		how = "8254";
	}
	knl_cntfrq = f;
	cpuid(0x80000000, 0, &a, &b, &c, &d);
	if ( a >= 0x80000007 ) {
		cpuid(0x80000007, 0, &a, &b, &c, &d);
	} else {
		d = 0;
	}
	tm_printf((UB *)"TessronOS x64: TSC %d kHz (%s)%s\n", (UW)( f / 1000 ), how,
		  ( d & ( 1U << 8 ) ) ? ", invariant" : ", NOT invariant");
}

#endif /* CPU_CORE_X86_64 */
