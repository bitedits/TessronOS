/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ktest_rump.c
 *	The NetBSD stack under load from every processor at once (design
 *	9.1.2, 12.6).
 *
 *	Many tasks of different priorities call into the rump kernel at the
 *	same time over the loopback interface: connections made, used and
 *	closed without end, echoed by several servers on one listening
 *	socket; datagrams back and forth with short waits for them; waits
 *	on sockets that nothing comes to, which run out every millisecond;
 *	sockets made and closed at once; and processes that talk to the
 *	servers and end by themselves, or are ended while they wait inside
 *	the stack. Waits that end by a signal and waits that run out cross
 *	each other in the rump kernel's locks and condition variables, on
 *	four processors.
 *
 *	What is checked: that it all keeps moving, that everything ends
 *	when told to, and that the stack still carries a connection after.
 *	A fault the load brings out stops the machine by itself; with make
 *	STKWATCH=1 a stack written over while its task was not running is
 *	caught as the task is taken to run again (design 4.7.6).
 *
 *	It runs KT_RUMP_S seconds (make KT_RUMP_S=<seconds>; 20 if not given).
 */

#include "ktest.h"
#include "tstdlib.h"
#include <ts/so.h>
#include <ts/fs.h>
#include <ts/proc.h>

#ifndef KT_RUMP_S
#define KT_RUMP_S	20
#endif

#define LOOPBACK	0x7f000001UL		/* 127.0.0.1 */
#define ECHO_PORT	17300			/* echoed back */
#define HOLD_PORT	17301			/* taken in and never answered */
#define UDP_PORT	17310			/* .. + 2 * UDP_PAIRS */
#define N_ECHO		3			/* servers on the echoing socket */
#define N_CLIENT	8
#define UDP_PAIRS	2
#define N_IDLE		2			/* waits that always run out */
#define N_PROC		2			/* tasks starting processes */
#define N_TASKS		( N_ECHO + 1 + N_CLIENT + 2 * UDP_PAIRS + N_IDLE + 1 + N_PROC )
#define BUF_MAX		2048

#define SVCPROG		"/boot/SVCPROG.ELF"
#define SP_BLOCK	4			/* as tests/uprog/svcprog.c */

LOCAL volatile BOOL	st_stop;
LOCAL INT		st_echo = -1, st_hold = -1;	/* the listening sockets */
LOCAL ID		st_done;		/* a count for each task that has ended */
LOCAL BOOL		st_prog;

/* What went on, counted by kind */
LOCAL volatile INT	n_conn, n_conn_err, n_bytes, n_udp, n_udp_lost, n_idle, n_churn, n_proc, n_kill;

LOCAL void addr_set( struct sockaddr_in *sa, UW addr, UH port )
{
	knl_memset(sa, 0, sizeof(*sa));
	sa->sin_family = AF_INET;
	sa->sin_port = lwip_htons(port);
	sa->sin_addr.s_addr = lwip_htonl(addr);
}

LOCAL void timeout_ms( INT s, INT opt, INT ms )
{
	struct timeval	tv;

	tv.tv_sec = ms / 1000;
	tv.tv_usec = ( ms % 1000 ) * 1000;
	(void)so_setsockopt(s, SOL_SOCKET, opt, &tv, sizeof(tv));
}

LOCAL void st_end( void )
{
	(void)tk_sig_sem(st_done, 1);
	tk_exd_tsk();
}

/* A value that moves along with each call, different for each task */
LOCAL UW st_rand( UW *seed )
{
	*seed = *seed * 1103515245U + 12345U;
	return *seed >> 8;
}

/* ---------------------------------------------------------------- servers */

/* Takes connections from the echoing socket and sends back what comes */
LOCAL void echo_task( INT k, void *exinf )
{
	UB	*buf = (UB *)Kmalloc(BUF_MAX);
	INT	c, n, idle;

	while ( buf != NULL && !st_stop ) {
		c = so_accept(st_echo, NULL, NULL);
		if ( c < 0 ) {
			if ( st_stop ) break;
			tk_dly_tsk(1);
			continue;
		}
		timeout_ms(c, SO_RCVTIMEO, 10 + k * 7);
		for ( idle = 0; idle < 50 && !st_stop; ) {
			n = so_recv(c, buf, BUF_MAX, 0);
			if ( n == E_TMOUT ) {
				idle++;
				continue;
			}
			if ( n <= 0 ) break;
			idle = 0;
			if ( so_send(c, buf, n, 0) != n ) break;
		}
		(void)so_close(c);
	}
	if ( buf != NULL ) Kfree(buf);
	st_end();
}

/* Takes connections in and keeps them, unanswered, until the other end goes */
LOCAL void hold_task( INT k, void *exinf )
{
	UB	b[16];
	INT	c, n, i;

	while ( !st_stop ) {
		c = so_accept(st_hold, NULL, NULL);
		if ( c < 0 ) {
			if ( st_stop ) break;
			tk_dly_tsk(1);
			continue;
		}
		timeout_ms(c, SO_RCVTIMEO, 5);
		for ( i = 0; i < 2000 && !st_stop; i++ ) {
			n = so_recv(c, b, sizeof(b), 0);
			if ( n != E_TMOUT && n <= 0 ) break;
		}
		(void)so_close(c);
	}
	st_end();
}

/* ---------------------------------------------------------------- the load */

/*
 * Connections without end: a few rounds of bytes each way. Odd tasks
 * read with a limit of a few milliseconds, so that their waits often
 * run out just as the answer comes; every third leaves the last answer
 * unread, so that its close resets the connection.
 */
LOCAL void client_task( INT k, void *exinf )
{
	struct sockaddr_in	me;
	UB			*out = (UB *)Kmalloc(BUF_MAX), *in = (UB *)Kmalloc(BUF_MAX);
	UW			seed = 0x9e3779b9U * (UW)( k + 1 );
	INT			s, r, rounds, len, got, n, tries, i;

	addr_set(&me, LOOPBACK, ECHO_PORT);
	for ( i = 0; out != NULL && i < BUF_MAX; i++ ) out[i] = (UB)( i * 7 + k );
	while ( out != NULL && in != NULL && !st_stop ) {
		s = so_socket(AF_INET, SOCK_STREAM, 0);
		if ( s < 0 ) {
			n_conn_err++;
			tk_dly_tsk(5);
			continue;
		}
		if ( ( k & 1 ) != 0 ) timeout_ms(s, SO_RCVTIMEO, 1 + (INT)( st_rand(&seed) % 4 ));
		if ( so_connect(s, (struct sockaddr *)&me, sizeof(me)) != E_OK ) {
			n_conn_err++;
			(void)so_close(s);
			tk_dly_tsk(5);
			continue;
		}
		rounds = 1 + (INT)( st_rand(&seed) % 4 );
		for ( r = 0; r < rounds && !st_stop; r++ ) {
			len = 16 + (INT)( st_rand(&seed) % ( BUF_MAX - 16 ) );
			if ( so_send(s, out, len, 0) != len ) break;
			if ( k % 3 == 0 && r == rounds - 1 ) break;
			for ( got = 0, tries = 0; got < len && tries < 200; ) {
				n = so_recv(s, in + got, len - got, 0);
				if ( n == E_TMOUT ) {
					tries++;
					continue;
				}
				if ( n <= 0 ) break;
				got += n;
			}
			n_bytes += got;
			if ( got < len ) break;
		}
		(void)so_close(s);
		n_conn++;
	}
	if ( out != NULL ) Kfree(out);
	if ( in != NULL ) Kfree(in);
	st_end();
}

/* One of a pair: sends to the other and waits a millisecond or two for what it sends back */
LOCAL void udp_task( INT k, void *exinf )
{
	struct sockaddr_in	me, peer;
	fd_set			rd;
	UB			b[256];
	UW			seed = 0x85ebca6bU * (UW)( k + 1 );
	INT			s, n, len;

	s = so_socket(AF_INET, SOCK_DGRAM, 0);
	addr_set(&me, LOOPBACK, (UH)( UDP_PORT + k ));
	addr_set(&peer, LOOPBACK, (UH)( UDP_PORT + ( k ^ 1 ) ));
	if ( s >= 0 && so_bind(s, (struct sockaddr *)&me, sizeof(me)) == E_OK ) {
		knl_memset(b, (INT)k, sizeof(b));
		while ( !st_stop ) {
			len = 1 + (INT)( st_rand(&seed) % sizeof(b) );
			(void)so_sendto(s, b, len, 0, (struct sockaddr *)&peer, sizeof(peer));
			FD_ZERO(&rd);
			FD_SET(s, &rd);
			if ( so_select(s + 1, &rd, NULL, NULL, 1 + (TMO)( st_rand(&seed) % 2 )) > 0 ) {
				n = so_recvfrom(s, b, sizeof(b), MSG_DONTWAIT, NULL, NULL);
				if ( n > 0 ) n_udp++;
			} else {
				n_udp_lost++;
			}
		}
	}
	if ( s >= 0 ) (void)so_close(s);
	st_end();
}

/* Waits on a socket nothing comes to, each running out after a millisecond */
LOCAL void idle_task( INT k, void *exinf )
{
	struct sockaddr_in	me;
	fd_set			rd;
	INT			s;

	s = so_socket(AF_INET, SOCK_DGRAM, 0);
	addr_set(&me, LOOPBACK, (UH)( UDP_PORT + 2 * UDP_PAIRS + k ));
	if ( s >= 0 && so_bind(s, (struct sockaddr *)&me, sizeof(me)) == E_OK ) {
		while ( !st_stop ) {
			FD_ZERO(&rd);
			FD_SET(s, &rd);
			(void)so_select(s + 1, &rd, NULL, NULL, 1);
			n_idle++;
		}
	}
	if ( s >= 0 ) (void)so_close(s);
	st_end();
}

/* Sockets made and closed straight away, with a call or two between */
LOCAL void churn_task( INT k, void *exinf )
{
	INT	s;

	while ( !st_stop ) {
		s = so_socket(( n_churn & 1 ) ? AF_INET : AF_INET6, ( n_churn & 2 ) ? SOCK_DGRAM : SOCK_STREAM, 0);
		if ( s >= 0 ) {
			(void)so_fcntl(s, F_GETFL, 0);
			timeout_ms(s, SO_SNDTIMEO, 3);
			(void)so_close(s);
		}
		n_churn++;
		if ( ( n_churn & 63 ) == 0 ) tk_dly_tsk(1);
	}
	st_end();
}

/*
 * Processes, one at a time: the program connects and sends a byte, then
 * waits for an answer. From the echoing server it has one and ends by
 * itself with its socket open; from the holding one it has none and is
 * ended while it waits in the stack.
 */
LOCAL void proc_task( INT k, void *exinf )
{
	T_CPRC	cprc;
	T_PSTS	psts;
	UW	arg[8], seed = 0xc2b2ae35U * (UW)( k + 1 );
	BOOL	hold;
	ID	pid;

	while ( st_prog && !st_stop ) {
		hold = ( ( st_rand(&seed) & 1 ) != 0 );
		knl_memset(arg, 0, sizeof(arg));
		arg[0] = SP_BLOCK;
		arg[1] = 0;
		arg[2] = hold ? HOLD_PORT : ECHO_PORT;
		cprc.pri    = KT_PRI_HIGH + 10;
		cprc.prcatr = 0;
		cprc.stksz  = 0;
		cprc.arg    = arg;
		cprc.argsz  = sizeof(arg);
		pid = ts_cre_prc(SVCPROG, &cprc);
		if ( pid <= 0 ) {
			tk_dly_tsk(50);
			continue;
		}
		psts.exitcd = 0;
		if ( hold ) {
			tk_dly_tsk(20 + (TMO)( st_rand(&seed) % 200 ));
			(void)ts_ter_prc(pid, -5);
			n_kill++;
		}
		if ( ts_wai_prc(pid, &psts, 10000) < E_OK ) {
			(void)ts_ter_prc(pid, -5);
			(void)ts_wai_prc(pid, &psts, 5000);
		}
		n_proc++;
	}
	st_end();
}

/* ---------------------------------------------------------------- the test */

LOCAL ID st_start( void (*fn)( INT, void * ), INT k, PRI pri )
{
	T_CTSK	ct;
	ID	tid;

	knl_memset(&ct, 0, sizeof(ct));
	ct.tskatr  = TA_HLNG | TA_RNG0;
	ct.task    = (FP)fn;
	ct.itskpri = pri;
	ct.stksz   = 16 * 1024;
	tid = tk_cre_tsk(&ct);
	if ( tid > 0 && tk_sta_tsk(tid, k) < E_OK ) {
		tk_del_tsk(tid);
		tid = E_SYS;
	}
	return tid;
}

LOCAL INT st_listen( UH port, INT backlog )
{
	struct sockaddr_in	me;
	INT			s = so_socket(AF_INET, SOCK_STREAM, 0);

	addr_set(&me, LOOPBACK, port);
	if ( s >= 0 && ( so_bind(s, (struct sockaddr *)&me, sizeof(me)) != E_OK
		      || so_listen(s, backlog) != E_OK ) ) {
		(void)so_close(s);
		s = -1;
	}
	return s;
}

/* The load for KT_RUMP_S seconds, then everything told to stop and the stack tried once more */
LOCAL void test_load( void )
{
	T_CSEM		cs;
	T_FSTAT		st;
	PRI		pri[3] = { KT_PRI_HIGH, 15, KT_PRI_LOW };
	INT		started = 0, ended = 0, i, t, s;
	UW		seed = 1;
	struct sockaddr_in	me;
	UB		b[4];

	s = so_socket(AF_INET, SOCK_DGRAM, 0);
	if ( s < 0 ) KT_SKIP("no stack");
	(void)so_close(s);
	st_prog = ( fs_stat(SVCPROG, &st) >= EX_OK );
	if ( !st_prog ) tm_printf((UB *)"  no %s: no processes in the load\n", SVCPROG);

	st_stop = FALSE;
	n_conn = n_conn_err = n_bytes = n_udp = n_udp_lost = n_idle = n_churn = n_proc = n_kill = 0;
	st_echo = st_listen(ECHO_PORT, 16);
	st_hold = st_listen(HOLD_PORT, 4);
	KT_ASSERT(st_echo >= 0);
	KT_ASSERT(st_hold >= 0);
	cs.exinf = NULL;
	cs.sematr = TA_TFIFO | TA_FIRST;
	cs.isemcnt = 0;
	cs.maxsem = N_TASKS;
	st_done = tk_cre_sem(&cs);
	KT_ASSERT(st_done > 0);
	if ( st_echo < 0 || st_hold < 0 || st_done <= 0 ) goto done;

	for ( i = 0; i < N_ECHO; i++ ) started += ( st_start(echo_task, i, pri[i % 3]) > 0 );
	started += ( st_start(hold_task, 0, 15) > 0 );
	for ( i = 0; i < N_CLIENT; i++ ) started += ( st_start(client_task, i, pri[( i + 1 ) % 3]) > 0 );
	for ( i = 0; i < 2 * UDP_PAIRS; i++ ) started += ( st_start(udp_task, i, pri[i % 3]) > 0 );
	for ( i = 0; i < N_IDLE; i++ ) started += ( st_start(idle_task, i, pri[( i + 2 ) % 3]) > 0 );
	started += ( st_start(churn_task, 0, KT_PRI_LOW) > 0 );
	for ( i = 0; i < N_PROC; i++ ) started += ( st_start(proc_task, i, 15) > 0 );
	KT_ASSERT_EQ(started, N_TASKS);

	for ( t = 0; t < KT_RUMP_S; t++ ) {
		tk_dly_tsk(1000);
		if ( ( t + 1 ) % 5 == 0 || t + 1 == KT_RUMP_S ) {
			tm_printf((UB *)"  %ds: %d connections (%d refused), %d KB, %d datagrams (%d not back),"
				  " %d empty waits, %d sockets, %d processes (%d ended by force)\n",
				  t + 1, n_conn, n_conn_err, n_bytes / 1024, n_udp, n_udp_lost,
				  n_idle, n_churn, n_proc, n_kill);
		}
		(void)st_rand(&seed);
	}

	/* told to stop; the servers taken out of their accept by closing what they wait on */
	st_stop = TRUE;
	for ( i = 0; i < 300 && ended < started; i++ ) {
		if ( tk_wai_sem(st_done, 1, 100) == E_OK ) {
			ended++;
		} else if ( i == 50 ) {
			(void)so_close(st_echo);
			(void)so_close(st_hold);
			st_echo = st_hold = -1;
		}
	}
	KT_ASSERT_EQ(ended, started);
	KT_ASSERT(n_conn > 0);
	KT_ASSERT(n_udp > 0);
	KT_ASSERT(n_idle > 0);
	KT_ASSERT_EQ(n_conn_err, 0);

	/* and the stack still carries a connection */
	s = st_listen(ECHO_PORT + 2, 1);
	KT_ASSERT(s >= 0);
	if ( s >= 0 ) {
		INT	c = so_socket(AF_INET, SOCK_STREAM, 0), a = -1;

		addr_set(&me, LOOPBACK, ECHO_PORT + 2);
		KT_ASSERT(c >= 0);
		if ( c >= 0 ) {
			KT_ASSERT_ER(so_connect(c, (struct sockaddr *)&me, sizeof(me)), E_OK);
			a = so_accept(s, NULL, NULL);
			KT_ASSERT(a >= 0);
			timeout_ms(c, SO_RCVTIMEO, 5000);
			KT_ASSERT_EQ(so_send(a, "ok", 2, 0), 2);
			KT_ASSERT_EQ(so_recv(c, b, sizeof(b), 0), 2);
			if ( a >= 0 ) (void)so_close(a);
			(void)so_close(c);
		}
		(void)so_close(s);
	}

    done:
	if ( st_echo >= 0 ) (void)so_close(st_echo);
	if ( st_hold >= 0 ) (void)so_close(st_hold);
	st_echo = st_hold = -1;
	if ( st_done > 0 ) (void)tk_del_sem(st_done);
}

EXPORT void ktest_rump( void )
{
	KT_RUN(test_load);
}
