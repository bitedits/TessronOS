/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cu_dec.c
 *	電卓: decimal numbers (design 11.17)
 *
 *	See cu_dec.h. Every result goes through cu_make, which rounds the
 *	coefficient to CU_PREC digits half away from zero, drops its
 *	trailing zeros and checks the range, so that a number has one form
 *	and two equal numbers compare equal digit for digit.
 */

#include "cu_dec.h"

#define WIDE_DIGITS	38		/* digits that always fit in a CUWIDE */

/* ---------------------------------------------------------------- small things */

/* 10^n, n from 0 to 38 */
LOCAL CUWIDE p10( INT n )
{
	CUWIDE	v = 1;

	while ( n-- > 0 ) v *= 10;
	return v;
}

EXPORT INT cu_digits( CUWIDE c )
{
	INT	n = 1;
	CUWIDE	t = 10;

	while ( n < 39 && c >= t ) {
		n++;
		if ( n == 39 ) break;
		t *= 10;
	}
	return n;
}

EXPORT void cu_zero( CUDEC *r )
{
	r->c = 0;
	r->e = 0;
	r->neg = FALSE;
}

EXPORT BOOL cu_is_zero( CONST CUDEC *a )
{
	return (BOOL)( a->c == 0 );
}

EXPORT BOOL cu_is_int( CONST CUDEC *a )
{
	return (BOOL)( a->c == 0 || a->e >= 0 );
}

EXPORT INT cu_adjexp( CONST CUDEC *a )
{
	return ( a->c == 0 ) ? 0 : a->e + cu_digits(a->c) - 1;
}

EXPORT void cu_neg( CUDEC *r, CONST CUDEC *a )
{
	*r = *a;
	r->neg = ( r->c != 0 ) ? (BOOL)!a->neg : FALSE;
}

/* c / 10^k rounded half up, k from 1 to 38 */
LOCAL CUWIDE round_off( CUWIDE c, INT k )
{
	CUWIDE	d = p10(k), q = c / d, rem = c % d;

	if ( rem >= d / 2 ) q++;
	return q;
}

/* ---------------------------------------------------------------- the one form */

EXPORT INT cu_make( CUDEC *r, BOOL neg, CUWIDE c, INT e )
{
	INT	nd;

	if ( c == 0 ) {
		cu_zero(r);
		return CU_OK;
	}
	nd = cu_digits(c);
	if ( nd > CU_PREC ) {
		c = round_off(c, nd - CU_PREC);
		e += nd - CU_PREC;
		if ( c == p10(CU_PREC) ) {
			c /= 10;
			e++;
		}
	}
	while ( c % 10 == 0 ) {
		c /= 10;
		e++;
	}
	r->c = c;
	r->e = e;
	r->neg = neg;
	nd = e + cu_digits(c) - 1;
	if ( nd > CU_EMAX ) {
		return CU_EOVER;
	}
	if ( nd < -CU_EMAX ) {
		cu_zero(r);
	}
	return CU_OK;
}

EXPORT void cu_int( CUDEC *r, D v )
{
	CUWIDE	c = ( v < 0 ) ? (CUWIDE)( -( v + 1 ) ) + 1 : (CUWIDE)v;

	(void)cu_make(r, (BOOL)( v < 0 ), c, 0);
}

/* ---------------------------------------------------------------- the four operations */

/*
 * The coefficients put on one exponent and added or taken one from the
 * other. When the digits of both do not fit on the smaller exponent,
 * the larger is put at 36 digits and the smaller cut to its exponent;
 * what was cut off is kept as a 1 one place further down, so that the
 * rounding to CU_PREC digits, many places above, comes out as it would
 * for the exact sum.
 */
EXPORT INT cu_add( CUDEC *r, CONST CUDEC *a, CONST CUDEC *b )
{
	CONST CUDEC	*l, *s;
	CUWIDE		lc, sc, c;
	INT		d, nl, e;
	BOOL		neg;

	if ( a->c == 0 ) {
		*r = *b;
		return CU_OK;
	}
	if ( b->c == 0 ) {
		*r = *a;
		return CU_OK;
	}
	l = ( a->e >= b->e ) ? a : b;
	s = ( l == a ) ? b : a;
	d = l->e - s->e;
	nl = cu_digits(l->c);
	if ( nl + d <= WIDE_DIGITS - 1 ) {
		lc = l->c * p10(d);
		sc = s->c;
		e = s->e;
	} else {
		INT	sh = 36 - nl, k;
		CUWIDE	rem;

		lc = l->c * p10(sh) * 10;
		e = l->e - sh;
		k = e - s->e;
		if ( k > WIDE_DIGITS ) {
			sc = 0;
			rem = s->c;
		} else {
			sc = s->c / p10(k);
			rem = s->c % p10(k);
		}
		sc = sc * 10 + ( ( rem != 0 ) ? 1 : 0 );
		e -= 1;
	}
	if ( l->neg == s->neg ) {
		c = lc + sc;
		neg = l->neg;
	} else if ( lc >= sc ) {
		c = lc - sc;
		neg = l->neg;
	} else {
		c = sc - lc;
		neg = s->neg;
	}
	return cu_make(r, neg, c, e);
}

EXPORT INT cu_sub( CUDEC *r, CONST CUDEC *a, CONST CUDEC *b )
{
	CUDEC	nb;

	cu_neg(&nb, b);
	return cu_add(r, a, &nb);
}

EXPORT INT cu_mul( CUDEC *r, CONST CUDEC *a, CONST CUDEC *b )
{
	if ( a->c == 0 || b->c == 0 ) {
		cu_zero(r);
		return CU_OK;
	}
	return cu_make(r, (BOOL)( a->neg != b->neg ), a->c * b->c, a->e + b->e);
}

/*
 * The dividend scaled to 38 digits, so that the quotient has at least
 * CU_PREC digits; when it has just that many, the remainder says which
 * way the last digit goes.
 */
EXPORT INT cu_div( CUDEC *r, CONST CUDEC *a, CONST CUDEC *b )
{
	CUWIDE	n, q, rem;
	INT	k;

	if ( b->c == 0 ) {
		return CU_EDIV0;
	}
	if ( a->c == 0 ) {
		cu_zero(r);
		return CU_OK;
	}
	k = WIDE_DIGITS - cu_digits(a->c);
	n = a->c * p10(k);
	q = n / b->c;
	rem = n % b->c;
	if ( cu_digits(q) <= CU_PREC && rem * 2 >= b->c ) {
		q++;
	}
	return cu_make(r, (BOOL)( a->neg != b->neg ), q, a->e - k - b->e);
}

/* ---------------------------------------------------------------- roots and powers */

/* The whole part of the square root, a bit at a time */
LOCAL CUWIDE isqrt( CUWIDE n )
{
	CUWIDE	res = 0, bit = (CUWIDE)1 << 126;

	while ( bit > n ) bit >>= 2;
	while ( bit != 0 ) {
		if ( n >= res + bit ) {
			n -= res + bit;
			res = ( res >> 1 ) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}
	return res;
}

/*
 * The coefficient scaled to 37 or 38 digits on an even exponent, its
 * root then has CU_PREC digits; it is rounded up when the root is at
 * least half way to the next: n > root^2 + root.
 */
EXPORT INT cu_sqrt( CUDEC *r, CONST CUDEC *a )
{
	CUWIDE	n, q;
	INT	k;

	if ( a->c == 0 ) {
		cu_zero(r);
		return CU_OK;
	}
	if ( a->neg ) {
		return CU_EDOM;
	}
	k = WIDE_DIGITS - cu_digits(a->c);
	if ( ( ( a->e - k ) & 1 ) != 0 ) k--;
	n = a->c * p10(k);
	q = isqrt(n);
	if ( n - q * q > q ) q++;
	return cu_make(r, FALSE, q, ( a->e - k ) / 2);
}

EXPORT INT cu_ipow( CUDEC *r, CONST CUDEC *a, D n )
{
	CUDEC	res, base, one;
	UD	m = ( n < 0 ) ? (UD)( -( n + 1 ) ) + 1 : (UD)n;
	INT	er;

	cu_int(&res, 1);
	base = *a;
	if ( a->c == 0 ) {
		if ( n < 0 ) return CU_EDIV0;
		if ( n == 0 ) {
			*r = res;
			return CU_OK;
		}
		cu_zero(r);
		return CU_OK;
	}
	while ( m != 0 ) {
		if ( ( m & 1 ) != 0 ) {
			er = cu_mul(&res, &res, &base);
			if ( er < CU_OK ) {
				/* too large; its inverse would be too small to show */
				if ( n < 0 ) {
					cu_zero(r);
					return CU_OK;
				}
				return er;
			}
		}
		m >>= 1;
		if ( m != 0 ) {
			er = cu_mul(&base, &base, &base);
			if ( er < CU_OK ) {
				if ( n < 0 ) {
					cu_zero(r);
					return CU_OK;
				}
				return er;
			}
		}
	}
	if ( n < 0 ) {
		cu_int(&one, 1);
		return cu_div(r, &one, &res);
	}
	*r = res;
	return CU_OK;
}

EXPORT INT cu_floor( CUDEC *r, CONST CUDEC *a )
{
	CUWIDE	q, rem;
	INT	k;

	if ( cu_is_int(a) ) {
		*r = *a;
		return CU_OK;
	}
	k = -a->e;
	if ( k > cu_digits(a->c) ) {
		q = 0;
		rem = a->c;
	} else {
		q = a->c / p10(k);
		rem = a->c % p10(k);
	}
	if ( a->neg && rem != 0 ) q++;
	return cu_make(r, a->neg, q, 0);
}

EXPORT INT cu_cmp( CONST CUDEC *a, CONST CUDEC *b )
{
	CUDEC	d;

	if ( cu_sub(&d, a, b) < CU_OK ) {
		/* only far apart numbers overflow: the larger one decides */
		if ( a->neg != b->neg ) return a->neg ? -1 : 1;
		return ( cu_adjexp(a) > cu_adjexp(b) ) == !a->neg ? 1 : -1;
	}
	return ( d.c == 0 ) ? 0 : d.neg ? -1 : 1;
}

/* ---------------------------------------------------------------- rounding */

EXPORT INT cu_round_at( CUDEC *r, CONST CUDEC *a, INT n )
{
	INT	k;

	if ( a->c == 0 || a->e >= -n ) {
		*r = *a;
		return CU_OK;
	}
	k = -n - a->e;
	if ( k > cu_digits(a->c) ) {
		cu_zero(r);
		return CU_OK;
	}
	if ( k == cu_digits(a->c) ) {
		/* only the half way to the first place below the point is left */
		return cu_make(r, a->neg, ( a->c >= p10(k) / 2 ) ? 1 : 0, -n);
	}
	return cu_make(r, a->neg, round_off(a->c, k), -n);
}

EXPORT INT cu_round_sig( CUDEC *r, CONST CUDEC *a, INT n )
{
	INT	nd = cu_digits(a->c);

	if ( a->c == 0 || nd <= n ) {
		*r = *a;
		return CU_OK;
	}
	return cu_make(r, a->neg, round_off(a->c, nd - n), a->e + nd - n);
}

EXPORT INT cu_to_int( CONST CUDEC *a, D *v )
{
	if ( !cu_is_int(a) || cu_adjexp(a) > 17 ) {
		return CU_EDOM;
	}
	*v = (D)( a->c * p10(a->e) );
	if ( a->neg ) *v = -*v;
	return CU_OK;
}

/* ---------------------------------------------------------------- reading */

EXPORT INT cu_parse( CUDEC *r, CONST char *s, INT n, INT *p_used )
{
	CUWIDE	c = 0;
	INT	i = 0, e = 0, nd = 0, x = 0, any = 0;
	BOOL	neg = FALSE, point = FALSE;

	if ( i < n && ( s[i] == '-' || s[i] == '+' ) ) {
		neg = (BOOL)( s[i] == '-' );
		i++;
	}
	for ( ; i < n; i++ ) {
		char	ch = s[i];

		if ( ch == '.' && !point ) {
			point = TRUE;
			continue;
		}
		if ( ch < '0' || ch > '9' ) break;
		any++;
		if ( c == 0 && ch == '0' ) {
			if ( point ) e--;
			continue;
		}
		if ( nd < WIDE_DIGITS ) {
			c = c * 10 + (CUWIDE)( ch - '0' );
			nd++;
			if ( point ) e--;
		} else if ( !point ) {
			e++;			/* a digit past what is kept */
		}
	}
	if ( any == 0 ) {
		return CU_ESYN;
	}
	if ( i < n && ( s[i] == 'E' || s[i] == 'e' ) ) {
		INT	j = i + 1, xs = 1, xd = 0;

		if ( j < n && ( s[j] == '-' || s[j] == '+' ) ) {
			xs = ( s[j] == '-' ) ? -1 : 1;
			j++;
		}
		while ( j < n && s[j] >= '0' && s[j] <= '9' ) {
			if ( x < 100000 ) x = x * 10 + ( s[j] - '0' );
			j++;
			xd++;
		}
		if ( xd > 0 ) {
			e += xs * x;
			i = j;
		}
	}
	if ( p_used != NULL ) *p_used = i;
	return cu_make(r, neg, c, e);
}

/* ---------------------------------------------------------------- writing */

/* The coefficient's digits; their number */
LOCAL INT wide_str( CUWIDE c, char *b )
{
	char	t[40];
	INT	n = 0, i;

	do {
		t[n++] = (char)( '0' + (INT)( c % 10 ) );
		c /= 10;
	} while ( c != 0 );
	for ( i = 0; i < n; i++ ) b[i] = t[n - 1 - i];
	b[n] = 0;
	return n;
}

typedef struct {
	char	*p;
	INT	n, max;
} OUT;

LOCAL void put( OUT *o, char c )
{
	if ( o->n < o->max - 1 ) o->p[o->n++] = c;
	o->p[o->n] = 0;
}

LOCAL void put_int( OUT *o, INT v )
{
	char	t[12];
	INT	n = 0;

	if ( v < 0 ) {
		put(o, '-');
		v = -v;
	}
	do {
		t[n++] = (char)( '0' + v % 10 );
		v /= 10;
	} while ( v > 0 );
	while ( n > 0 ) put(o, t[--n]);
}

/* The whole part's digits, grouped by threes */
LOCAL void put_whole( OUT *o, CONST char *d, INT n, BOOL group )
{
	INT	i;

	for ( i = 0; i < n; i++ ) {
		if ( group && i > 0 && ( n - i ) % 3 == 0 ) put(o, ',');
		put(o, d[i]);
	}
}

LOCAL void put_fixed( OUT *o, CONST CUDEC *t, BOOL group )
{
	char	d[48];
	INT	nd = wide_str(t->c, d), p, i;

	if ( t->neg ) put(o, '-');
	if ( t->e >= 0 ) {
		for ( i = 0; i < t->e && nd < 46; i++ ) d[nd++] = '0';
		put_whole(o, d, nd, group);
		return;
	}
	p = nd + t->e;
	if ( p <= 0 ) {
		put(o, '0');
		put(o, '.');
		for ( i = 0; i < -p; i++ ) put(o, '0');
		for ( i = 0; i < nd; i++ ) put(o, d[i]);
		return;
	}
	put_whole(o, d, p, group);
	put(o, '.');
	for ( i = p; i < nd; i++ ) put(o, d[i]);
}

LOCAL void put_sci( OUT *o, CONST CUDEC *a )
{
	CUDEC	t;
	char	d[48];
	INT	nd, i, x;

	if ( cu_round_sig(&t, a, CU_SHOW_SCI) < CU_OK ) {
		t = *a;
		t.c = a->c;
	}
	nd = wide_str(t.c, d);
	if ( nd > CU_SHOW_SCI ) nd = CU_SHOW_SCI;
	x = cu_adjexp(&t);
	if ( t.neg ) put(o, '-');
	put(o, d[0]);
	if ( nd > 1 ) {
		put(o, '.');
		for ( i = 1; i < nd; i++ ) put(o, d[i]);
	}
	put(o, 'E');
	if ( x >= 0 ) put(o, '+');
	put_int(o, x);
}

EXPORT INT cu_format( CONST CUDEC *a, char *out, INT max, BOOL group )
{
	OUT	o;
	CUDEC	t;
	INT	x = cu_adjexp(a);

	o.p = out;
	o.n = 0;
	o.max = max;
	out[0] = 0;
	if ( a->c == 0 ) {
		put(&o, '0');
		return o.n;
	}
	if ( x < CU_SHOW && x >= CU_SHOW_MIN ) {
		INT	fd = CU_SHOW - ( ( x >= 0 ) ? x + 1 : 1 );

		if ( cu_round_at(&t, a, fd) >= CU_OK && t.c != 0 && cu_adjexp(&t) < CU_SHOW ) {
			put_fixed(&o, &t, group);
			return o.n;
		}
	}
	put_sci(&o, a);
	return o.n;
}

EXPORT INT cu_string( CONST CUDEC *a, char *out, INT max )
{
	OUT	o;
	char	d[48];
	INT	i, nd = wide_str(a->c, d);

	o.p = out;
	o.n = 0;
	o.max = max;
	out[0] = 0;
	if ( a->neg ) put(&o, '-');
	for ( i = 0; i < nd; i++ ) put(&o, d[i]);
	if ( a->e != 0 ) {
		put(&o, 'E');
		put_int(&o, a->e);
	}
	return o.n;
}
