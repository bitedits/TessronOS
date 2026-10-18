/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cpuclk.c
 *	The processors' clock: the limit of the machine's settings and the
 *	fan's slowing, given to the firmware (include/ts/cpuclk.h, design
 *	3.7)
 *
 *	Three things say how fast the processors may run: the firmware, the
 *	most and the least it allows; the machine's setting CPU_MHZ, which
 *	the person sets with システム環境設定; and the fan's task, which slows
 *	them when it cannot keep them cool. The clock asked for is the least
 *	of the first two's most and the fan's, and not below the firmware's
 *	least, so the fan's slowing never lifts the clock above the limit,
 *	and its end brings it back to the limit, not to the most.
 *
 *	Until the settings are read the limit is CPU_MHZ_DEFAULT: the start
 *	never runs the processors faster than that, and a machine whose
 *	settings say more is brought up to it as they are read.
 *
 *	On a PC the clock is the processor's own to choose (HWP), and
 *	nothing here sets it yet: the limit is kept and nothing is set, as
 *	on a machine without the Raspberry Pi's firmware. Calls are one at
 *	a time, the limit and the fan coming from different tasks: the clock
 *	asked for and what the firmware took belong together.
 */

#include <sys/machine.h>

#ifdef CPU_CORE_X86_64

#include "kernel.h"
#include <tm/tmonitor.h>
#include <ts/cpuclk.h>

#define MHZ		1000000U
#define LIMIT_MHZ_MAX	4000		/* above any clock the firmware allows; UW keeps Hz below 4.29GHz */

/* No firmware to ask */
WEAK_FUNC EXPORT BOOL knl_cpu_hw_range( UW *p_min, UW *p_max )
{
	(void)p_min;
	(void)p_max;
	return FALSE;
}

WEAK_FUNC EXPORT ER knl_cpu_hw_set( UW hz, UW *p_got )
{
	(void)hz;
	(void)p_got;
	return E_NOSPT;
}

WEAK_FUNC EXPORT ER knl_cpu_hw_now( UW *p_hz )
{
	(void)p_hz;
	return E_NOSPT;
}

WEAK_FUNC EXPORT ER knl_cpu_hw_throttled( UW *p_val )
{
	(void)p_val;
	return E_NOSPT;
}

LOCAL struct {
	UW	limit_mhz;
	UW	heat_hz;
	UW	want_hz;		/* what was last asked; 0 nothing yet */
	UW	got_hz;			/* what the firmware answered it */
	UW	min_hz, max_hz;		/* asked once */
	BOOL	ranged;
	BOOL	told;			/* a limit of the settings told on the console */
	UW	taken, applied;
} clk = { CPU_MHZ_DEFAULT, 0, 0, 0, 0, 0, FALSE, FALSE, 0, 0 };

LOCAL T_SPLOCK	clk_lock;

LOCAL void clk_take( void )
{
	while ( !SpinTryLock(&clk_lock) ) {
		if ( tk_dly_tsk(1) != E_OK ) {
			CpuRelax();		/* not in a task: no one else holds it */
		}
	}
}

EXPORT UW knl_cpu_want( UW limit_mhz, UW heat_hz, UW min_hz, UW max_hz )
{
	UW	want = max_hz;

	if ( limit_mhz > LIMIT_MHZ_MAX ) {
		limit_mhz = LIMIT_MHZ_MAX;
	}
	if ( limit_mhz != 0 && ( want == 0 || limit_mhz * MHZ < want ) ) {
		want = limit_mhz * MHZ;
	}
	if ( heat_hz != 0 && ( want == 0 || heat_hz < want ) ) {
		want = heat_hz;
	}
	if ( min_hz != 0 && want != 0 && want < min_hz ) {
		want = min_hz;
	}
	return want;
}

LOCAL void range_once( void )
{
	if ( !clk.ranged ) {
		clk.ranged = knl_cpu_hw_range(&clk.min_hz, &clk.max_hz);
	}
}

/*
 * The clock set as the limit and the fan say, with the lock held. The
 * firmware is asked only when what is asked for changes: the settings
 * are taken again whenever any line of them is written. FALSE when
 * there is no firmware.
 */
LOCAL BOOL clk_apply( void )
{
	UW	want, got = 0;

	range_once();
	if ( !clk.ranged ) {
		return FALSE;
	}
	want = knl_cpu_want(clk.limit_mhz, clk.heat_hz, clk.min_hz, clk.max_hz);
	if ( want == 0 || want == clk.want_hz ) {
		return TRUE;
	}
	if ( knl_cpu_hw_set(want, &got) >= E_OK ) {
		clk.want_hz = want;
		clk.got_hz = got;
		clk.applied++;
	}
	return TRUE;
}

EXPORT void knl_cpu_start( void )
{
	UW	was = 0, now = 0;

	(void)knl_cpu_hw_now(&was);
	clk_take();
	clk.taken++;
	if ( !clk_apply() ) {
		SpinUnlock(&clk_lock);
		return;
	}
	SpinUnlock(&clk_lock);
	(void)knl_cpu_hw_now(&now);
	tm_printf((UB *)"TessronOS: processors at %d MHz (were %d, the firmware allows %d to %d, limit %d until the settings are read)\n",
		  (INT)( now / MHZ ), (INT)( was / MHZ ), (INT)( clk.min_hz / MHZ ),
		  (INT)( clk.max_hz / MHZ ), CPU_MHZ_DEFAULT);
}

EXPORT ER knl_cpu_limit( UW mhz )
{
	BOOL	changed, fw;
	UW	now = 0;

	if ( mhz > LIMIT_MHZ_MAX ) {
		mhz = LIMIT_MHZ_MAX;
	}
	clk_take();
	clk.taken++;
	changed = ( mhz != clk.limit_mhz || !clk.told );
	clk.limit_mhz = mhz;
	clk.told = TRUE;
	fw = clk_apply();
	SpinUnlock(&clk_lock);
	if ( !changed ) {
		return E_OK;
	}
	if ( !fw ) {
		tm_printf((UB *)"cpu: limit %d MHz%s (no firmware to set the clock)\n",
			  (INT)mhz, ( mhz == 0 ) ? " (the most)" : "");
		return E_OK;
	}
	(void)knl_cpu_hw_now(&now);
	tm_printf((UB *)"cpu: limit %d MHz%s; processors at %d MHz (the firmware allows %d to %d)\n",
		  (INT)mhz, ( mhz == 0 ) ? " (the most)" : "", (INT)( now / MHZ ),
		  (INT)( clk.min_hz / MHZ ), (INT)( clk.max_hz / MHZ ));
	return E_OK;
}

EXPORT ER knl_cpu_heat( UW hz, UW *p_got )
{
	ER	er = E_OK;

	clk_take();
	clk.heat_hz = hz;
	if ( !clk_apply() ) {
		er = E_NOSPT;
	} else if ( p_got != NULL ) {
		*p_got = clk.got_hz;
	}
	SpinUnlock(&clk_lock);
	return er;
}

EXPORT void knl_cpu_clock( T_CPUCLK *st )
{
	clk_take();
	range_once();
	st->limit_mhz = clk.limit_mhz;
	st->want_hz = clk.want_hz;
	st->min_hz = clk.min_hz;
	st->max_hz = clk.max_hz;
	st->heat_hz = clk.heat_hz;
	st->taken = clk.taken;
	st->applied = clk.applied;
	SpinUnlock(&clk_lock);
	st->hz = 0;
	if ( knl_cpu_hw_now(&st->hz) < E_OK ) {
		st->hz = 0;
	}
	if ( knl_cpu_hw_throttled(&st->throttled) < E_OK ) {
		st->throttled = 0xFFFFFFFFU;
	}
}

#endif /* CPU_CORE_X86_64 */
