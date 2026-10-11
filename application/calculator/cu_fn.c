/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cu_fn.c
 *	電卓: the functions worked out in double precision (design 11.17)
 *
 *	The trigonometric functions and their inverses, the logarithms, the
 *	exponentials and a power to a fraction. The decimal number is read
 *	as a double from its digits (strtod rounds it to the nearest), the
 *	function taken with libm, and the answer written back with 15
 *	significant digits, those a double holds for certain, so that
 *	sin 30° is 0.5 and not 0.49999999999999994. A sine, cosine or
 *	tangent below 10^-15 is 0: sin π is then 0, not the 1.2E-16 the
 *	nearest double to π gives.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "cu_eng.h"

#define TRUST_DIGITS	15
#define TINY		1e-15

LOCAL double to_double( CONST CUDEC *a )
{
	char	s[64];

	(void)cu_string(a, s, sizeof(s));
	return strtod(s, NULL);
}

LOCAL INT from_double( CUDEC *r, double v )
{
	char	s[48];

	if ( isnan(v) ) return CU_EDOM;
	if ( isinf(v) ) return CU_EOVER;
	snprintf(s, sizeof(s), "%.*e", TRUST_DIGITS - 1, v);
	return cu_parse(r, s, (INT)strlen(s), NULL);
}

EXPORT INT cu_fn( INT key, CUDEC *x, CONST CUDEC *y, BOOL deg )
{
	CONST double	rad = M_PI / 180.0;
	double		v = to_double(x), r;
	BOOL		trig = FALSE;

	switch ( key ) {
	case CU_K_SIN:	r = sin(deg ? v * rad : v);	trig = TRUE;	break;
	case CU_K_COS:	r = cos(deg ? v * rad : v);	trig = TRUE;	break;
	case CU_K_TAN:	r = tan(deg ? v * rad : v);	trig = TRUE;	break;
	case CU_K_ASIN:	r = asin(v);	if ( deg ) r /= rad;		break;
	case CU_K_ACOS:	r = acos(v);	if ( deg ) r /= rad;		break;
	case CU_K_ATAN:	r = atan(v);	if ( deg ) r /= rad;		break;
	case CU_K_LOG:	r = log10(v);					break;
	case CU_K_LN:	r = log(v);					break;
	case CU_K_EXP10: r = pow(10.0, v);				break;
	case CU_K_EXPE:	r = exp(v);					break;
	case CU_K_POW:
		if ( y == NULL ) return CU_EDOM;
		r = pow(v, to_double(y));
		break;
	default:
		return CU_EDOM;
	}
	if ( trig && fabs(r) < TINY ) r = 0.0;
	if ( trig && fabs(r) > 1e15 ) return CU_EDOM;	/* the tangent of a right angle in radians */
	return from_double(x, r);
}
