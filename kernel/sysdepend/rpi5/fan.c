/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	fan.c
 *	The Raspberry Pi 5's fan, run by the processor's temperature
 *	(docs/tessronos/03-platform.md 3.7)
 *
 *	The fan connector's PWM line is driven from RP1 GPIO 45 through a
 *	transistor that inverts it: the pin low lets the fan run at full
 *	speed, and the pin's pull-down keeps it at full speed until the pin
 *	is driven. The board's device tree gives the signal as channel 3 of
 *	the RP1's second PWM block at a period of 41566ns with the output
 *	inverted, with four steps of the processor's temperature. Those the
 *	device tree gives (50 to 75 degrees, the fan at 75 to 250 of 255)
 *	are for a board left at its starting clock; the processors run here
 *	at the most the firmware allows, so the fan is run earlier and
 *	harder: 40, 50, 57.5 and 65 degrees, each left again 3 degrees below,
 *	at 120, 170, 220 and 255 of 255. Below the first it stops.
 *
 *	Should the fan not be enough, the processors are slowed before the
 *	firmware has to: to 1800MHz at 78 degrees, to 1500MHz at 82, and back
 *	below 70. The slowing goes through the limit of the machine's
 *	settings (cpuclk.c): the clock is never set above the limit, so with
 *	a limit of 1800MHz or less the first step leaves it as it is, and
 *	coming back takes it to the limit rather than to the most.
 *
 *	Which of the pin's functions is the PWM output, and which source the
 *	block's clock is to be made from, are not in the RP1's public tables,
 *	so both are found by trying them: with the channel at half duty each
 *	source is selected in turn, the crystal's first, and under each every
 *	function of the pin, which is read back many times; the pair under
 *	which the pin is high about half the time and changes often is the
 *	PWM. As the chip comes up the clock is made from a source that does
 *	not run, and the PWM's function then holds the pin still. The same
 *	reading gives the PWM's frequency, from which the range is set for
 *	25kHz whatever the block's clock is, and a reading at three quarters
 *	duty says which way the output is inverted. When no pair gives a
 *	PWM, the clock is put back as it was found and the pin is driven as
 *	a plain output: the fan runs at full speed from the first step on
 *	and stops below it.
 *
 *	The temperature comes from the firmware once a second. When it
 *	cannot be read three times running, the fan runs at full speed.
 */

#include <sys/machine.h>

#ifdef RPI5

#include "kernel.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <sys/sysdef.h>
#include <ts/gpio.h>
#include <ts/board.h>
#include <ts/cpuclk.h>
#include "sysdepend.h"

#define FAN_PIN		45

/* The RP1's second PWM block, channel 3 */
#define PWM1_BASE	(RP1_WINDOW_BASE + 0x09c000)
#define PWM_CH		3
#define PWM_GLOBAL	(PWM1_BASE + 0x00)
#define PWM_CTRL	(PWM1_BASE + 0x14 + PWM_CH * 0x10)
#define PWM_RANGE	(PWM1_BASE + 0x18 + PWM_CH * 0x10)
#define PWM_PHASE	(PWM1_BASE + 0x1c + PWM_CH * 0x10)
#define PWM_DUTY	(PWM1_BASE + 0x20 + PWM_CH * 0x10)
#define PWM_RPM		(PWM1_BASE + 0x3c)	/* where the device tree reads the fan's speed */
#define GLOBAL_UPDATE	0x80000000U		/* the settings taken on one PWM clock */
#define CTRL_TRAILING	0x00000001U		/* mark first, then space */
#define CTRL_INVERT	0x00000008U

/* The block's clock (clk_pwm1), among the RP1's clock generators */
#define CLK_PWM1_CTRL	(RP1_WINDOW_BASE + 0x018084)
#define CLK_PWM1_DIV	(RP1_WINDOW_BASE + 0x018088)	/* the integer part of the divisor */
#define CLK_PWM1_FRAC	(RP1_WINDOW_BASE + 0x01808c)	/* and the fraction */
#define CLK_PWM1_SEL	(RP1_WINDOW_BASE + 0x018090)
#define CLK_ENABLE	0x00000800U
#define CLK_AUXSRC	0x000003e0U			/* which source the clock is made from */
#define CLK_AUX_SHIFT	5

#define PWM_HZ		25000
#define RANGE_FIRST	2078			/* 41566ns at the 50MHz the device tree asks for */
#define SAMPLES		4000

#define FAN_PRI		6			/* above the programs: it must run under load */
#define FAN_STKSZ	4096
#define FAN_TICK_MS	1000
#define FAILS_FULL	3

#ifndef TAG_GET_TEMPERATURE
#define TAG_GET_TEMPERATURE	0x00030006U
#endif

/*
 * What the firmware says of the supply and the clock: bit 0 the voltage
 * is low now, 1 the clock is capped, 2 it is slowed, 3 the soft limit of
 * the temperature is reached; bits 16 to 19 that each has happened since
 * the start. A supply too weak for the load shows here before the board
 * stops, so each change is told on the console (TAG_GET_THROTTLED of
 * rpi_fw.h, the bits CPU_THR_* of include/ts/cpuclk.h).
 */

LOCAL CONST INT	trip_mc[4] = { 40000, 50000, 57500, 65000 };
#define HYST_MC		3000
LOCAL CONST UB	level_of[5] = { 0, 120, 170, 220, 255 };

/* The processors slowed when the fan is not enough */
#define SLOW1_MC	78000
#define SLOW2_MC	82000
#define FAST_MC		70000
#define SLOW1_HZ	1800000000U
#define SLOW2_HZ	1500000000U

LOCAL struct {
	BOOL	on;			/* the task runs */
	BOOL	pwm;			/* the PWM drives the pin; else a plain output */
	UINT	func;			/* the pin's function that is the PWM */
	UW	range;
	UW	ctrl;
	INT	step;			/* 0..4 */
	INT	temp_mc;
	INT	fails;
	INT	slow;			/* 0 none, 1 SLOW1_HZ, 2 SLOW2_HZ */
	UW	thr;			/* what TAG_GET_THROTTLED said last */
} fan;

/* The supply and the clock as the firmware sees them, told when it changes */
LOCAL void throttled_look( void )
{
	UW	v = 0;

	if ( knl_fw_value(TAG_GET_THROTTLED, 0, &v) < E_OK || v == fan.thr ) {
		return;
	}
	fan.thr = v;
	tm_printf((UB *)"power: %s%s%s%s(since the start:%s%s%s%s) %08x, %d.%d C\n",
		  ( v & 0x1 ) ? "the voltage is low, " : "", ( v & 0x2 ) ? "the clock is capped, " : "",
		  ( v & 0x4 ) ? "the clock is slowed, " : "", ( v & 0x8 ) ? "at the soft limit of the temperature, " : "",
		  ( v & 0x10000 ) ? " low voltage" : "", ( v & 0x20000 ) ? " capped" : "",
		  ( v & 0x40000 ) ? " slowed" : "", ( v & 0x80000 ) ? " soft limit" : "",
		  v, fan.temp_mc / 1000, ( fan.temp_mc % 1000 ) / 100);
}

/* The processors' clock for the temperature t, under the limit of the settings */
LOCAL void arm_for( INT t )
{
	INT	want = fan.slow;
	UW	got = 0;

	if ( t >= SLOW2_MC ) {
		want = 2;
	} else if ( t >= SLOW1_MC && want < 1 ) {
		want = 1;
	} else if ( t < FAST_MC ) {
		want = 0;
	}
	if ( want == fan.slow ) {
		return;
	}
	if ( knl_cpu_heat(( want == 2 ) ? SLOW2_HZ : ( want == 1 ) ? SLOW1_HZ : 0, &got) >= E_OK ) {
		fan.slow = want;
		tm_printf((UB *)"fan: %d.%d C, processors at %d MHz\n", t / 1000, ( t % 1000 ) / 100,
			  (INT)( got / 1000000 ));
	}
}

/* The pin read SAMPLES times: how often it was high, how often it changed, how long it took */
LOCAL void sample( INT *p_high, INT *p_edges, UD *p_us )
{
	INT	i, v, high = 0, edges = 0, last;
	UD	t0, t1, hz;

	last = rp1_gpio_read(FAN_PIN);
	Asm("isb; mrs %0, cntvct_el0" : "=r"(t0));
	for ( i = 0; i < SAMPLES; i++ ) {
		v = rp1_gpio_read(FAN_PIN);
		high += v;
		if ( v != last ) {
			edges++;
		}
		last = v;
	}
	Asm("isb; mrs %0, cntvct_el0" : "=r"(t1));
	Asm("mrs %0, cntfrq_el0" : "=r"(hz));
	*p_high = high;
	*p_edges = edges;
	*p_us = ( hz != 0 ) ? ( t1 - t0 ) * 1000000ULL / hz : 0;
}

LOCAL BOOL toggling( INT high, INT edges )
{
	return ( edges >= 20 && high > SAMPLES * 3 / 10 && high < SAMPLES * 7 / 10 );
}

LOCAL void pwm_put( UW duty )
{
	out_w(PWM_DUTY, duty);
}

LOCAL void pwm_start( void )
{
	out_w(PWM_CTRL, fan.ctrl);
	out_w(PWM_RANGE, fan.range);
	out_w(PWM_PHASE, 0);
	out_w(PWM_DUTY, fan.range / 2);
	out_w(PWM_GLOBAL, in_w(PWM_GLOBAL) | ( 1U << PWM_CH ) | GLOBAL_UPDATE);
}

/*
 * The PWM put on the pin, and its frequency and polarity settled. FALSE
 * when no function of the pin shows the PWM.
 */
/* The block's clock made from one source, undivided; stopped while it is changed */
LOCAL void clk_use( UW ctrl, UINT aux )
{
	out_w(CLK_PWM1_CTRL, ctrl & ~( CLK_ENABLE | CLK_AUXSRC ));
	out_w(CLK_PWM1_DIV, 1);
	out_w(CLK_PWM1_FRAC, 0);
	out_w(CLK_PWM1_CTRL, ( ctrl & ~CLK_AUXSRC ) | ( (UW)aux << CLK_AUX_SHIFT ) | CLK_ENABLE);
}

LOCAL BOOL pwm_find( void )
{
	/* the function that held the pin high with the clock stopped first */
	LOCAL CONST UB	funcs[] = { 3, 0, 1, 2, 4, 8 };
	/* the crystal (50MHz, what the device tree asks for) first */
	LOCAL CONST UB	auxs[] = { 2, 1, 0, 3, 4, 5, 6, 7, 8 };
	INT		a, i = 0, high = 0, edges = 0, best, bh, be;
	UD		us = 0;
	UW		c = in_w(CLK_PWM1_CTRL), d = in_w(CLK_PWM1_DIV), fr = in_w(CLK_PWM1_FRAC);

	tm_printf((UB *)"fan: pwm1 clock ctrl %08x div %08x frac %08x sel %08x\n",
		  c, d, fr, in_w(CLK_PWM1_SEL));
	fan.range = RANGE_FIRST;
	fan.ctrl = CTRL_TRAILING | CTRL_INVERT;

	for ( a = 0; a < (INT)sizeof(auxs); a++ ) {
		clk_use(c, auxs[a]);
		pwm_start();
		tk_dly_tsk(2);
		best = -1;
		bh = be = 0;
		for ( i = 0; i < (INT)sizeof(funcs); i++ ) {
			(void)rp1_gpio_set_func(FAN_PIN, funcs[i]);
			tk_dly_tsk(2);
			sample(&high, &edges, &us);
			if ( toggling(high, edges) ) {
				break;
			}
			if ( edges > be || best < 0 ) {
				best = funcs[i];
				bh = high;
				be = edges;
			}
		}
		if ( i < (INT)sizeof(funcs) ) {
			tm_printf((UB *)"fan: clock source %d, function %d: high %d of %d, %d changes in %d us\n",
				  auxs[a], funcs[i], high, SAMPLES, edges, (INT)us);
			break;
		}
		tm_printf((UB *)"fan: clock source %d: no PWM (most changes under function %d: high %d, %d changes)\n",
			  auxs[a], best, bh, be);
	}
	if ( a == (INT)sizeof(auxs) ) {
		out_w(PWM_GLOBAL, ( in_w(PWM_GLOBAL) & ~( 1U << PWM_CH ) ) | GLOBAL_UPDATE);
		out_w(CLK_PWM1_CTRL, c & ~CLK_ENABLE);	/* the clock as it was found */
		out_w(CLK_PWM1_DIV, d);
		out_w(CLK_PWM1_FRAC, fr);
		out_w(CLK_PWM1_CTRL, c);
		return FALSE;
	}
	fan.func = funcs[i];

	/* the range for 25kHz from the frequency seen */
	if ( us > 0 ) {
		UD	f = (UD)edges * 1000000ULL / 2 / us;
		UD	clk = f * RANGE_FIRST;
		UD	r = ( f > 0 ) ? clk / PWM_HZ : RANGE_FIRST;

		if ( r < 100 ) r = 100;
		if ( r > 0xFFFFU ) r = 0xFFFFU;
		tm_printf((UB *)"fan: pwm at about %d Hz; range %d\n", (INT)f, (INT)r);
		fan.range = (UW)r;
		pwm_start();
	}

	/* at three quarters, the pin is to be high a quarter of the time */
	pwm_put(fan.range * 3 / 4);
	tk_dly_tsk(2);
	sample(&high, &edges, &us);
	if ( high > SAMPLES / 2 ) {
		fan.ctrl ^= CTRL_INVERT;
		pwm_start();
		pwm_put(fan.range * 3 / 4);
		tm_printf((UB *)"fan: output the other way round (high %d of %d)\n", high, SAMPLES);
	}
	return TRUE;
}

LOCAL void fan_set( INT step )
{
	UINT	lv = level_of[step];

	if ( fan.pwm ) {
		pwm_put((UW)( (UD)fan.range * lv / 255 ));
	} else {
		(void)rp1_gpio_write(FAN_PIN, ( lv > 0 ) ? 0 : 1);	/* low runs it */
	}
	fan.step = step;
}

LOCAL void fan_task( INT stacd, void *exinf )
{
	(void)stacd;
	(void)exinf;

	for ( ;; ) {
		UW	mc = 0;
		INT	step = fan.step;

		if ( knl_fw_value(TAG_GET_TEMPERATURE, 0, &mc) >= E_OK ) {
			INT	t = (INT)mc;

			fan.fails = 0;
			fan.temp_mc = t;
			while ( step < 4 && t >= trip_mc[step] ) {
				step++;
			}
			while ( step > 0 && t < trip_mc[step - 1] - HYST_MC ) {
				step--;
			}
			arm_for(t);
			throttled_look();
		} else if ( ++fan.fails >= FAILS_FULL ) {
			step = 4;
		}
		if ( step != fan.step ) {
			fan_set(step);
			tm_printf((UB *)"fan: %d.%d C, step %d (%d of 255), speed word %d\n",
				  fan.temp_mc / 1000, ( fan.temp_mc % 1000 ) / 100, step,
				  level_of[step], (INT)in_w(PWM_RPM));
		}
		tk_dly_tsk(FAN_TICK_MS);
	}
}

/* What the fan does now, for the system object; E_NOEXS when it is not run */
EXPORT ER knl_fan_state( INT *p_level, INT *p_speed, BOOL *p_pwm )
{
	if ( !fan.on ) {
		return E_NOEXS;
	}
	if ( p_level != NULL ) *p_level = level_of[fan.step];
	if ( p_speed != NULL ) *p_speed = fan.pwm ? (INT)in_w(PWM_RPM) : -1;
	if ( p_pwm != NULL )   *p_pwm = fan.pwm;
	return E_OK;
}

EXPORT ER knl_fan_init( void )
{
	T_CTSK	ctsk;
	ID	tskid;

	if ( knl_board.kind != BOARD_RPI5 ) {
		return E_NOEXS;		/* the Compute Module's carrier has a fan header of its own kind */
	}
	if ( rp1_gpio_set_dir(FAN_PIN, GPIO_DIR_OUT) < E_OK ) {
		tm_printf((UB *)"fan: the RP1's pins are not there; the fan is left at full speed\n");
		return E_NOEXS;
	}
	(void)rp1_gpio_set_pull(FAN_PIN, GPIO_PULL_DOWN);
	(void)rp1_gpio_write(FAN_PIN, 0);	/* full speed while the PWM is looked for */

	fan.pwm = pwm_find();
	if ( !fan.pwm ) {
		(void)rp1_gpio_set_dir(FAN_PIN, GPIO_DIR_OUT);
		(void)rp1_gpio_write(FAN_PIN, 0);
		tm_printf((UB *)"fan: no PWM on GPIO %d; run on and off\n", FAN_PIN);
	} else {
		tm_printf((UB *)"fan: PWM on GPIO %d (function %d, range %d)\n",
			  FAN_PIN, fan.func, fan.range);
	}
	fan.thr = 0xFFFFFFFFU;		/* the first reading is told whatever it says */
	fan.step = 4;			/* until the first reading */
	fan_set(4);

	ctsk.exinf = NULL;
	ctsk.tskatr = TA_HLNG | TA_RNG0;
	ctsk.task = (FP)fan_task;
	ctsk.itskpri = FAN_PRI;
	ctsk.stksz = FAN_STKSZ;
	tskid = tk_cre_tsk(&ctsk);
	if ( tskid < E_OK ) {
		return tskid;
	}
	fan.on = TRUE;
	return tk_sta_tsk(tskid, 0);
}

#endif /* RPI5 */
