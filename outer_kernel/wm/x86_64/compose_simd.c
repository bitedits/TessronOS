/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	compose_simd.c (x86-64)
 *	The inner loops of the stages (compose.c), as outer_kernel/wm/
 *	compose_simd.c has them for the vector unit of AArch64, here in
 *	plain C a channel at a time: the same sums in the same 16 bit lanes,
 *	wrapping where the vector lanes wrap, rounded the same way, so that
 *	every one writes what the plain loop in compose.c writes, to the
 *	bit. The x64 PC turns the vector loops off at start
 *	(cp_simd_use(FALSE), kernel/sysdepend/x64/devinit.c) and composes
 *	with the plain loops; these are there for the test that checks the
 *	loops against each other, and for the SSE2 loops that will take
 *	their place.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include "../compose.h"

/* ---------------------------------------------------------------- copying */

EXPORT void cps_copy( UW *d, CONST UW *s, INT n )
{
	INT	i;

	for ( i = 0; i < n; i++ ) {
		d[i] = s[i];
	}
}

/* ---------------------------------------------------------------- mixing */

LOCAL UW over1( UW under, UW over, UINT a )
{
	UINT	b = 255U - a;
	UW	r, g, bl;

	r  = ( ( ( over >> 16 ) & 0xFFU ) * a + ( ( under >> 16 ) & 0xFFU ) * b + 127U ) / 255U;
	g  = ( ( ( over >> 8 ) & 0xFFU ) * a + ( ( under >> 8 ) & 0xFFU ) * b + 127U ) / 255U;
	bl = ( ( over & 0xFFU ) * a + ( under & 0xFFU ) * b + 127U ) / 255U;

	return ( r << 16 ) | ( g << 8 ) | bl;
}

EXPORT void cps_lay( UW *dst, CONST UW *src, CONST UW *und, INT n, UINT a, BOOL keyed,
		     UW tint, UINT tstr, UW wcol, UINT wstr )
{
	INT	i;

	if ( a > 255U ) {
		a = 255U;
	}
	for ( i = 0; i < n; i++ ) {
		UW	c = src[i];

		if ( keyed && ( c >> 24 ) == 0xFF ) {
			continue;
		}
		if ( tstr != 0 ) {
			c = over1(c, tint, tstr);
		}
		if ( wstr != 0 ) {
			c = over1(c, wcol, wstr);
		}
		dst[i] = ( a >= 255 ) ? c : over1(( und != NULL ) ? und[i] : dst[i], c, a);
	}
}

EXPORT void cps_darken( UW *row, CONST UB *a, INT n )
{
	INT	i;

	for ( i = 0; i < n; i++ ) {
		if ( a[i] != 0 ) {
			UW	c = row[i];
			UINT	k = 255U - a[i];

			row[i] = ( ( ( ( ( c >> 16 ) & 0xFFU ) * k + 127U ) / 255U ) << 16 )
				 | ( ( ( ( ( c >> 8 ) & 0xFFU ) * k + 127U ) / 255U ) << 8 )
				 | ( ( ( c & 0xFFU ) * k + 127U ) / 255U );
		}
	}
}

/* ---------------------------------------------------------------- the spread */

LOCAL INT within( INT v, INT n )
{
	return ( v < 0 ) ? 0 : ( v >= n ) ? n - 1 : v;
}

/* A pixel as four 16 bit lanes, blue first, the top byte left out */
LOCAL void px16( UW v, UH *l )
{
	l[0] = (UH)( v & 0xFF );
	l[1] = (UH)( ( v >> 8 ) & 0xFF );
	l[2] = (UH)( ( v >> 16 ) & 0xFF );
	l[3] = 0;
}

/*
 * Row y of a level as four lanes a pixel, from x = lo (0 or less) to hi
 * counted from its left edge, past its edges the pixel at the edge
 * again: pixel x lands at row + 4 (x - lo).
 */
LOCAL void expand( CONST T_CPLEVEL *s, INT y, INT lo, INT hi, UH *row )
{
	CONST UW	*p = s->p + (SZ)y * s->pitch;
	INT		n = s->r.right - s->r.left, x;
	UH		*e = row - (SZ)lo * 4;

	for ( x = lo; x <= hi; x++ ) {
		px16(p[within(x, n)], e + (SZ)x * 4);
	}
}

/* Four lanes, each rounded down by 2^s and cut to eight bits, as one pixel */
LOCAL UW put1( CONST UH *v, INT s )
{
	UW	o = 0;
	INT	k;

	for ( k = 0; k < 4; k++ ) {
		UH	r = (UH)( ( (UW)v[k] + ( 1U << ( s - 1 ) ) ) >> s );

		o |= (UW)( r & 0xFF ) << ( 8 * k );
	}
	return o;
}

EXPORT void cps_halve( CONST T_CPLEVEL *s, CONST T_DPRECT *d, UW *out, UH *row, UH *mid )
{
	INT	w = d->right - d->left, sh = s->r.bottom - s->r.top;
	INT	lo = 2 * d->left - 1 - s->r.left, hi = 2 * d->right - s->r.left;
	INT	y, i, k;

	/* along the rows: of four pixels 1 3 3 1 */
	for ( y = 0; y < sh; y++ ) {
		UH	*m = mid + (SZ)y * w * 4;

		expand(s, y, lo, hi, row);
		for ( i = 0; i < w; i++ ) {
			CONST UH	*q = row + (SZ)i * 8;

			for ( k = 0; k < 4; k++ ) {
				UH	v = (UH)( q[k] + q[12 + k] );

				m[(SZ)i * 4 + k] = (UH)( v + (UH)( q[4 + k] + q[8 + k] ) * 3 );
			}
		}
	}

	/* down the columns, the same, rounded to eight bits */
	for ( y = d->top; y < d->bottom; y++ ) {
		INT		t = 2 * y - 1 - s->r.top;
		CONST UH	*m0 = mid + (SZ)within(t, sh) * w * 4, *m1 = mid + (SZ)within(t + 1, sh) * w * 4;
		CONST UH	*m2 = mid + (SZ)within(t + 2, sh) * w * 4, *m3 = mid + (SZ)within(t + 3, sh) * w * 4;
		UW		*o = out + (SZ)( y - d->top ) * w;

		for ( i = 0; i < w; i++ ) {
			SZ	j = (SZ)i * 4;
			UH	v[4];

			for ( k = 0; k < 4; k++ ) {
				v[k] = (UH)( (UH)( m0[j + k] + m3[j + k] ) + (UH)( m1[j + k] + m2[j + k] ) * 3 );
			}
			o[i] = put1(v, 6);
		}
	}
}

EXPORT void cps_smooth( CONST T_CPLEVEL *s, CONST T_DPRECT *d, CONST INT *wt, UW *out,
			UH *row, UH *mid )
{
	INT		w = d->right - d->left, sh = s->r.bottom - s->r.top;
	INT		taps = ( wt[0] != 0 ) ? 2 : 1;
	INT		lo = d->left - taps - s->r.left, hi = d->right - 1 + taps - s->r.left;
	UH		a = (UH)wt[0], b = (UH)wt[1], c = (UH)wt[2];
	INT		y, i, k;

	/* along the rows: a b m b a */
	for ( y = 0; y < sh; y++ ) {
		UH		*m = mid + (SZ)y * w * 4;
		CONST UH	*q = row + (SZ)taps * 4;	/* the pixel at d->left */

		expand(s, y, lo, hi, row);
		for ( i = 0; i < w; i++ ) {
			CONST UH	*p = q + (SZ)i * 4;

			for ( k = 0; k < 4; k++ ) {
				UH	v = (UH)( p[k] * c );

				v = (UH)( v + (UH)( p[k - 4] + p[k + 4] ) * b );
				if ( taps == 2 ) {
					v = (UH)( v + (UH)( p[k - 8] + p[k + 8] ) * a );
				}
				m[(SZ)i * 4 + k] = v;
			}
		}
	}

	/* down the columns: the sums reach past sixteen bits, so in 32 */
	for ( y = d->top; y < d->bottom; y++ ) {
		INT		t = y - s->r.top;
		CONST UH	*m0 = mid + (SZ)within(t - 2, sh) * w * 4, *m1 = mid + (SZ)within(t - 1, sh) * w * 4;
		CONST UH	*m2 = mid + (SZ)within(t, sh) * w * 4;
		CONST UH	*m3 = mid + (SZ)within(t + 1, sh) * w * 4, *m4 = mid + (SZ)within(t + 2, sh) * w * 4;
		UW		*o = out + (SZ)( y - d->top ) * w;

		for ( i = 0; i < w; i++ ) {
			SZ	j = (SZ)i * 4;
			UW	px = 0;

			for ( k = 0; k < 4; k++ ) {
				UW	v = (UW)m2[j + k] * c;
				UH	r;

				v += (UW)(UH)( m1[j + k] + m3[j + k] ) * b;
				if ( taps == 2 ) {
					v += (UW)(UH)( m0[j + k] + m4[j + k] ) * a;
				}
				r = (UH)( ( v + ( 1U << 11 ) ) >> 12 );
				px |= (UW)( r & 0xFF ) << ( 8 * k );
			}
			o[i] = px;
		}
	}
}

EXPORT void cps_double( CONST T_CPLEVEL *s, CONST T_DPRECT *d, UW *out, UH *row, UH *mid )
{
	INT	w = d->right - d->left, sh = s->r.bottom - s->r.top;
	INT	lo = ( d->left >> 1 ) - 1 - s->r.left, hi = ( ( d->right - 1 ) >> 1 ) + 1 - s->r.left;
	INT	y, i, x, k;

	/*
	 * Along the rows: a pixel from the two of the level above nearest
	 * its centre, 3 and 1; an even pixel takes the one to its left as
	 * the 1, an odd one the one to its right.
	 */
	for ( y = 0; y < sh; y++ ) {
		UH	*m = mid + (SZ)y * w * 4;

		expand(s, y, lo, hi, row);
		for ( i = 0, x = d->left; i < w; i++, x++ ) {
			CONST UH	*q = row + (SZ)( ( x >> 1 ) - s->r.left - lo ) * 4;
			CONST UH	*o = ( ( x & 1 ) != 0 ) ? q + 4 : q - 4;

			for ( k = 0; k < 4; k++ ) {
				m[(SZ)i * 4 + k] = (UH)( o[k] + q[k] * 3 );
			}
		}
	}

	/* down the columns, the same */
	for ( y = d->top; y < d->bottom; y++ ) {
		INT		c = ( y >> 1 ) - s->r.top;
		CONST UH	*ma, *mb;
		UW		*o = out + (SZ)( y - d->top ) * w;
		BOOL		odd = (BOOL)( ( y & 1 ) != 0 );

		ma = mid + (SZ)within(odd ? c : c - 1, sh) * w * 4;
		mb = mid + (SZ)within(odd ? c + 1 : c, sh) * w * 4;
		for ( i = 0; i < w; i++ ) {
			SZ	j = (SZ)i * 4;
			UH	v[4];

			for ( k = 0; k < 4; k++ ) {
				v[k] = odd ? (UH)( mb[j + k] + ma[j + k] * 3 ) : (UH)( ma[j + k] + mb[j + k] * 3 );
			}
			o[i] = put1(v, 4);
		}
	}
}
