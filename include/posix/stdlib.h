/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	stdlib.h
 *	newlib's, with the BSD functions the commands use (design 5.8.3)
 */

#ifndef __POSIX_STDLIB_H__
#define __POSIX_STDLIB_H__

#include_next <stdlib.h>
#include <sys/types.h>
#include <stdint.h>

#ifndef HN_DECIMAL
#define HN_DECIMAL		0x01
#define HN_NOSPACE		0x02
#define HN_B			0x04
#define HN_DIVISOR_1000		0x08
#define HN_GETSCALE		0x10
#define HN_AUTOSCALE		0x20
#endif

#ifdef __cplusplus
extern "C" {
#endif

const char	*getprogname( void );
void		setprogname( const char * );
int		reallocarr( void *, size_t, size_t );
void		mi_vector_hash( const void *, size_t, uint32_t, uint32_t[3] );
void		setproctitle( const char *, ... );	/* a process shows no title of its own */
intmax_t	strtoi( const char *, char **, int, intmax_t, intmax_t, int * );
uintmax_t	strtou( const char *, char **, int, uintmax_t, uintmax_t, int * );
long long	strtonum( const char *, long long, long long, const char ** );
int		heapsort( void *, size_t, size_t, int (*)( const void *, const void * ) );
int		mergesort( void *, size_t, size_t, int (*)( const void *, const void * ) );
int		radixsort( const unsigned char **, int, const unsigned char *, unsigned int );
int		sradixsort( const unsigned char **, int, const unsigned char *, unsigned int );
char		*getbsize( int *, long * );
char		*devname( dev_t, mode_t );
int		humanize_number( char *, size_t, int64_t, const char *, int, int );
int		dehumanize_number( const char *, int64_t * );
long long	strsuftoll( const char *, const char *, long long, long long );
long long	strsuftollx( const char *, const char *, long long, long long, char *, size_t );
char		*cgetcap( char *, const char *, int );
int		getloadavg( double [], int );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_STDLIB_H__ */
