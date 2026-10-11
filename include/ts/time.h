/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	time.h
 *	TessronOS time services (design 7.3)
 */

#ifndef __TS_TIME_H__
#define __TS_TIME_H__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Monotonic clock: nanoseconds since the kernel started. Never set back,
 * unaffected by tk_set_tim.
 */
IMPORT ER ts_get_mono( UD *p_ns );

/*
 * The processors: how many are online, and the time each has spent
 * running tasks since the kernel started, in counts of the generic
 * timer (freq a second). A processor's load over a while is what its
 * busy count grew by against what now grew by.
 */
#define TS_CPU_MAX	8

typedef struct {
	INT	ncpu;			/* processors online */
	UW	freq;			/* the counts a second */
	UD	now;			/* the timer's count now */
	UD	busy[TS_CPU_MAX];	/* each processor's counts spent running tasks */
} T_TSCPU;

IMPORT ER ts_ref_cpu( T_TSCPU *pk );

#ifdef __cplusplus
}
#endif

#endif /* __TS_TIME_H__ */
