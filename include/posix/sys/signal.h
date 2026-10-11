/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/signal.h
 *	Signals (design 5.8.5): numbered as NetBSD numbers them, which is
 *	also how the kernel does (include/ts/psx.h TS_SIG*). A handler runs
 *	on the task that comes back from a system call (lib/libposix
 *	psx_sig.c).
 */

#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/_sigset.h>
#include <sys/_timespec.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SIGSET_T_DECLARED
#define _SIGSET_T_DECLARED
typedef __sigset_t	sigset_t;
#endif

#define SIGHUP		1
#define SIGINT		2
#define SIGQUIT		3
#define SIGILL		4
#define SIGTRAP		5
#define SIGABRT		6
#define SIGIOT		SIGABRT
#define SIGEMT		7
#define SIGFPE		8
#define SIGKILL		9
#define SIGBUS		10
#define SIGSEGV		11
#define SIGSYS		12
#define SIGPIPE		13
#define SIGALRM		14
#define SIGTERM		15
#define SIGURG		16
#define SIGSTOP		17
#define SIGTSTP		18
#define SIGCONT		19
#define SIGCHLD		20
#define SIGCLD		SIGCHLD
#define SIGTTIN		21
#define SIGTTOU		22
#define SIGIO		23
#define SIGPOLL		SIGIO
#define SIGXCPU		24
#define SIGXFSZ		25
#define SIGVTALRM	26
#define SIGPROF		27
#define SIGWINCH	28
#define SIGINFO		29
#define SIGUSR1		30
#define SIGUSR2		31
#define NSIG		32
#define _NSIG		NSIG

typedef void (*_sig_func_ptr)( int );

union sigval {
	int	sival_int;
	void	*sival_ptr;
};

typedef struct {
	int		si_signo;
	int		si_code;
	int		si_errno;
	pid_t		si_pid;
	uid_t		si_uid;
	int		si_status;
	void		*si_addr;
	union sigval	si_value;
} siginfo_t;

#define SI_USER		0
#define SI_QUEUE	-1
#define SI_TIMER	-2
#define SI_KERNEL	128
#define CLD_EXITED	1
#define CLD_KILLED	2
#define CLD_DUMPED	3
#define CLD_STOPPED	5
#define CLD_CONTINUED	6
#define CLD_TRAPPED	4

#define ILL_ILLOPC	1
#define ILL_ILLOPN	2
#define ILL_ILLADR	3
#define ILL_ILLTRP	4
#define ILL_PRVOPC	5
#define ILL_PRVREG	6
#define ILL_COPROC	7
#define ILL_BADSTK	8

#define FPE_INTDIV	1
#define FPE_INTOVF	2
#define FPE_FLTDIV	3
#define FPE_FLTOVF	4
#define FPE_FLTUND	5
#define FPE_FLTRES	6
#define FPE_FLTINV	7
#define FPE_FLTSUB	8

#define SEGV_MAPERR	1
#define SEGV_ACCERR	2

#define BUS_ADRALN	1
#define BUS_ADRERR	2
#define BUS_OBJERR	3

#define TRAP_BRKPT	1
#define TRAP_TRACE	2

struct sigaction {
	union {
		_sig_func_ptr	_sa_handler;
		void		(*_sa_sigaction)( int, siginfo_t *, void * );
	} _sa_u;
	sigset_t	sa_mask;
	int		sa_flags;
};
#define sa_handler	_sa_u._sa_handler
#define sa_sigaction	_sa_u._sa_sigaction

#define SA_ONSTACK	0x0001
#define SA_RESTART	0x0002
#define SA_RESETHAND	0x0004
#define SA_NODEFER	0x0010
#define SA_NOCLDSTOP	0x0008
#define SA_NOCLDWAIT	0x0020
#define SA_SIGINFO	0x0040

#define SIG_BLOCK	1
#define SIG_UNBLOCK	2
#define SIG_SETMASK	3

#define SIG_DFL		((_sig_func_ptr)0)
#define SIG_IGN		((_sig_func_ptr)1)
#define SIG_ERR		((_sig_func_ptr)-1)
#define SIG_HOLD	((_sig_func_ptr)3)

typedef struct sigaltstack {
	void	*ss_sp;
	size_t	ss_size;
	int	ss_flags;
} stack_t;
#define SS_ONSTACK	1
#define SS_DISABLE	4
#define MINSIGSTKSZ	8192
#define SIGSTKSZ	( MINSIGSTKSZ + 32768 )

static __inline__ int sigaddset( sigset_t *s, int n )	{ *s |= 1UL << n; return 0; }
static __inline__ int sigdelset( sigset_t *s, int n )	{ *s &= ~( 1UL << n ); return 0; }
static __inline__ int sigismember( const sigset_t *s, int n ) { return ( *s & ( 1UL << n ) ) != 0; }
static __inline__ int sigemptyset( sigset_t *s )	{ *s = 0; return 0; }
static __inline__ int sigfillset( sigset_t *s )		{ *s = ~(sigset_t)0; return 0; }

int	sigprocmask( int, const sigset_t *, sigset_t * );
int	pthread_sigmask( int, const sigset_t *, sigset_t * );
int	sigaction( int, const struct sigaction *, struct sigaction * );
int	sigpending( sigset_t * );
int	sigsuspend( const sigset_t * );
int	sigwait( const sigset_t *, int * );
int	kill( pid_t, int );
int	killpg( pid_t, int );
int	sigaltstack( const stack_t *, stack_t * );
int	siginterrupt( int, int );
int	sighold( int );
int	sigrelse( int );
int	sigignore( int );
int	sigpause( int );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SIGNAL_H */
