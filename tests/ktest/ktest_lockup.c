/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ktest_lockup.c
 *	Finding the machine stopped (lockup.c, design 14.10): stops made on
 *	purpose -- a processor with its interrupts masked, one whose timer
 *	is made to look stopped, a spin lock and the kernel lock held too
 *	long, two tasks waiting for each other's mutexes, the watchdog's
 *	kicks and the watched loop stopping -- each found and told. The
 *	times are shortened to a second for the run and put back after.
 *	What is told goes to the console as !LOCKUP! lines.
 */

#include "ktest.h"
#include "kernel.h"
#include <ts/lockup.h>

#define STOP_MS		1000		/* what counts as stopped, for the run */

LOCAL volatile BOOL	lk_done;
LOCAL T_SPLOCK		lk_test_lock;
LOCAL ID		lk_m1, lk_m2;

LOCAL UD now_ms( void )
{
	UD	c, f;

	Asm("isb; mrs %0, cntpct_el0" : "=r"(c) :: "memory");
	Asm("mrs %0, cntfrq_el0" : "=r"(f));
	return c * 1000 / f;
}

LOCAL void spin_ms( UD ms )
{
	UD	t0 = now_ms();

	while ( now_ms() - t0 < ms ) {
		;
	}
}

/* A task on one processor (PRC ID prc) */
LOCAL ID cre_on( FP task, ID prc, PRI pri )
{
	T_CTSK	ctsk;

	ctsk.exinf   = NULL;
	ctsk.tskatr  = TA_HLNG | TA_RNG0 | TA_ASSPRC;
	ctsk.task    = task;
	ctsk.itskpri = pri;
	ctsk.stksz   = 16 * 1024;
	ctsk.assprc  = 1U << ( prc - 1 );
	return tk_cre_tsk(&ctsk);
}

LOCAL void wait_done( INT ms )
{
	INT	i;

	for ( i = 0; i < ms / 10 && !lk_done; i++ ) {
		tk_dly_tsk(10);
	}
}

/* every processor's timer comes */
LOCAL void test_beats( void )
{
	T_LKSTAT	a, b;
	INT		i;

	knl_lockup_stat(&a);
	tk_dly_tsk(1300);
	knl_lockup_stat(&b);
	for ( i = 0; i < knl_num_prc && i < 8; i++ ) {
		tm_printf((UB *)"  CPU%d: %d interrupts of its timer in 1.3 s\n", i, (INT)( b.beats[i] - a.beats[i] ));
		KT_ASSERT(b.beats[i] - a.beats[i] >= 2);
	}
	tm_printf((UB *)"  hardware watchdog: %s\n", b.hw ? "running" : "none");
}

/* a processor with its interrupts masked: found, and said not to take the interrupt sent */
LOCAL void masker( INT stacd, void *exinf )
{
	UINT	s = disint();

	spin_ms(STOP_MS * 2 + 500);
	enaint(s);
	lk_done = TRUE;
	tk_exd_tsk();
}

LOCAL void test_masked( void )
{
	T_LKSTAT	a, b;
	ID		prc = knl_num_prc, id;

	if ( knl_num_prc < 2 ) KT_SKIP("single processor");
	knl_lockup_stat(&a);
	lk_done = FALSE;
	id = cre_on((FP)masker, prc, KT_PRI_HIGH);
	KT_ASSERT(id > 0);
	KT_ASSERT_ER(tk_sta_tsk(id, 0), E_OK);
	wait_done(10000);
	KT_ASSERT(lk_done);
	tk_dly_tsk(STOP_MS);			/* its next interrupt, and a look by another */
	knl_lockup_stat(&b);
	KT_ASSERT(b.hard > a.hard);
	KT_ASSERT(b.sgi_lost > a.sgi_lost);
	KT_ASSERT_EQ(b.last_prc, prc);
	KT_ASSERT(b.back > a.back);
}

/* a processor whose interrupts come, but whose timer looks stopped: it answers the interrupt sent */
LOCAL void test_answers( void )
{
	T_LKSTAT	a, b;
	ID		prc = knl_num_prc;

	if ( knl_num_prc < 2 ) KT_SKIP("single processor");
	knl_lockup_stat(&a);
	knl_lockup_mute(prc, TRUE);
	tk_dly_tsk(STOP_MS * 2 + 500);
	knl_lockup_mute(prc, FALSE);
	tk_dly_tsk(STOP_MS);
	knl_lockup_stat(&b);
	KT_ASSERT(b.hard > a.hard);
	KT_ASSERT(b.sgi_taken > a.sgi_taken);
	KT_ASSERT(b.back > a.back);
}

/* a spin lock held too long: its waiter says so, with the holder */
LOCAL void holder( INT stacd, void *exinf )
{
	UINT	s;

	ISpinLock(&lk_test_lock, &s);
	spin_ms(STOP_MS * 2);
	ISpinUnlock(&lk_test_lock, &s);
	tk_exd_tsk();
}

LOCAL void waiter( INT stacd, void *exinf )
{
	UINT	s;

	tk_dly_tsk(100);
	ISpinLock(&lk_test_lock, &s);
	ISpinUnlock(&lk_test_lock, &s);
	lk_done = TRUE;
	tk_exd_tsk();
}

LOCAL void test_spin( void )
{
	T_LKSTAT	a, b;
	ID		h, w;

	if ( knl_num_prc < 3 ) KT_SKIP("fewer than three processors");
	InitSpinLock(&lk_test_lock);
	knl_lockup_stat(&a);
	lk_done = FALSE;
	h = cre_on((FP)holder, knl_num_prc, KT_PRI_HIGH);
	w = cre_on((FP)waiter, knl_num_prc - 1, KT_PRI_HIGH);
	KT_ASSERT(h > 0 && w > 0);
	KT_ASSERT_ER(tk_sta_tsk(h, 0), E_OK);
	KT_ASSERT_ER(tk_sta_tsk(w, 0), E_OK);
	wait_done(10000);
	KT_ASSERT(lk_done);
	tk_dly_tsk(STOP_MS);
	knl_lockup_stat(&b);
	KT_ASSERT(b.spin > a.spin);
}

/* the kernel lock held too long by one taking */
LOCAL void klocker( INT stacd, void *exinf )
{
	UINT	s = knl_lock_kernel();

	spin_ms(STOP_MS * 2);
	knl_unlock_kernel(s);
	lk_done = TRUE;
	tk_exd_tsk();
}

LOCAL void test_klock( void )
{
	T_LKSTAT	a, b;
	ID		id;

	if ( knl_num_prc < 2 ) KT_SKIP("single processor");
	knl_lockup_stat(&a);
	lk_done = FALSE;
	id = cre_on((FP)klocker, knl_num_prc, KT_PRI_HIGH);
	KT_ASSERT(id > 0);
	KT_ASSERT_ER(tk_sta_tsk(id, 0), E_OK);
	wait_done(10000);
	KT_ASSERT(lk_done);
	tk_dly_tsk(STOP_MS);
	knl_lockup_stat(&b);
	KT_ASSERT(b.klock > a.klock);
}

/* two tasks, each holding the mutex the other waits for */
LOCAL void taker( INT stacd, void *exinf )
{
	ID	mine = ( stacd == 0 ) ? lk_m1 : lk_m2;
	ID	other = ( stacd == 0 ) ? lk_m2 : lk_m1;

	tk_loc_mtx(mine, TMO_FEVR);
	tk_dly_tsk(200);
	(void)tk_loc_mtx(other, 3000);		/* never had: the other waits for this one's */
	tk_unl_mtx(mine);
	tk_ext_tsk();
}

LOCAL void test_circle( void )
{
	T_LKSTAT	a, b;
	T_CMTX		cm;
	ID		t1, t2;

	knl_memset(&cm, 0, sizeof(cm));
	cm.mtxatr = TA_TFIFO;
	lk_m1 = tk_cre_mtx(&cm);
	lk_m2 = tk_cre_mtx(&cm);
	KT_ASSERT(lk_m1 > 0 && lk_m2 > 0);
	knl_lockup_stat(&a);
	t1 = kt_cre_tsk((FP)taker, KT_PRI_HIGH, NULL);
	t2 = kt_cre_tsk((FP)taker, KT_PRI_HIGH, NULL);
	KT_ASSERT(t1 > 0 && t2 > 0);
	KT_ASSERT_ER(tk_sta_tsk(t1, 0), E_OK);
	KT_ASSERT_ER(tk_sta_tsk(t2, 1), E_OK);
	tk_dly_tsk(600);
	knl_lockup_scan();
	knl_lockup_scan();			/* told once only */
	knl_lockup_stat(&b);
	KT_ASSERT_EQ(b.deadlock, a.deadlock + 1);
	tk_dly_tsk(3500);			/* the waits time out, and the tasks end */
	KT_ASSERT_ER(tk_del_tsk(t1), E_OK);
	KT_ASSERT_ER(tk_del_tsk(t2), E_OK);
	KT_ASSERT_ER(tk_del_mtx(lk_m1), E_OK);
	KT_ASSERT_ER(tk_del_mtx(lk_m2), E_OK);
}

/* the watchdog's kicks stop: told, and the machine counted as stopped until they come again */
LOCAL void test_kicks( void )
{
	T_LKSTAT	a, b, c;

	knl_lockup_stat(&a);
	knl_wdog_kick();
	tk_dly_tsk(STOP_MS * 2);
	knl_lockup_stat(&b);
	KT_ASSERT(b.starved > a.starved);
	KT_ASSERT(b.failing);
	knl_wdog_kick();
	tk_dly_tsk(700);
	knl_lockup_stat(&c);
	KT_ASSERT(!c.failing);
	knl_wdog_kick_stop();
}

/* the watched loop does not come round: told, with where its task is */
LOCAL void test_silent( void )
{
	T_LKSTAT	a, b;

	knl_lockup_stat(&a);
	knl_wdog_watch(tk_get_tid());
	tk_dly_tsk(STOP_MS * 2);
	knl_wdog_watch(0);
	knl_lockup_stat(&b);
	KT_ASSERT(b.silent > a.silent);
	KT_ASSERT(!b.failing);			/* the machine is not reset for it */
}

EXPORT void ktest_lockup( void )
{
	knl_lockup_tune(STOP_MS, STOP_MS, STOP_MS);
	KT_RUN(test_beats);
	KT_RUN(test_masked);
	KT_RUN(test_answers);
	KT_RUN(test_spin);
	KT_RUN(test_klock);
	KT_RUN(test_circle);
	KT_RUN(test_kicks);
	KT_RUN(test_silent);
	knl_lockup_tune(0, 0, 0);
}
