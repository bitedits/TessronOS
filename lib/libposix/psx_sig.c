/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	psx_sig.c
 *	Signals (lib/libposix, design 5.8.5)
 *
 *	The kernel keeps what each signal does -- caught, ignored, by
 *	default -- and the mask; the handlers are kept here. A caught signal
 *	is handed to __psx_sigentry (psx_crt.S) as one of the program's tasks comes back
 *	from a system call: the handler runs there, errno kept round it,
 *	with sa_mask held back as well, and ts_sig_ret goes back to where the
 *	task was with the mask it had. A call that waited and was cut short
 *	by a handler with SA_RESTART is made again (__psx_restart).
 */

#include "psx_int.h"
#include <sys/types.h>
#include <sys/time.h>
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static struct sigaction	acts[NSIG];
static volatile int	restart_flag;
static struct itimerval	itimer;

#define UNCATCH		( ( 1UL << SIGKILL ) | ( 1UL << SIGSTOP ) )

/* What the kernel is told of the dispositions: those caught, those ignored */
static void sig_tell( void )
{
	T_TSSIG	s;
	int	i;

	memset(&s, 0, sizeof(s));
	for ( i = 1; i < NSIG; i++ ) {
		if ( acts[i].sa_handler == SIG_IGN ) {
			s.ign |= 1UL << i;
		} else if ( acts[i].sa_handler != SIG_DFL ) {
			s.catch_ |= 1UL << i;
		}
	}
	(void)ts_sig_set(TS_SS_CATCH | TS_SS_IGN, &s, NULL);
}

extern void __psx_sigentry( void );

/* The handler of a caught signal, called by __psx_sigentry (psx_crt.S) */
void __psx_sighandle( T_TSSIGFR *fr, int sig )
{
	struct sigaction a;
	siginfo_t	si;
	int		e = errno;
	T_TSSIG		m;

	if ( sig > 0 && sig < NSIG ) {
		a = acts[sig];
		if ( a.sa_flags & SA_RESETHAND ) {
			acts[sig].sa_handler = SIG_DFL;
			acts[sig].sa_flags &= ~SA_SIGINFO;
			sig_tell();
		}
		if ( a.sa_mask != 0 ) {
			m.mask = (UD)a.sa_mask & ~UNCATCH;
			(void)ts_sig_set(TS_SS_BLOCK, &m, NULL);
		}
		if ( a.sa_flags & SA_RESTART ) restart_flag = 1;
		if ( sig == SIGALRM && ( itimer.it_interval.tv_sec != 0 || itimer.it_interval.tv_usec != 0 ) ) {
			(void)ts_sig_alm((UINT)( itimer.it_interval.tv_sec * 1000 + itimer.it_interval.tv_usec / 1000 ));
		}
		if ( a.sa_handler != SIG_DFL && a.sa_handler != SIG_IGN ) {
			if ( a.sa_flags & SA_SIGINFO ) {
				memset(&si, 0, sizeof(si));
				si.si_signo = sig;
				si.si_code = ( fr->code == TS_SI_USER ) ? SI_USER : SI_KERNEL;
				si.si_pid = (pid_t)fr->from;
				a.sa_sigaction(sig, &si, NULL);
			} else {
				a.sa_handler(sig);
			}
		}
	}
	errno = e;
}

/* Whether a call cut short by a signal is to be made again; asked once */
int __psx_restart( void )
{
	int	r = restart_flag;

	restart_flag = 0;
	return r;
}

void __psx_sig_init( void )
{
	T_TSSIG	s;
	int	i;

	memset(&s, 0, sizeof(s));
	s.entry = (UD)(unsigned long)__psx_sigentry;
	(void)ts_sig_set(TS_SS_ENTRY, &s, &s);
	/* what was ignored when the program was started stays so */
	for ( i = 1; i < NSIG; i++ ) {
		acts[i].sa_handler = ( s.ign & ( 1UL << i ) ) ? SIG_IGN : SIG_DFL;
	}
}

int sigaction( int sig, const struct sigaction *act, struct sigaction *old )
{
	if ( sig <= 0 || sig >= NSIG ) {
		errno = EINVAL;
		return -1;
	}
	if ( old != NULL ) *old = acts[sig];
	if ( act != NULL ) {
		if ( sig == SIGKILL || sig == SIGSTOP ) {
			errno = EINVAL;
			return -1;
		}
		acts[sig] = *act;
		sig_tell();
	}
	return 0;
}

_sig_func_ptr signal( int sig, _sig_func_ptr fn )
{
	struct sigaction a, old;

	memset(&a, 0, sizeof(a));
	a.sa_handler = fn;
	a.sa_flags = SA_RESTART;
	if ( sigaction(sig, &a, &old) < 0 ) return SIG_ERR;
	return old.sa_handler;
}

_sig_func_ptr _signal_r( struct _reent *r, int sig, _sig_func_ptr fn )
{
	(void)r;
	return signal(sig, fn);
}

int siginterrupt( int sig, int flag )
{
	struct sigaction a;

	if ( sigaction(sig, NULL, &a) < 0 ) return -1;
	if ( flag ) a.sa_flags &= ~SA_RESTART; else a.sa_flags |= SA_RESTART;
	return sigaction(sig, &a, NULL);
}

int sigprocmask( int how, const sigset_t *set, sigset_t *old )
{
	T_TSSIG	s, o;
	UINT	what;
	ER	er;

	if ( set == NULL ) {
		er = ts_sig_set(0, NULL, &o);
	} else {
		memset(&s, 0, sizeof(s));
		s.mask = (UD)*set & ~UNCATCH;
		switch ( how ) {
		  case SIG_BLOCK:	what = TS_SS_BLOCK; break;
		  case SIG_UNBLOCK:	what = TS_SS_UNBLOCK; break;
		  case SIG_SETMASK:	what = TS_SS_MASK; break;
		  default:
			errno = EINVAL;
			return -1;
		}
		er = ts_sig_set(what, &s, &o);
	}
	if ( er < 0 ) return __psx_fail_er(er);
	if ( old != NULL ) *old = (sigset_t)o.mask;
	return 0;
}

int pthread_sigmask( int how, const sigset_t *set, sigset_t *old )
{
	return ( sigprocmask(how, set, old) < 0 ) ? errno : 0;
}

int sigpending( sigset_t *set )
{
	T_TSSIG	o;
	ER	er = ts_sig_set(0, NULL, &o);

	if ( er < 0 ) return __psx_fail_er(er);
	*set = (sigset_t)( o.pend & o.mask );
	return 0;
}

int sigsuspend( const sigset_t *set )
{
	(void)ts_sig_sus((UD)*set & ~UNCATCH);
	errno = EINTR;
	return -1;
}

int pause( void )
{
	sigset_t m = 0;

	(void)sigprocmask(SIG_BLOCK, NULL, &m);
	return sigsuspend(&m);
}

int sighold( int sig )
{
	sigset_t m = 1UL << sig;

	return sigprocmask(SIG_BLOCK, &m, NULL);
}

int sigrelse( int sig )
{
	sigset_t m = 1UL << sig;

	return sigprocmask(SIG_UNBLOCK, &m, NULL);
}

int sigignore( int sig )
{
	return ( signal(sig, SIG_IGN) == SIG_ERR ) ? -1 : 0;
}

int sigpause( int sig )
{
	sigset_t m = 0;

	(void)sigprocmask(SIG_BLOCK, NULL, &m);
	m &= ~( 1UL << sig );
	return sigsuspend(&m);
}

int sigwait( const sigset_t *set, int *sig )
{
	(void)set;
	(void)sig;
	return ENOSYS;
}

int sigaltstack( const stack_t *ss, stack_t *old )
{
	(void)ss;
	if ( old != NULL ) {
		memset(old, 0, sizeof(*old));
		old->ss_flags = SS_DISABLE;
	}
	return 0;
}

int kill( pid_t pid, int sig )
{
	ER	er = ts_kil_prc((ID)pid, sig);

	if ( er < 0 ) {
		errno = ( er == E_NOEXS ) ? ESRCH : ( er == E_OACV ) ? EPERM : __psx_errno_er(er);
		return -1;
	}
	return 0;
}

int _kill( int pid, int sig ) __attribute__((alias("kill")));

int killpg( pid_t pg, int sig )
{
	if ( pg <= 1 ) {
		errno = EINVAL;
		return -1;
	}
	return kill(-pg, sig);
}

int raise( int sig )
{
	return kill(getpid(), sig);
}

int _raise_r( struct _reent *r, int sig )
{
	(void)r;
	return raise(sig);
}

void abort( void )
{
	sigset_t m = 1UL << SIGABRT;

	(void)sigprocmask(SIG_UNBLOCK, &m, NULL);
	(void)raise(SIGABRT);
	signal(SIGABRT, SIG_DFL);
	(void)raise(SIGABRT);
	_exit(127);
}

/* ---------------------------------------------------------------- time */

unsigned int alarm( unsigned int sec )
{
	INT	left = ts_sig_alm(sec * 1000);

	memset(&itimer, 0, sizeof(itimer));
	return ( left > 0 ) ? (unsigned int)( ( left + 999 ) / 1000 ) : 0;
}

useconds_t ualarm( useconds_t us, useconds_t interval )
{
	INT	left = ts_sig_alm((UINT)( ( us + 999 ) / 1000 ));

	itimer.it_interval.tv_sec = interval / 1000000;
	itimer.it_interval.tv_usec = interval % 1000000;
	return ( left > 0 ) ? (useconds_t)left * 1000 : 0;
}

int setitimer( int which, const struct itimerval *v, struct itimerval *old )
{
	INT	left;

	if ( which != ITIMER_REAL ) {
		errno = EINVAL;
		return -1;
	}
	left = ts_sig_alm((UINT)( v->it_value.tv_sec * 1000 + ( v->it_value.tv_usec + 999 ) / 1000 ));
	if ( old != NULL ) {
		old->it_interval = itimer.it_interval;
		old->it_value.tv_sec = ( left > 0 ) ? left / 1000 : 0;
		old->it_value.tv_usec = ( left > 0 ) ? ( left % 1000 ) * 1000 : 0;
	}
	itimer = *v;
	return 0;
}

int getitimer( int which, struct itimerval *v )
{
	INT	left;

	if ( which != ITIMER_REAL ) {
		errno = EINVAL;
		return -1;
	}
	left = ts_sig_alm(0);
	if ( left > 0 ) (void)ts_sig_alm((UINT)left);
	v->it_interval = itimer.it_interval;
	v->it_value.tv_sec = ( left > 0 ) ? left / 1000 : 0;
	v->it_value.tv_usec = ( left > 0 ) ? ( left % 1000 ) * 1000 : 0;
	return 0;
}

int nanosleep( const struct timespec *req, struct timespec *rem )
{
	UD	left = 0;
	ER	er;

	if ( req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000L ) {
		errno = EINVAL;
		return -1;
	}
	er = ts_slp_psx((UD)req->tv_sec * 1000000000ULL + (UD)req->tv_nsec, &left);
	if ( er == E_RLWAI ) {
		if ( rem != NULL ) {
			rem->tv_sec = (time_t)( left / 1000000000ULL );
			rem->tv_nsec = (long)( left % 1000000000ULL );
		}
		errno = EINTR;
		return -1;
	}
	if ( rem != NULL ) {
		rem->tv_sec = 0;
		rem->tv_nsec = 0;
	}
	return 0;
}

unsigned int sleep( unsigned int s )
{
	struct timespec	t, r;

	t.tv_sec = s;
	t.tv_nsec = 0;
	if ( nanosleep(&t, &r) < 0 ) return (unsigned int)( r.tv_sec + ( r.tv_nsec > 0 ) );
	return 0;
}

int usleep( useconds_t us )
{
	struct timespec	t;

	t.tv_sec = us / 1000000;
	t.tv_nsec = (long)( us % 1000000 ) * 1000;
	return nanosleep(&t, NULL);
}

/* ---------------------------------------------------------------- names */

const int sys_nsig = NSIG;

/* ---------------------------------------------------------------- jumps that keep the mask */

void __psx_sigsave( sigjmp_buf env, int save )
{
	env[0].__saved = save;
	if ( save ) (void)sigprocmask(SIG_BLOCK, NULL, &env[0].__mask);
}

void siglongjmp( sigjmp_buf env, int val )
{
	if ( env[0].__saved ) (void)sigprocmask(SIG_SETMASK, &env[0].__mask, NULL);
	longjmp(env[0].__jb, ( val == 0 ) ? 1 : val);
}

/* The number of a signal by its name, with or without "SIG", in any case; 0 for none */
int signalnumber( const char *name )
{
	int	i;

	if ( strncasecmp(name, "sig", 3) == 0 ) name += 3;
	for ( i = 1; i < NSIG; i++ ) {
		if ( sys_signame[i] != NULL && strcasecmp(name, sys_signame[i]) == 0 ) return i;
	}
	return 0;
}

const char *const sys_signame[NSIG] = {
	"Signal 0", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "EMT",
	"FPE", "KILL", "BUS", "SEGV", "SYS", "PIPE", "ALRM", "TERM",
	"URG", "STOP", "TSTP", "CONT", "CHLD", "TTIN", "TTOU", "IO",
	"XCPU", "XFSZ", "VTALRM", "PROF", "WINCH", "INFO", "USR1", "USR2",
};

const char *const sys_siglist[NSIG] = {
	"Signal 0", "Hangup", "Interrupt", "Quit", "Illegal instruction",
	"Trace/BPT trap", "Abort trap", "EMT trap", "Floating point exception",
	"Killed", "Bus error", "Segmentation fault", "Bad system call",
	"Broken pipe", "Alarm clock", "Terminated", "Urgent I/O condition",
	"Suspended (signal)", "Suspended", "Continued", "Child exited",
	"Stopped (tty input)", "Stopped (tty output)", "I/O possible",
	"Cputime limit exceeded", "Filesize limit exceeded",
	"Virtual timer expired", "Profiling timer expired",
	"Window size changes", "Information request",
	"User defined signal 1", "User defined signal 2",
};

char *strsignal( int sig )
{
	static char	buf[32];

	if ( sig > 0 && sig < NSIG ) return (char *)sys_siglist[sig];
	snprintf(buf, sizeof(buf), "Unknown signal %d", sig);
	return buf;
}

void psignal( int sig, const char *s )
{
	if ( s != NULL && s[0] != 0 ) fprintf(stderr, "%s: ", s);
	fprintf(stderr, "%s\n", strsignal(sig));
}
