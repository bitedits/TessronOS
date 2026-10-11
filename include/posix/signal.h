/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	signal.h
 *	Signals: sys/signal.h, and the names a program prints them by
 *	(design 5.8.5)
 */

#ifndef _SIGNAL_H_
#define _SIGNAL_H_

#include <sys/signal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int		sig_atomic_t;
typedef _sig_func_ptr	sig_t;
typedef _sig_func_ptr	sighandler_t;

extern const char *const sys_signame[NSIG];
extern const char *const sys_siglist[NSIG];
extern const int sys_nsig;

/* the names of the signals without "SIG", one after another */
const char	*signalname( int );
int		signalnumber( const char * );
int		signalnext( int );

_sig_func_ptr	signal( int, _sig_func_ptr );
int		raise( int );
void		psignal( int, const char * );
void		psiginfo( const siginfo_t *, const char * );
char		*strsignal( int );

#ifdef __cplusplus
}
#endif

#endif /* _SIGNAL_H_ */
