/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	stdio.h
 *	newlib's, with BSD's reading of a line and its continuations
 *	(design 5.8.3)
 */

#ifndef __POSIX_STDIO_H__
#define __POSIX_STDIO_H__

#include_next <stdio.h>

#ifndef FPARSELN_UNESCESC
#define FPARSELN_UNESCESC	0x01
#define FPARSELN_UNESCCONT	0x02
#define FPARSELN_UNESCCOMM	0x04
#define FPARSELN_UNESCREST	0x08
#define FPARSELN_UNESCALL	0x0f
#endif

#ifdef __cplusplus
extern "C" {
#endif

char	*fgetln( FILE *, size_t * );
ssize_t	getline( char **, size_t *, FILE * );
ssize_t	getdelim( char **, size_t *, int, FILE * );

FILE	*funopen2( const void *, ssize_t (*)( void *, void *, size_t ),
		   ssize_t (*)( void *, const void *, size_t ), off_t (*)( void *, off_t, int ),
		   int (*)( void * ), int (*)( void * ) );

/* formatting that may be done in a signal handler */
int	snprintf_ss( char *__restrict, size_t, const char *__restrict, ... );
int	vsnprintf_ss( char *__restrict, size_t, const char *__restrict, __VALIST );
char	*fparseln( FILE *, size_t *, size_t *, const char[3], int );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_STDIO_H__ */
