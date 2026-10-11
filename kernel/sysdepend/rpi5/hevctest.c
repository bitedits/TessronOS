/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	hevctest.c (Raspberry Pi 5)
 *	The check on the board of the HEVC decoder (device/hevc/hevc.c,
 *	design 6.7), which QEMU does not have. make HEVCTEST=1 puts it in.
 *
 *	Some seconds after the system has come up a task of its own decodes
 *	the pictures of hevctest_data.S (tools/hevc/mkjobs.py made them from
 *	small streams libx265 encoded: one slice and CTBs of 32, two slices
 *	with wavefronts and CTBs of 64, CTBs of 16; an I picture then P
 *	pictures with weighted prediction and temporal motion vectors; two
 *	more of 10 bits, Main 10, one of them wider than a column of the
 *	decoder's) with the kernel's calls of the decoder, the way a program
 *	does with the object's records, and writes "hevctest:" lines to the
 *	console: the decoder's version and clock, then for each picture the
 *	hash of its NV12 (P010 for 10 bits) against the hash ffmpeg's
 *	decoding gave, the time it took, and
 *	at the end a summary. One that fails lets the next run; a stream
 *	whose picture fails goes on, its later pictures likely failing too.
 */

#include <sys/machine.h>

#ifdef RPI5

#include "kernel.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <ts/hevc.h>
#include "sysdepend.h"

#ifndef CNF_HEVCTEST
#define CNF_HEVCTEST	0
#endif

#if CNF_HEVCTEST

#define TEST_PRI	30
#define TEST_STKSZ	16384
#define TEST_DELAY_MS	20000

IMPORT CONST UB	kt_hevc_jobs[], kt_hevc_jobs_end[];

LOCAL UW rd32( CONST UB *p )
{
	return (UW)p[0] | ( (UW)p[1] << 8 ) | ( (UW)p[2] << 16 ) | ( (UW)p[3] << 24 );
}

LOCAL UW fnv1a( CONST UB *p, UW n, UW h )
{
	UW	i;

	for ( i = 0; i < n; i++ ) h = ( h ^ p[i] ) * 0x01000193U;
	return h;
}

LOCAL void test_task( INT stacd, void *exinf )
{
	CONST UB	*p = kt_hevc_jobs, *end = kt_hevc_jobs_end;
	T_HEVCINFO	info;
	UW		ns, s, npass = 0, nrun = 0;
	UB		*buf = NULL;
	UD		*job = NULL;
	ER		er;

	(void)stacd;
	(void)exinf;
	tk_dly_tsk(TEST_DELAY_MS);
	knl_hevc_info(&info);
	tm_printf((UB *)"hevctest: decoder %s, version %08x, clock %u Hz\n",
		  info.present ? "present" : "absent", info.version, info.clock_hz);
	if ( !info.present ) goto done;
	if ( end - p < 24 || rd32(p) != 0x45545648U || rd32(p + 4) != 0x32305453U	/* "HVTEST02" */
	     || rd32(p + 12) != sizeof(T_HEVCJOB) || rd32(p + 16) != sizeof(T_HEVCSLICE) ) {
		tm_printf((UB *)"hevctest: the data does not fit (T_HEVCJOB %d/%d, T_HEVCSLICE %d/%d)\n",
			  (INT)rd32(p + 12), (INT)sizeof(T_HEVCJOB), (INT)rd32(p + 16), (INT)sizeof(T_HEVCSLICE));
		goto done;
	}
	ns = rd32(p + 8);
	p += 24;
	job = (UD *)Kmalloc(64 * 1024);
	if ( job == NULL ) {
		tm_printf((UB *)"hevctest: no memory\n");
		goto done;
	}
	for ( s = 0; s < ns && p < end; s++ ) {
		char		name[17];
		T_HEVCCONF	c;
		UW		n, i, all;

		knl_memcpy(name, p, 16);
		name[16] = '\0';
		c.width = rd32(p + 16);
		c.height = rd32(p + 20);
		c.nslots = rd32(p + 24);
		n = rd32(p + 28);
		c.bitdepth = rd32(p + 32);
		p += 40;
		all = c.width * c.height * 3 / 2 * ( c.bitdepth == 10 ? 2 : 1 );
		er = knl_hevc_conf(&c);
		if ( buf != NULL ) Kfree(buf);
		buf = ( er >= E_OK ) ? (UB *)Kmalloc(all) : NULL;
		if ( er >= E_OK && buf == NULL ) er = E_NOMEM;
		tm_printf((UB *)"hevctest: %s %ux%u, %u bits, %u slots, %u pictures: conf %d\n",
			  name, c.width, c.height, c.bitdepth, c.nslots, n, er);
		for ( i = 0; i < n && p < end; i++ ) {
			UW	size = rd32(p), want = rd32(p + 4), slot = rd32(p + 8), got = 0;
			SZ	asz = 0;

			p += 16;
			nrun++;
			if ( size > 64 * 1024 ) {
				tm_printf((UB *)"hevctest: %s picture %u: FAIL, a job of %u bytes\n", name, i, size);
			} else if ( er >= E_OK ) {
				ER	e2;

				knl_memcpy(job, p, size);		/* aligned as the structures want */
				e2 = knl_hevc_decode(job, size);
				knl_hevc_info(&info);
				if ( e2 >= E_OK ) e2 = knl_hevc_picture(slot, 0, buf, all, &asz);
				if ( e2 >= E_OK ) got = fnv1a(buf, (UW)asz, 0x811C9DC5U);
				if ( e2 >= E_OK && got == want ) npass++;
				tm_printf((UB *)"hevctest: %s picture %u slot %u: %s, %d, hash %08x want %08x, %u us,"
					  " status %08x\n", name, i, slot, ( e2 >= E_OK && got == want ) ? "ok" : "FAIL",
					  e2, got, want, info.last_us, info.last_error);
				if ( e2 >= E_OK && got != want ) {
					/* where it differs first, for the one looking at it */
					UW	k;

					for ( k = 0; k < (UW)asz; k++ ) {
						if ( buf[k] != 0 ) break;
					}
					tm_printf((UB *)"hevctest:   first bytes %02x %02x %02x %02x, first not zero at %u,"
						  " middle %02x\n", buf[0], buf[1], buf[2], buf[3], k, buf[all / 3]);
				}
			} else {
				tm_printf((UB *)"hevctest: %s picture %u: FAIL, not configured\n", name, i);
			}
			p += ( size + 7 ) & ~7U;
		}
	}
	knl_hevc_info(&info);
	tm_printf((UB *)"hevctest: decoded %u, errors %u\n", info.decoded, info.errors);
done:
	if ( buf != NULL ) Kfree(buf);
	if ( job != NULL ) Kfree(job);
	tm_printf((UB *)"hevctest: summary %u/%u passed\n", npass, nrun);
	tk_ext_tsk();
}

#endif /* CNF_HEVCTEST */

EXPORT void knl_hevctest_start( void )
{
#if CNF_HEVCTEST
	T_CTSK	ctsk;
	ID	tid;

	knl_memset(&ctsk, 0, sizeof(ctsk));
	ctsk.tskatr = TA_HLNG | TA_RNG0;
	ctsk.task = (FP)test_task;
	ctsk.itskpri = TEST_PRI;
	ctsk.stksz = TEST_STKSZ;
	tid = tk_cre_tsk(&ctsk);
	if ( tid <= 0 || tk_sta_tsk(tid, 0) < E_OK ) {
		tm_printf((UB *)"hevctest: could not start (%d)\n", (INT)tid);
	}
#endif
}

#endif /* RPI5 */
