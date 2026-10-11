/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	libc.c
 *	The part of the C library that vendored code reaches for, and that
 *	nothing else in TessronOS provides.
 *
 *	The string functions here serve lwIP and FreeType alike; what only
 *	lwIP needs (errno, its random numbers) is in
 *	peripheral_kernel/network/port/lwip_libc.c. A name is implemented in
 *	one place, never both.
 *
 *	The allocator here is the kernel's. FreeType is given an allocator
 *	of its own by the font layer, so malloc is only reached by parts of
 *	it that are not built in; it is provided so that the link does not
 *	depend on which parts those are.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <stddef.h>
#include <stdarg.h>
#include "tstdlib.h"

/* Declared by the compiler's stdio.h, which the kernel does not otherwise use */
int vsnprintf( char *s, size_t n, const char *fmt, va_list ap );
int snprintf( char *s, size_t n, const char *fmt, ... );

/* ---------------------------------------------------------------- text */

void *memchr( const void *s, int c, size_t n )
{
	CONST UB	*p = (CONST UB *)s;
	size_t		i;

	for ( i = 0; i < n; i++ ) {
		if ( p[i] == (UB)c ) {
			return (void *)(p + i);
		}
	}

	return NULL;
}

size_t strlen( const char *s )
{
	return (size_t)knl_strlen(s);
}

char *strcpy( char *dst, const char *src )
{
	return knl_strcpy(dst, src);
}

char *strncpy( char *dst, const char *src, size_t n )
{
	return knl_strncpy(dst, src, (SZ)n);
}

int strcmp( const char *s1, const char *s2 )
{
	return knl_strcmp(s1, s2);
}

int strncmp( const char *s1, const char *s2, size_t n )
{
	size_t	i;

	for ( i = 0; i < n; i++ ) {
		UB	a = (UB)s1[i], b = (UB)s2[i];

		if ( a != b ) {
			return ( a < b ) ? -1 : 1;
		}
		if ( a == 0 ) {
			break;
		}
	}

	return 0;
}

/* The terminator counts as part of the string, as the standard has it */
char *strchr( const char *s, int c )
{
	for ( ;; s++ ) {
		if ( *s == (char)c ) {
			return (char *)s;
		}
		if ( *s == '\0' ) {
			return NULL;
		}
	}
}

char *strstr( const char *haystack, const char *needle )
{
	SZ	n = knl_strlen(needle);
	SZ	i, j;

	if ( n == 0 ) {
		return (char *)haystack;
	}
	for ( i = 0; haystack[i] != '\0'; i++ ) {
		for ( j = 0; j < n && haystack[i + j] == needle[j]; j++ ) {
			;
		}
		if ( j == n ) {
			return (char *)(haystack + i);
		}
		if ( haystack[i + j] == '\0' ) {
			break;
		}
	}

	return NULL;
}

/*
 * Leading space and a sign, then digits, and nothing else: which is all
 * that is asked of it (the number at the end of an interface name).
 */
int atoi( const char *s )
{
	int	sign = 1, v = 0;

	while ( *s == ' ' || *s == '\t' ) {
		s++;
	}
	if ( *s == '-' ) {
		sign = -1;
		s++;
	} else if ( *s == '+' ) {
		s++;
	}
	while ( *s >= '0' && *s <= '9' ) {
		v = v * 10 + (*s - '0');
		s++;
	}

	return v * sign;
}

char *strrchr( const char *s, int c )
{
	CONST char	*last = NULL;

	for ( ;; ) {
		if ( *s == (char)c ) {
			last = s;
		}
		if ( *s == '\0' ) {
			break;
		}
		s++;
	}

	return (char *)last;
}

long atol( const char *s )
{
	long	v = 0;
	int	neg = 0;

	while ( *s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' ) s++;
	if ( *s == '-' ) { neg = 1; s++; }
	else if ( *s == '+' ) { s++; }
	while ( *s >= '0' && *s <= '9' ) {
		v = v * 10 + ( *s - '0' );
		s++;
	}

	return neg ? -v : v;
}

/* Digits in base 2 to 36 (0: 8 after "0", 16 after "0x", else 10) */
unsigned long strtoul( const char *s, char **end, int base )
{
	unsigned long	v = 0;
	int		neg = 0, d, any = 0;
	const char	*p = s;

	while ( *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ) p++;
	if ( *p == '-' ) { neg = 1; p++; }
	else if ( *p == '+' ) { p++; }
	if ( ( base == 0 || base == 16 ) && p[0] == '0' && ( p[1] == 'x' || p[1] == 'X' ) ) {
		p += 2;
		base = 16;
	} else if ( base == 0 ) {
		base = ( p[0] == '0' ) ? 8 : 10;
	}
	for ( ;; p++ ) {
		if ( *p >= '0' && *p <= '9' ) d = *p - '0';
		else if ( *p >= 'a' && *p <= 'z' ) d = *p - 'a' + 10;
		else if ( *p >= 'A' && *p <= 'Z' ) d = *p - 'A' + 10;
		else break;
		if ( d >= base ) break;
		v = v * (unsigned long)base + (unsigned long)d;
		any = 1;
	}
	if ( end != NULL ) *end = (char *)( any ? p : s );

	return neg ? -v : v;
}

long labs( long v )
{
	return ( v < 0 ) ? -v : v;
}

char *getenv( const char *name )
{
	(void)name;

	return NULL;			/* there is no environment to look in */
}

/* ---------------------------------------------------------------- formatting */

typedef struct {
	char	*s;
	size_t	n, k;			/* room, characters made */
} FMTOUT;

LOCAL void fmt_put( FMTOUT *o, char c )
{
	if ( o->k + 1 < o->n ) o->s[o->k] = c;
	o->k++;
}

/*
 * The conversions the code built into the kernel uses: %d %i %u %x %X
 * %o %c %s %p %%, with '-', '0', a width and a precision, and the
 * lengths h, l, ll and z. No floating point: the kernel does not use
 * its registers.
 */
int vsnprintf( char *s, size_t n, const char *fmt, va_list ap )
{
	FMTOUT	o;

	o.s = s;
	o.n = n;
	o.k = 0;
	while ( *fmt != 0 ) {
		char		c = *fmt++, buf[24], pad = ' ';
		int		left = 0, width = 0, prec = -1, lng = 0, neg = 0, len = 0, i, base = 10;
		unsigned long long u = 0;
		const char	*str = NULL;

		if ( c != '%' ) {
			fmt_put(&o, c);
			continue;
		}
		for ( ;; fmt++ ) {
			if ( *fmt == '-' ) left = 1;
			else if ( *fmt == '0' ) pad = '0';
			else if ( *fmt != ' ' && *fmt != '+' && *fmt != '#' ) break;
		}
		if ( *fmt == '*' ) {
			width = va_arg(ap, int);
			fmt++;
		}
		while ( *fmt >= '0' && *fmt <= '9' ) width = width * 10 + ( *fmt++ - '0' );
		if ( *fmt == '.' ) {
			prec = 0;
			fmt++;
			if ( *fmt == '*' ) {
				prec = va_arg(ap, int);
				fmt++;
			}
			while ( *fmt >= '0' && *fmt <= '9' ) prec = prec * 10 + ( *fmt++ - '0' );
		}
		for ( ;; fmt++ ) {
			if ( *fmt == 'l' ) lng++;
			else if ( *fmt == 'z' || *fmt == 'j' || *fmt == 't' ) lng = 2;
			else if ( *fmt != 'h' ) break;
		}
		c = *fmt;
		if ( c != 0 ) fmt++;
		switch ( c ) {
		case 'd':
		case 'i': {
			long long v = ( lng >= 2 ) ? va_arg(ap, long long) : ( lng == 1 ) ? va_arg(ap, long) : va_arg(ap, int);

			neg = ( v < 0 );
			u = neg ? (unsigned long long)( -( v + 1 ) ) + 1 : (unsigned long long)v;
			break;
		}
		case 'u': case 'x': case 'X': case 'o':
			u = ( lng >= 2 ) ? va_arg(ap, unsigned long long) : ( lng == 1 ) ? va_arg(ap, unsigned long)
			  : va_arg(ap, unsigned int);
			base = ( c == 'o' ) ? 8 : ( c == 'u' ) ? 10 : 16;
			break;
		case 'p':
			u = (unsigned long long)(unsigned long)va_arg(ap, void *);
			base = 16;
			fmt_put(&o, '0');
			fmt_put(&o, 'x');
			break;
		case 'c':
			buf[0] = (char)va_arg(ap, int);
			str = buf;
			len = 1;
			break;
		case 's':
			str = va_arg(ap, const char *);
			if ( str == NULL ) str = "(null)";
			for ( len = 0; str[len] != 0 && ( prec < 0 || len < prec ); len++ ) ;
			break;
		case '%':
			fmt_put(&o, '%');
			continue;
		default:
			continue;
		}
		if ( str == NULL ) {
			const char *dg = ( c == 'X' ) ? "0123456789ABCDEF" : "0123456789abcdef";

			do {
				buf[len++] = dg[u % (unsigned)base];
				u /= (unsigned)base;
			} while ( u != 0 && len < (int)sizeof(buf) );
			while ( len < prec && len < (int)sizeof(buf) ) buf[len++] = '0';
			if ( neg && pad == '0' && !left ) {
				fmt_put(&o, '-');
				width--;
				neg = 0;
			}
			if ( neg ) buf[len++] = '-';
			if ( !left ) for ( i = len; i < width; i++ ) fmt_put(&o, pad);
			for ( i = len - 1; i >= 0; i-- ) fmt_put(&o, buf[i]);
		} else {
			if ( !left ) for ( i = len; i < width; i++ ) fmt_put(&o, ' ');
			for ( i = 0; i < len; i++ ) fmt_put(&o, str[i]);
		}
		if ( left ) for ( i = len; i < width; i++ ) fmt_put(&o, ' ');
	}
	if ( n > 0 ) s[( o.k < n ) ? o.k : n - 1] = 0;

	return (int)o.k;
}

int snprintf( char *s, size_t n, const char *fmt, ... )
{
	va_list	ap;
	int	k;

	va_start(ap, fmt);
	k = vsnprintf(s, n, fmt, ap);
	va_end(ap);

	return k;
}

/* ---------------------------------------------------------------- memory */

/*
 * The size is kept in front of the block: the kernel's allocator is not
 * asked how large a block is, and realloc has to know how much to copy.
 */
#define HDR	( (SZ)sizeof(SZ) )

void *malloc( size_t n )
{
	UB	*p;

	if ( n == 0 ) {
		return NULL;
	}
	p = (UB *)Kmalloc((SZ)n + HDR);
	if ( p == NULL ) {
		return NULL;
	}
	*(SZ *)p = (SZ)n;

	return p + HDR;
}

void free( void *p )
{
	if ( p != NULL ) {
		Kfree((UB *)p - HDR);
	}
}

void *calloc( size_t n, size_t size )
{
	SZ	total = (SZ)n * (SZ)size;
	UB	*p;
	SZ	i;

	if ( n != 0 && total / (SZ)n != (SZ)size ) {
		return NULL;			/* the count times the size overflowed */
	}
	p = (UB *)malloc((size_t)total);
	if ( p == NULL ) {
		return NULL;
	}
	for ( i = 0; i < total; i++ ) {
		p[i] = 0;
	}

	return p;
}

void *realloc( void *p, size_t n )
{
	UB	*fresh;
	SZ	keep;

	if ( p == NULL ) {
		return malloc(n);
	}
	if ( n == 0 ) {
		free(p);
		return NULL;
	}
	fresh = (UB *)malloc(n);
	if ( fresh == NULL ) {
		return NULL;
	}
	keep = *(SZ *)((UB *)p - HDR);
	if ( keep > (SZ)n ) {
		keep = (SZ)n;
	}
	knl_memcpy(fresh, p, keep);
	free(p);

	return fresh;
}

/* ---------------------------------------------------------------- sorting */

LOCAL void swap_bytes( UB *a, UB *b, SZ size )
{
	SZ	i;

	for ( i = 0; i < size; i++ ) {
		UB t = a[i];

		a[i] = b[i];
		b[i] = t;
	}
}

/*
 * Insertion sort. What is sorted here is a handful of glyph indices or
 * table entries, never a large array, and a small predictable amount of
 * code is worth more in a kernel than an asymptotically better sort
 * that needs a stack of its own.
 */
void qsort( void *base, size_t n, size_t size,
	    int (*cmp)( const void *, const void * ) )
{
	UB	*a = (UB *)base;
	SZ	i, j;

	if ( a == NULL || cmp == NULL || size == 0 ) {
		return;
	}
	for ( i = 1; i < (SZ)n; i++ ) {
		for ( j = i; j > 0; j-- ) {
			UB *cur = a + j * size;
			UB *prv = cur - size;

			if ( cmp(prv, cur) <= 0 ) {
				break;
			}
			swap_bytes(prv, cur, (SZ)size);
		}
	}
}
