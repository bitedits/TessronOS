/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	mbox.c (Raspberry Pi 5, Raspberry Pi 4)
 *	The VideoCore firmware's property interface (design 4.10.2, 16.5.1).
 *
 *	One call hands the firmware a buffer of tags and waits until it has
 *	written its answers into the same buffer. The buffer is given by its
 *	address on the VideoCore's bus, which sees ARM physical 0..1GB at
 *	0xC000_0000, so it must lie below 1GB; it is 16 byte aligned because
 *	the low four bits of the message carry the channel. The firmware
 *	reads and writes memory without looking at this processor's caches:
 *	the buffer is cleaned before the call and invalidated after it.
 *
 *	The real time clock and the screen both use it. Calls are one at a
 *	time: the mailbox has one reply register, and an answer taken by the
 *	wrong caller would be lost to the right one.
 */

#include <sys/machine.h>

#ifdef RPI_VCFW

#include "kernel.h"
#include <tm/tmonitor.h>
#include <ts/time.h>
#include <ts/cpuclk.h>
#include "../sysdepend.h"

#define MBOX_READ	(VC_MAILBOX_BASE + 0x00)
#define MBOX_STATUS	(VC_MAILBOX_BASE + 0x18)
#define MBOX_WRITE	(VC_MAILBOX_BASE + 0x20)
#define MBOX_FULL	0x80000000U
#define MBOX_EMPTY	0x40000000U
#define MBOX_CH_PROP	8

#define VC_BUS_BASE	0xC0000000U		/* VC view of ARM physical 0..1GB */
#define VC_BUS_LIMIT	0x40000000ULL

/*
 * Polling iterations. Setting a display mode keeps the firmware busy
 * for far longer than reading the clock does, so the wait for the
 * answer is given more room than the wait to send.
 */
#define MBOX_SEND_TMO	1000000
#define MBOX_REPLY_TMO	100000000

LOCAL T_SPLOCK	mbox_lock;

LOCAL void dcache_clean_inval( void *p, UD len )
{
	UBINT	a = (UBINT)p & ~63ULL, end = (UBINT)p + len;

	for ( ; a < end; a += 64 ) {
		Asm("dc civac, %0" :: "r"(a) : "memory");
	}
	Asm("dsb sy");
}

/*
 * Take the mailbox. A task that finds it in use gives up the processor
 * rather than spinning against a caller that may be waiting for the
 * firmware for some milliseconds; where it cannot wait (before the
 * kernel runs tasks), no one else can hold it.
 */
LOCAL void mbox_take( void )
{
	while ( !SpinTryLock(&mbox_lock) ) {
		if ( tk_dly_tsk(1) != E_OK ) {
			Asm("yield");
		}
	}
}

/*
 * One property call on 'buf', whose first word is its size in bytes.
 * Answers E_OK when the firmware answered with the success code
 * (0x80000000) in word 1; whether each tag was answered is in its own
 * response word, which the caller looks at.
 */
EXPORT ER knl_mbox_property( UW *buf )
{
	UD	pa = VA2PA((UBINT)buf);
	UW	msg;
	INT	i;
	ER	er = E_OK;

	if ( pa >= VC_BUS_LIMIT || ( pa & 15 ) != 0 ) {
		return E_NOSPT;
	}
	msg = (UW)pa | VC_BUS_BASE | MBOX_CH_PROP;

	mbox_take();
	dcache_clean_inval(buf, buf[0]);

	for ( i = 0; in_w(MBOX_STATUS) & MBOX_FULL; i++ ) {
		if ( i >= MBOX_SEND_TMO ) {
			er = E_TMOUT;
			goto out;
		}
	}
	out_w(MBOX_WRITE, msg);

	for (;;) {
		for ( i = 0; in_w(MBOX_STATUS) & MBOX_EMPTY; i++ ) {
			if ( i >= MBOX_REPLY_TMO ) {
				er = E_TMOUT;
				goto out;
			}
		}
		if ( in_w(MBOX_READ) == msg ) break;
	}

	dcache_clean_inval(buf, buf[0]);
	er = ( buf[1] == 0x80000000U ) ? E_OK : E_IO;
out:
	SpinUnlock(&mbox_lock);
	return er;
}

/*
 * The same call, for one the firmware may take a while to answer: the
 * screen's next vertical blank, or a change of the page it shows, which
 * a task makes for every frame. The answer is looked for without a
 * pause for 'spin_us', which is as long as most answers take, and then
 * once a millisecond with the task asleep in between, so that a wait
 * of a whole refresh does not hold a processor. E_TMOUT when there is
 * no answer within 'tmo_ms' milliseconds; an answer that comes later
 * is passed over by the next call, which looks only for its own.
 */
EXPORT ER knl_mbox_property_wait( UW *buf, UINT spin_us, UINT tmo_ms )
{
	UD	pa = VA2PA((UBINT)buf);
	UD	t0 = 0, now = 0;
	UW	msg;
	INT	i;
	ER	er = E_OK;

	if ( pa >= VC_BUS_LIMIT || ( pa & 15 ) != 0 ) {
		return E_NOSPT;
	}
	msg = (UW)pa | VC_BUS_BASE | MBOX_CH_PROP;

	mbox_take();
	dcache_clean_inval(buf, buf[0]);

	for ( i = 0; in_w(MBOX_STATUS) & MBOX_FULL; i++ ) {
		if ( i >= MBOX_SEND_TMO ) {
			er = E_TMOUT;
			goto out;
		}
	}
	out_w(MBOX_WRITE, msg);
	(void)ts_get_mono(&t0);

	for (;;) {
		if ( ( in_w(MBOX_STATUS) & MBOX_EMPTY ) == 0 ) {
			if ( in_w(MBOX_READ) == msg ) {
				break;
			}
			continue;
		}
		(void)ts_get_mono(&now);
		if ( now - t0 >= (UD)tmo_ms * 1000000 ) {
			er = E_TMOUT;
			goto out;
		}
		if ( now - t0 < (UD)spin_us * 1000 ) {
			continue;
		}
		if ( tk_dly_tsk(1) != E_OK ) {
			Asm("yield");		/* not in a task: no one else to run */
		}
	}

	dcache_clean_inval(buf, buf[0]);
	er = ( buf[1] == 0x80000000U ) ? E_OK : E_IO;
out:
	SpinUnlock(&mbox_lock);
	return er;
}

/*
 * One value the firmware tells: a tag whose request is one word (which
 * sensor, which clock) and whose answer is that word and the value --
 * the temperature (0x00030006, thousandths of a degree Celsius), a
 * clock's rate (0x00030002, Hz). E_IO when the tag was not answered.
 * The message is one of this file's, taken one caller at a time.
 */
LOCAL UW	fw_buf[16] __attribute__((aligned(64)));
LOCAL T_SPLOCK	fw_lock;

EXPORT ER knl_fw_value( UW tag, UW id, UW *p_val )
{
	ER	er;

	while ( !SpinTryLock(&fw_lock) ) {
		if ( tk_dly_tsk(1) != E_OK ) {
			Asm("yield");
		}
	}
	knl_memset(fw_buf, 0, sizeof(fw_buf));
	fw_buf[0] = 8 * 4;
	fw_buf[1] = 0;			/* a request */
	fw_buf[2] = tag;
	fw_buf[3] = 8;			/* room for two words */
	fw_buf[4] = 4;			/* one word asked with */
	fw_buf[5] = id;
	fw_buf[6] = 0;
	fw_buf[7] = 0;			/* the end tag */
	er = knl_mbox_property(fw_buf);
	if ( er >= E_OK ) {
		if ( ( fw_buf[4] & 0x80000000U ) == 0 ) {
			er = E_IO;
		} else if ( p_val != NULL ) {
			*p_val = fw_buf[6];
		}
	}
	SpinUnlock(&fw_lock);
	return er;
}

/*
 * One value the firmware tells for a tag asked with nothing: the board
 * revision (0x00010002) and the like, whose answer is a single word.
 */
EXPORT ER knl_fw_get( UW tag, UW *p_val )
{
	ER	er;

	while ( !SpinTryLock(&fw_lock) ) {
		if ( tk_dly_tsk(1) != E_OK ) {
			Asm("yield");
		}
	}
	knl_memset(fw_buf, 0, sizeof(fw_buf));
	fw_buf[0] = 7 * 4;
	fw_buf[1] = 0;			/* a request */
	fw_buf[2] = tag;
	fw_buf[3] = 4;			/* room for the word */
	fw_buf[4] = 0;			/* asked with nothing */
	fw_buf[5] = 0;
	fw_buf[6] = 0;			/* the end tag */
	er = knl_mbox_property(fw_buf);
	if ( er >= E_OK ) {
		if ( ( fw_buf[4] & 0x80000000U ) == 0 ) {
			er = E_IO;
		} else if ( p_val != NULL ) {
			*p_val = fw_buf[5];
		}
	}
	SpinUnlock(&fw_lock);
	return er;
}

/*
 * A clock set to a rate: the clock's number, the rate in Hz and a 0
 * (the firmware's own boost of the voltage left to it). The firmware
 * answers with the rate it took, which may be less than asked.
 */
LOCAL ER fw_set_clock( UW id, UW hz, UW *p_got )
{
	ER	er;

	while ( !SpinTryLock(&fw_lock) ) {
		if ( tk_dly_tsk(1) != E_OK ) {
			Asm("yield");
		}
	}
	knl_memset(fw_buf, 0, sizeof(fw_buf));
	fw_buf[0] = 9 * 4;
	fw_buf[1] = 0;			/* a request */
	fw_buf[2] = TAG_SET_CLOCK_RATE;
	fw_buf[3] = 12;			/* room for three words */
	fw_buf[4] = 12;			/* three words asked with */
	fw_buf[5] = id;
	fw_buf[6] = hz;
	fw_buf[7] = 0;
	fw_buf[8] = 0;			/* the end tag */
	er = knl_mbox_property(fw_buf);
	if ( er >= E_OK ) {
		if ( ( fw_buf[4] & 0x80000000U ) == 0 ) {
			er = E_IO;
		} else if ( p_got != NULL ) {
			*p_got = fw_buf[6];
		}
	}
	SpinUnlock(&fw_lock);
	return er;
}

/*
 * The processors' clock, for cpuclk.c (include/ts/cpuclk.h): the range
 * the firmware allows, a rate set, the rate now, and what the firmware
 * says of the supply and the clock.
 */
EXPORT BOOL knl_cpu_hw_range( UW *p_min, UW *p_max )
{
	UW	lo = 0, hi = 0;

	if ( knl_fw_value(TAG_GET_MAX_CLOCK_RATE, FW_CLOCK_ARM, &hi) < E_OK || hi == 0 ) {
		return FALSE;
	}
	if ( knl_fw_value(TAG_GET_MIN_CLOCK_RATE, FW_CLOCK_ARM, &lo) < E_OK || lo > hi ) {
		lo = 0;			/* not told: the firmware holds the clock to it by itself */
	}
	*p_min = lo;
	*p_max = hi;
	return TRUE;
}

EXPORT ER knl_cpu_hw_set( UW hz, UW *p_got )
{
	return fw_set_clock(FW_CLOCK_ARM, hz, p_got);
}

EXPORT ER knl_cpu_hw_now( UW *p_hz )
{
	return knl_fw_value(TAG_GET_CLOCK_RATE, FW_CLOCK_ARM, p_hz);
}

EXPORT ER knl_cpu_hw_throttled( UW *p_val )
{
	return knl_fw_value(TAG_GET_THROTTLED, 0, p_val);
}

#endif /* RPI_VCFW */
