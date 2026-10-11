/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	util.h
 *	The helpers of BSD's libutil the commands use (design 5.8.3): the
 *	functions that end the program when memory runs out, a line read
 *	with its continuations, numbers made short with a unit, a signal
 *	raised as by default, pseudo terminals. Built from NetBSD's sources
 *	(the commands' build, tools/netbsd/cmds.sh) or lib/libposix.
 */

#ifndef _UTIL_H_
#define _UTIL_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <termios.h>
#include <pwd.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* efun */
void	(*esetfunc( void (*)( int, const char *, ... ) ))( int, const char *, ... );
size_t	estrlcpy( char *, const char *, size_t );
size_t	estrlcat( char *, const char *, size_t );
char	*estrdup( const char * );
char	*estrndup( const char *, size_t );
intmax_t estrtoi( const char *, int, intmax_t, intmax_t );
uintmax_t estrtou( const char *, int, uintmax_t, uintmax_t );
void	*ecalloc( size_t, size_t );
void	*emalloc( size_t );
void	*erealloc( void *, size_t );
void	ereallocarr( void *, size_t, size_t );
FILE	*efopen( const char *, const char * );
int	easprintf( char ** __restrict, const char * __restrict, ... ) __printflike(2, 3);
int	evasprintf( char ** __restrict, const char * __restrict, va_list ) __printflike(2, 0);

/* fparseln: in stdio.h */

/* humanize_number: the HN_ flags are in stdlib.h */
int	humanize_number( char *, size_t, int64_t, const char *, int, int );
int	dehumanize_number( const char *, int64_t * );

int	raise_default_signal( int );
#define STRPCT_RTN	0U
#define STRPCT_RAZ	2U
#define STRPCT_RTZ	3U
#define STRPCT_RTI	6U
#define STRPCT_RAI	7U
#define STRPCT_RQRY	8U
char	*strpct( char *, size_t, uintmax_t, uintmax_t, size_t );
char	*strspct( char *, size_t, intmax_t, intmax_t, size_t );
char	*strpct_r( char *, size_t, uintmax_t, uintmax_t, size_t, uint32_t );
char	*strspct_r( char *, size_t, intmax_t, intmax_t, size_t, uint32_t );
uint32_t strpct_round( uint32_t );
int	string_to_flags( char **, unsigned long *, unsigned long * );
char	*flags_to_string( unsigned long, const char * );
int	snprintb( char *, size_t, const char *, uint64_t );

int	openpty( int *, int *, char *, const struct termios *, const struct winsize * );
int	login_tty( int );
pid_t	forkpty( int *, char *, struct termios *, struct winsize * );

int	pidfile( const char * );

time_t	parsedate( const char *, const time_t *, const int * );

/* the records of logins: none are kept */
void	logwtmp( const char *, const char *, const char * );
int	logout( const char * );
int	logoutx( const char *, int, int );

#ifdef __cplusplus
}
#endif

#endif /* _UTIL_H_ */
