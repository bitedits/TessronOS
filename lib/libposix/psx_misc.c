/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	psx_misc.c
 *	The rest of lib/libposix (design 5.8.3): the messages of err.h, the
 *	program's name, random bytes, and the small BSD functions the
 *	commands take for granted.
 */

#include "psx_int.h"
#include <ts/ob.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---------------------------------------------------------------- err.h */

static void vmsg( int code, int use, const char *fmt, va_list ap )
{
	fprintf(stderr, "%s: ", getprogname());
	if ( fmt != NULL ) {
		vfprintf(stderr, fmt, ap);
		if ( use ) fprintf(stderr, ": ");
	}
	if ( use ) fprintf(stderr, "%s", strerror(code));
	fprintf(stderr, "\n");
	fflush(stderr);
}

void vwarnc( int code, const char *fmt, va_list ap )	{ vmsg(code, 1, fmt, ap); }
void vwarn( const char *fmt, va_list ap )		{ vmsg(errno, 1, fmt, ap); }
void vwarnx( const char *fmt, va_list ap )		{ vmsg(0, 0, fmt, ap); }
void verrc( int ev, int code, const char *fmt, va_list ap ) { vmsg(code, 1, fmt, ap); exit(ev); }
void verr( int ev, const char *fmt, va_list ap )	{ vmsg(errno, 1, fmt, ap); exit(ev); }
void verrx( int ev, const char *fmt, va_list ap )	{ vmsg(0, 0, fmt, ap); exit(ev); }

void warnc( int code, const char *fmt, ... )
{
	va_list	ap;

	va_start(ap, fmt);
	vwarnc(code, fmt, ap);
	va_end(ap);
}

void warn( const char *fmt, ... )
{
	va_list	ap;
	int	e = errno;

	va_start(ap, fmt);
	vwarnc(e, fmt, ap);
	va_end(ap);
}

void warnx( const char *fmt, ... )
{
	va_list	ap;

	va_start(ap, fmt);
	vwarnx(fmt, ap);
	va_end(ap);
}

void errc( int ev, int code, const char *fmt, ... )
{
	va_list	ap;

	va_start(ap, fmt);
	verrc(ev, code, fmt, ap);
	va_end(ap);
}

void err( int ev, const char *fmt, ... )
{
	va_list	ap;
	int	e = errno;

	va_start(ap, fmt);
	verrc(ev, e, fmt, ap);
	va_end(ap);
}

void errx( int ev, const char *fmt, ... )
{
	va_list	ap;

	va_start(ap, fmt);
	verrx(ev, fmt, ap);
	va_end(ap);
}

/* ---------------------------------------------------------------- the program's name */

const char *getprogname( void )
{
	return __psx_progname;
}

void setprogname( const char *name )
{
	const char *b = strrchr(name, '/');

	__psx_progname = ( b != NULL ) ? b + 1 : name;
}

/* ---------------------------------------------------------------- random bytes */

extern ID ts_random_key( void );

int getentropy( void *buf, size_t len )
{
	SZ	asz = 0;
	ID	key;

	if ( len > 256 ) {
		errno = EIO;
		return -1;
	}
	key = ts_random_key();
	if ( key <= 0 || ob_rea_rec(key, OB_RND_DATA, 0, buf, (SZ)len, &asz) < E_OK || asz != (SZ)len ) {
		errno = EIO;
		return -1;
	}
	return 0;
}

ssize_t getrandom( void *buf, size_t len, unsigned int flags )
{
	size_t	done = 0;

	(void)flags;
	while ( done < len ) {
		size_t	n = ( len - done > 256 ) ? 256 : len - done;

		if ( getentropy((char *)buf + done, n) < 0 ) return ( done > 0 ) ? (ssize_t)done : -1;
		done += n;
	}
	return (ssize_t)done;
}

/* ---------------------------------------------------------------- small functions */

int reallocarr( void *ptr, size_t n, size_t size )
{
	void	**p = (void **)ptr, *q;

	if ( n == 0 || size == 0 ) {
		free(*p);
		*p = NULL;
		return 0;
	}
	if ( n > SIZE_MAX / size ) return EOVERFLOW;
	q = realloc(*p, n * size);
	if ( q == NULL ) return errno;
	*p = q;
	return 0;
}

void *explicit_memset( void *b, int c, size_t n )
{
	volatile unsigned char *p = (volatile unsigned char *)b;

	while ( n-- > 0 ) *p++ = (unsigned char)c;
	return b;
}

int consttime_memequal( const void *a, const void *b, size_t n )
{
	const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
	unsigned char	d = 0;

	while ( n-- > 0 ) d |= *x++ ^ *y++;
	return d == 0;
}

int getloadavg( double l[], int n )
{
	int	i;

	for ( i = 0; i < n && i < 3; i++ ) l[i] = 0.0;
	return ( n < 3 ) ? n : 3;
}

char *devname( dev_t dev, mode_t type )
{
	static char	buf[32];

	(void)type;
	if ( dev >= 0x500 && dev < 0x600 ) {
		snprintf(buf, sizeof(buf), "pts%u", (unsigned int)( dev - 0x500 ));
	} else {
		snprintf(buf, sizeof(buf), "#%u", (unsigned int)dev);
	}
	return buf;
}

/*
 * BSD's word that getopt starts over: newlib's getopt starts over when
 * optind is set back to 1 at the end of a list, which is what the
 * programs that set this do with it
 */
int optreset;

void sync( void )
{
	(void)fs_sync();
}
