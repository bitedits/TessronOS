/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cpuclk.h
 *	The processors' clock and the limit the machine's settings put on
 *	it (kernel/sysdepend/cpu/core/armv8a/cpuclk.c, design 3.7)
 *
 *	The clock asked of the firmware is the least of three: the most the
 *	firmware allows, the machine's setting CPU_MHZ (0 the most; with no
 *	line CPU_MHZ_DEFAULT), and what the fan asks when the processors run
 *	hot (Raspberry Pi 5). It is never below the least the firmware
 *	allows. Where there is no firmware to ask (QEMU) the limit is kept
 *	and nothing is set.
 */

#ifndef __TS_CPUCLK_H__
#define __TS_CPUCLK_H__

#include <tk/typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The limit with no line CPU_MHZ, and up to the moment the setting is
 * read at the start: the four processors at 2.4GHz draw more than a
 * supply that is not quite enough gives, and the board then stops.
 */
#define CPU_MHZ_DEFAULT		1800
#define CPU_MHZ_MOST		0		/* the setting for the most the firmware allows */

typedef struct {
	UW	limit_mhz;	/* the limit as taken: 0 the most */
	UW	want_hz;	/* what was last asked of the firmware (0: nothing asked) */
	UW	hz;		/* the clock now, as the firmware says (0: unknown) */
	UW	min_hz;		/* the least and the most the firmware allows (0: unknown) */
	UW	max_hz;
	UW	heat_hz;	/* the fan's slowing (0: none) */
	UW	throttled;	/* what the firmware says of the supply (0xFFFFFFFF: unknown) */
	UW	taken;		/* times a limit was taken (the start, the settings) */
	UW	applied;	/* times the firmware was asked */
} T_CPUCLK;

/* The bits of 'throttled', as the firmware gives them */
#define CPU_THR_UNDERVOLT	0x00000001U	/* the supply's voltage is low now */
#define CPU_THR_CAPPED		0x00000002U	/* the most the clock may be is lowered now */
#define CPU_THR_SLOWED		0x00000004U	/* the clock is slowed now */
#define CPU_THR_SOFTTEMP	0x00000008U	/* the soft limit of the temperature is reached */
#define CPU_THR_UNDERVOLT_SEEN	0x00010000U	/* each of them since the start */
#define CPU_THR_CAPPED_SEEN	0x00020000U
#define CPU_THR_SLOWED_SEEN	0x00040000U
#define CPU_THR_SOFTTEMP_SEEN	0x00080000U

/*
 * The clock at the start, as high as the default limit lets it go, until
 * the machine's settings are read.
 */
IMPORT void knl_cpu_start( void );

/*
 * The machine's setting CPU_MHZ: the limit in MHz, 0 the most the
 * firmware allows. The clock is set at once; what the firmware took is
 * told on the console when the limit changes.
 */
IMPORT ER   knl_cpu_limit( UW mhz );

/* The fan's slowing: hz, 0 none; the rate the firmware took */
IMPORT ER   knl_cpu_heat( UW hz, UW *p_got );

/* The state of the clock, the firmware asked again */
IMPORT void knl_cpu_clock( T_CPUCLK *st );

/* The clock the three ask for, within what the firmware allows (0 for min or max: unknown) */
IMPORT UW   knl_cpu_want( UW limit_mhz, UW heat_hz, UW min_hz, UW max_hz );

/*
 * The firmware's part (kernel/sysdepend/rpi/mbox.c): none on a machine
 * without it, where each answers FALSE or E_NOSPT.
 */
IMPORT BOOL knl_cpu_hw_range( UW *p_min, UW *p_max );
IMPORT ER   knl_cpu_hw_set( UW hz, UW *p_got );
IMPORT ER   knl_cpu_hw_now( UW *p_hz );
IMPORT ER   knl_cpu_hw_throttled( UW *p_val );

#ifdef __cplusplus
}
#endif

#endif /* __TS_CPUCLK_H__ */
