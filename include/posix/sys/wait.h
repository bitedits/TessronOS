/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/wait.h
 *	Waiting for children (design 5.8.2): the status as the kernel's
 *	ts_wai_psx gives it, read by the usual macros.
 */

#ifndef _SYS_WAIT_H
#define _SYS_WAIT_H

#include <sys/types.h>
#include <sys/resource.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WNOHANG		0x00000001
#define WUNTRACED	0x00000002
#define WSTOPPED	WUNTRACED
#define WCONTINUED	0x00000010
#define WEXITED		0x00000020
#define WNOWAIT		0x00010000
#define WALLSIG		0x00000008
#define WALTSIG		0x00000004

#define _W_INT(w)	(*(int *)&(w))
#define WCOREFLAG	0200
#define _WSTATUS(x)	( _W_INT(x) & 0177 )
#define _WSTOPPED	0177

#define WIFSTOPPED(x)	( _WSTATUS(x) == _WSTOPPED && !WIFCONTINUED(x) )
#define WSTOPSIG(x)	( (int)( ( (unsigned int)_W_INT(x) ) >> 8 ) & 0xff )
#define WIFSIGNALED(x)	( _WSTATUS(x) != _WSTOPPED && _WSTATUS(x) != 0 && !WIFCONTINUED(x) )
#define WTERMSIG(x)	( _WSTATUS(x) )
#define WIFEXITED(x)	( _WSTATUS(x) == 0 )
#define WEXITSTATUS(x)	( (int)( ( (unsigned int)_W_INT(x) ) >> 8 ) & 0xff )
#define WIFCONTINUED(x)	( _W_INT(x) == 0xffff )
#define WCOREDUMP(x)	( _W_INT(x) & WCOREFLAG )
#define W_EXITCODE(ret, sig)	( (ret) << 8 | (sig) )
#define W_STOPCODE(sig)		( (sig) << 8 | _WSTOPPED )

#define WAIT_ANY	(-1)
#define WAIT_MYPGRP	0

pid_t	wait( int * );
pid_t	waitpid( pid_t, int *, int );
pid_t	wait3( int *, int, struct rusage * );
pid_t	wait4( pid_t, int *, int, struct rusage * );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_WAIT_H */
