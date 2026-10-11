/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cu_dec.h
 *	電卓: decimal numbers (design 11.17)
 *
 *	A number is a coefficient of at most CU_PREC decimal digits, an
 *	exponent of ten and a sign, so that what is written in decimal is
 *	held exactly: 0.1 + 0.2 is 0.3, not the nearest binary fraction to
 *	it. Sums, differences and products whose digits fit are exact; a
 *	result with more digits is rounded to CU_PREC digits, half away
 *	from zero. A number at or past 10^(CU_EMAX+1) is an overflow; one
 *	below 10^-CU_EMAX becomes 0.
 *
 *	The coefficient is an unsigned 128-bit integer, so that the product
 *	of two coefficients and a dividend scaled to 38 digits fit in it.
 *	Nothing here makes a system call or takes memory, and no floating
 *	point is used: the program and the kernel's test both link it.
 */

#ifndef __CU_DEC_H__
#define __CU_DEC_H__

#include <tk/typedef.h>

#define CU_PREC		19		/* significant digits a number keeps */
#define CU_EMAX		99		/* the largest exponent of a number shown */

/* What an operation answers */
#define CU_OK		0
#define CU_EOVER	( -1 )		/* too large */
#define CU_EDIV0	( -2 )		/* divided by 0 */
#define CU_EDOM		( -3 )		/* outside what the operation is defined for */
#define CU_ESYN		( -4 )		/* not a number */

typedef unsigned __int128	CUWIDE;

typedef struct {
	CUWIDE	c;			/* the coefficient, below 10^CU_PREC, no trailing zeros */
	INT	e;			/* the exponent of ten */
	BOOL	neg;			/* below 0; never set for 0 */
} CUDEC;

IMPORT void cu_zero( CUDEC *r );
IMPORT void cu_int( CUDEC *r, D v );
IMPORT BOOL cu_is_zero( CONST CUDEC *a );
IMPORT BOOL cu_is_int( CONST CUDEC *a );
IMPORT INT  cu_digits( CUWIDE c );			/* decimal digits of c, 1 for 0 */
IMPORT INT  cu_adjexp( CONST CUDEC *a );		/* the exponent of its first digit */

/* The value of c * 10^e, rounded to CU_PREC digits */
IMPORT INT  cu_make( CUDEC *r, BOOL neg, CUWIDE c, INT e );

IMPORT INT  cu_add( CUDEC *r, CONST CUDEC *a, CONST CUDEC *b );
IMPORT INT  cu_sub( CUDEC *r, CONST CUDEC *a, CONST CUDEC *b );
IMPORT INT  cu_mul( CUDEC *r, CONST CUDEC *a, CONST CUDEC *b );
IMPORT INT  cu_div( CUDEC *r, CONST CUDEC *a, CONST CUDEC *b );
IMPORT INT  cu_sqrt( CUDEC *r, CONST CUDEC *a );
IMPORT INT  cu_ipow( CUDEC *r, CONST CUDEC *a, D n );	/* a to a whole power */
IMPORT INT  cu_floor( CUDEC *r, CONST CUDEC *a );
IMPORT INT  cu_cmp( CONST CUDEC *a, CONST CUDEC *b );	/* -1, 0, 1 */
IMPORT void cu_neg( CUDEC *r, CONST CUDEC *a );

/* Rounded to n significant digits; to n digits after the point */
IMPORT INT  cu_round_sig( CUDEC *r, CONST CUDEC *a, INT n );
IMPORT INT  cu_round_at( CUDEC *r, CONST CUDEC *a, INT n );

/* A whole number of at most 18 digits: E_OK and its value, else CU_EDOM */
IMPORT INT  cu_to_int( CONST CUDEC *a, D *v );

/*
 * A number read from s: an optional sign, digits with at most one point,
 * and an exponent ("E-12"). The bytes it took in *p_used; CU_ESYN when
 * there are no digits.
 */
IMPORT INT  cu_parse( CUDEC *r, CONST char *s, INT n, INT *p_used );

/*
 * As it is shown: in CU_SHOW digit places, the digits after the point
 * rounded off and their trailing zeros dropped, the whole part grouped
 * by threes with "," when group is set; a number too large or too small
 * for the places as a mantissa of CU_SHOW_SCI digits and "E+n". The
 * length written.
 */
#define CU_SHOW		12		/* digit places of the display */
#define CU_SHOW_SCI	10		/* digits of a mantissa */
#define CU_SHOW_MIN	( -6 )		/* the smallest first digit's exponent shown without E */
IMPORT INT  cu_format( CONST CUDEC *a, char *out, INT max, BOOL group );

/* Every digit, as coefficient and exponent ("12345E-2"), for reading back elsewhere */
IMPORT INT  cu_string( CONST CUDEC *a, char *out, INT max );

#endif /* __CU_DEC_H__ */
