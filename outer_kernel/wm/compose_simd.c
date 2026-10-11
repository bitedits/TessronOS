/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	compose_simd.c
 *	The inner loops of the stages (compose.c) on the processor's vector
 *	unit (design 16.5.5)
 *
 *	The kernel is built without the FP/SIMD registers, and a task's copy
 *	of them is kept across a switch only when the task was made with
 *	TA_FPU. This file alone is built with them, and compose.c calls it
 *	only from a task that has said it owns them (cp_simd_allow): the
 *	compositor of wm.c. Called from any other task the registers would
 *	not be kept, so nothing here is reached from anywhere else.
 *
 *	Every loop writes what the plain loop in compose.c writes, to the
 *	bit. A channel mixed at a of 255 is (o a + u (255 - a) + 127) / 255,
 *	and the division by 255 is (v + 1 + (v >> 8)) >> 8, which is exact
 *	for every v below 65535; the largest v a mix makes is 255 * 255 +
 *	127. The sums of the spread are the ones compose.c keeps in lanes of
 *	a 64 bit word, here a channel to a 16 bit lane, and are rounded the
 *	same way: (v + 2^(s-1)) >> s, cut to eight bits.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <arm_neon.h>
#include "compose.h"

/* ---------------------------------------------------------------- copying */

EXPORT void cps_copy( UW *d, CONST UW *s, INT n )
{
	INT	i = 0;

	for ( ; i + 16 <= n; i += 16 ) {
		uint32x4_t	a = vld1q_u32(s + i), b = vld1q_u32(s + i + 4);
		uint32x4_t	c = vld1q_u32(s + i + 8), e = vld1q_u32(s + i + 12);

		vst1q_u32(d + i, a);
		vst1q_u32(d + i + 4, b);
		vst1q_u32(d + i + 8, c);
		vst1q_u32(d + i + 12, e);
	}
	for ( ; i + 4 <= n; i += 4 ) {
		vst1q_u32(d + i, vld1q_u32(s + i));
	}
	for ( ; i < n; i++ ) {
		d[i] = s[i];
	}
}

/* ---------------------------------------------------------------- mixing */

/* Each 16 bit lane v divided by 255, rounded down, as eight bits (v < 65535) */
static inline uint8x8_t div255( uint16x8_t v )
{
	return vaddhn_u16(v, vsraq_n_u16(vdupq_n_u16(1), v, 8));
}

/*
 * Four pixels of 'over' laid at a over four of 'under' (b = 255 - a),
 * channel by channel, rounded; the top byte comes out 0, as over_of
 * leaves it.
 */
static inline uint32x4_t over4( uint32x4_t under, uint32x4_t over, uint8x8_t a, uint8x8_t b )
{
	uint8x16_t	u = vreinterpretq_u8_u32(under), o = vreinterpretq_u8_u32(over);
	uint16x8_t	lo = vmlal_u8(vmull_u8(vget_low_u8(o), a), vget_low_u8(u), b);
	uint16x8_t	hi = vmlal_u8(vmull_u8(vget_high_u8(o), a), vget_high_u8(u), b);
	uint16x8_t	r = vdupq_n_u16(127);

	lo = vaddq_u16(lo, r);
	hi = vaddq_u16(hi, r);
	return vandq_u32(vreinterpretq_u32_u8(vcombine_u8(div255(lo), div255(hi))),
			 vdupq_n_u32(0x00FFFFFFU));
}

/* The same for one pixel, for what is left over at the end of a row */
LOCAL UW over1( UW under, UW over, UINT a )
{
	UINT	b = 255U - a;
	UW	r, g, bl;

	r  = ( ( ( over >> 16 ) & 0xFFU ) * a + ( ( under >> 16 ) & 0xFFU ) * b + 127U ) / 255U;
	g  = ( ( ( over >> 8 ) & 0xFFU ) * a + ( ( under >> 8 ) & 0xFFU ) * b + 127U ) / 255U;
	bl = ( ( over & 0xFFU ) * a + ( under & 0xFFU ) * b + 127U ) / 255U;

	return ( r << 16 ) | ( g << 8 ) | bl;
}

/*
 * A run of n pixels of a window laid where nothing but its own solidity
 * a varies across it (no outline of its own, no round corner): each
 * pixel tinted by tstr, washed by wstr, and mixed at a over what shows
 * through (und, or what is there when it is NULL); with 'keyed' a pixel
 * with 0xFF on top is not laid.
 */
EXPORT void cps_lay( UW *dst, CONST UW *src, CONST UW *und, INT n, UINT a, BOOL keyed,
		     UW tint, UINT tstr, UW wcol, UINT wstr )
{
	uint32x4_t	tv = vdupq_n_u32(tint), wv = vdupq_n_u32(wcol);
	uint32x4_t	ff = vdupq_n_u32(0xFFU);
	uint8x8_t	ta = vdup_n_u8((UB)tstr), tb = vdup_n_u8((UB)( 255U - tstr ));
	uint8x8_t	wa = vdup_n_u8((UB)wstr), wb = vdup_n_u8((UB)( 255U - wstr ));
	uint8x8_t	aa = vdup_n_u8((UB)( ( a > 255U ) ? 255U : a ));
	uint8x8_t	ab = vdup_n_u8((UB)( 255U - ( ( a > 255U ) ? 255U : a ) ));
	INT		i = 0;

	for ( ; i + 4 <= n; i += 4 ) {
		uint32x4_t	c = vld1q_u32(src + i), was = vld1q_u32(dst + i), out;

		if ( tstr != 0 ) {
			c = over4(c, tv, ta, tb);
		}
		if ( wstr != 0 ) {
			c = over4(c, wv, wa, wb);
		}
		if ( a >= 255 ) {
			out = c;
		} else {
			out = over4(( und != NULL ) ? vld1q_u32(und + i) : was, c, aa, ab);
		}
		if ( keyed ) {
			/* see-through where the source had 0xFF on top */
			uint32x4_t	m = vceqq_u32(vshrq_n_u32(vld1q_u32(src + i), 24), ff);

			out = vbslq_u32(m, was, out);
		}
		vst1q_u32(dst + i, out);
	}
	for ( ; i < n; i++ ) {
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

/*
 * A run of n pixels taken toward black, each by its own a of 255 (a 0
 * leaves the pixel as it is, top byte and all; any other clears it).
 */
EXPORT void cps_darken( UW *row, CONST UB *a, INT n )
{
	static CONST UB	spread_idx[16] = { 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3 };
	uint8x16_t	idx = vld1q_u8(spread_idx);
	uint16x8_t	r = vdupq_n_u16(127);
	INT		i = 0;

	for ( ; i + 4 <= n; i += 4 ) {
		uint32x4_t	c = vld1q_u32(row + i), out, keep;
		uint8x16_t	av = vqtbl1q_u8(vcombine_u8(vreinterpret_u8_u32(vdup_n_u32(
					(UW)a[i] | ( (UW)a[i + 1] << 8 ) | ( (UW)a[i + 2] << 16 )
					| ( (UW)a[i + 3] << 24 ))), vdup_n_u8(0)), idx);
		uint8x16_t	k = vmvnq_u8(av);		/* 255 - a, byte by byte */
		uint8x16_t	cb = vreinterpretq_u8_u32(c);
		uint16x8_t	lo = vaddq_u16(vmull_u8(vget_low_u8(cb), vget_low_u8(k)), r);
		uint16x8_t	hi = vaddq_u16(vmull_u8(vget_high_u8(cb), vget_high_u8(k)), r);

		out = vandq_u32(vreinterpretq_u32_u8(vcombine_u8(div255(lo), div255(hi))),
				vdupq_n_u32(0x00FFFFFFU));
		keep = vceqq_u32(vreinterpretq_u32_u8(av), vdupq_n_u32(0));
		vst1q_u32(row + i, vbslq_u32(keep, c, out));
	}
	for ( ; i < n; i++ ) {
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
static inline uint16x4_t px16( UW v )
{
	return vget_low_u16(vmovl_u8(vreinterpret_u8_u32(vdup_n_u32(v & 0x00FFFFFFU))));
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
	uint16x4_t	first = px16(p[0]), last = px16(p[n - 1]);
	UH		*e = row - (SZ)lo * 4;
	uint32x2_t	mask = vdup_n_u32(0x00FFFFFFU);

	for ( x = lo; x < 0; x++ ) {
		vst1_u16(e + (SZ)x * 4, first);
	}
	for ( x = 0; x + 2 <= n; x += 2 ) {
		uint8x8_t	v = vreinterpret_u8_u32(vand_u32(vld1_u32(p + x), mask));

		vst1q_u16(e + (SZ)x * 4, vmovl_u8(v));
	}
	for ( ; x < n; x++ ) {
		vst1_u16(e + (SZ)x * 4, px16(p[x]));
	}
	for ( x = n; x <= hi; x++ ) {
		vst1_u16(e + (SZ)x * 4, last);
	}
}

/* Two pixels' sums rounded down by 2^s into two pixels (s a constant) */
#define PUT2(o, v, s)	vst1_u8((UB *)(o), vmovn_u16(vrshrq_n_u16((v), (s))))

/* And one */
#define PUT1(o, v, s)	vst1_lane_u32((o), vreinterpret_u32_u8(vmovn_u16(vrshrq_n_u16( \
				vcombine_u16((v), vdup_n_u16(0)), (s)))), 0)

EXPORT void cps_halve( CONST T_CPLEVEL *s, CONST T_DPRECT *d, UW *out, UH *row, UH *mid )
{
	INT	w = d->right - d->left, sh = s->r.bottom - s->r.top;
	INT	lo = 2 * d->left - 1 - s->r.left, hi = 2 * d->right - s->r.left;
	INT	y, i;

	/* along the rows: of four pixels 1 3 3 1, two at a time */
	for ( y = 0; y < sh; y++ ) {
		UH	*m = mid + (SZ)y * w * 4;

		expand(s, y, lo, hi, row);
		for ( i = 0; i + 2 <= w; i += 2 ) {
			uint64x2x2_t	a = vld2q_u64((const uint64_t *)( row + (SZ)i * 8 ));
			uint64x2x2_t	b = vld2q_u64((const uint64_t *)( row + (SZ)i * 8 + 8 ));
			uint16x8_t	v = vaddq_u16(vreinterpretq_u16_u64(a.val[0]), vreinterpretq_u16_u64(b.val[1]));

			v = vmlaq_n_u16(v, vaddq_u16(vreinterpretq_u16_u64(a.val[1]),
						     vreinterpretq_u16_u64(b.val[0])), 3);
			vst1q_u16(m + (SZ)i * 4, v);
		}
		for ( ; i < w; i++ ) {
			CONST UH	*q = row + (SZ)i * 8;
			uint16x4_t	v = vadd_u16(vld1_u16(q), vld1_u16(q + 12));

			v = vmla_n_u16(v, vadd_u16(vld1_u16(q + 4), vld1_u16(q + 8)), 3);
			vst1_u16(m + (SZ)i * 4, v);
		}
	}

	/* down the columns, the same, rounded to eight bits */
	for ( y = d->top; y < d->bottom; y++ ) {
		INT		t = 2 * y - 1 - s->r.top;
		CONST UH	*m0 = mid + (SZ)within(t, sh) * w * 4, *m1 = mid + (SZ)within(t + 1, sh) * w * 4;
		CONST UH	*m2 = mid + (SZ)within(t + 2, sh) * w * 4, *m3 = mid + (SZ)within(t + 3, sh) * w * 4;
		UW		*o = out + (SZ)( y - d->top ) * w;

		for ( i = 0; i + 2 <= w; i += 2 ) {
			SZ		k = (SZ)i * 4;
			uint16x8_t	v = vaddq_u16(vld1q_u16(m0 + k), vld1q_u16(m3 + k));

			v = vmlaq_n_u16(v, vaddq_u16(vld1q_u16(m1 + k), vld1q_u16(m2 + k)), 3);
			PUT2(o + i, v, 6);
		}
		for ( ; i < w; i++ ) {
			SZ		k = (SZ)i * 4;
			uint16x4_t	v = vadd_u16(vld1_u16(m0 + k), vld1_u16(m3 + k));

			v = vmla_n_u16(v, vadd_u16(vld1_u16(m1 + k), vld1_u16(m2 + k)), 3);
			PUT1(o + i, v, 6);
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
	INT		y, i;

	/* along the rows: a b m b a, two pixels at a time */
	for ( y = 0; y < sh; y++ ) {
		UH		*m = mid + (SZ)y * w * 4;
		CONST UH	*q = row + (SZ)taps * 4;	/* the pixel at d->left */

		expand(s, y, lo, hi, row);
		for ( i = 0; i + 2 <= w; i += 2 ) {
			CONST UH	*p = q + (SZ)i * 4;
			uint16x8_t	v = vmulq_n_u16(vld1q_u16(p), c);

			v = vmlaq_n_u16(v, vaddq_u16(vld1q_u16(p - 4), vld1q_u16(p + 4)), b);
			if ( taps == 2 ) {
				v = vmlaq_n_u16(v, vaddq_u16(vld1q_u16(p - 8), vld1q_u16(p + 8)), a);
			}
			vst1q_u16(m + (SZ)i * 4, v);
		}
		for ( ; i < w; i++ ) {
			CONST UH	*p = q + (SZ)i * 4;
			uint16x4_t	v = vmul_n_u16(vld1_u16(p), c);

			v = vmla_n_u16(v, vadd_u16(vld1_u16(p - 4), vld1_u16(p + 4)), b);
			if ( taps == 2 ) {
				v = vmla_n_u16(v, vadd_u16(vld1_u16(p - 8), vld1_u16(p + 8)), a);
			}
			vst1_u16(m + (SZ)i * 4, v);
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
			SZ		k = (SZ)i * 4;
			uint32x4_t	v = vmull_n_u16(vld1_u16(m2 + k), c);
			uint16x4_t	r;

			v = vmlal_n_u16(v, vadd_u16(vld1_u16(m1 + k), vld1_u16(m3 + k)), b);
			if ( taps == 2 ) {
				v = vmlal_n_u16(v, vadd_u16(vld1_u16(m0 + k), vld1_u16(m4 + k)), a);
			}
			r = vrshrn_n_u32(v, 12);
			vst1_lane_u32(o + i, vreinterpret_u32_u8(vmovn_u16(vcombine_u16(r, vdup_n_u16(0)))), 0);
		}
	}
}

EXPORT void cps_double( CONST T_CPLEVEL *s, CONST T_DPRECT *d, UW *out, UH *row, UH *mid )
{
	INT	w = d->right - d->left, sh = s->r.bottom - s->r.top;
	INT	lo = ( d->left >> 1 ) - 1 - s->r.left, hi = ( ( d->right - 1 ) >> 1 ) + 1 - s->r.left;
	INT	y, i, x;

	/*
	 * Along the rows: a pixel from the two of the level above nearest
	 * its centre, 3 and 1; an even pixel and the odd one after it read
	 * the same three.
	 */
	for ( y = 0; y < sh; y++ ) {
		UH	*m = mid + (SZ)y * w * 4;

		expand(s, y, lo, hi, row);
		i = 0;
		x = d->left;
		if ( ( x & 1 ) != 0 && i < w ) {
			CONST UH	*q = row + (SZ)( ( x >> 1 ) - s->r.left - lo ) * 4;

			vst1_u16(m, vmla_n_u16(vld1_u16(q + 4), vld1_u16(q), 3));
			i++;
			x++;
		}
		for ( ; i + 2 <= w; i += 2, x += 2 ) {
			CONST UH	*q = row + (SZ)( ( x >> 1 ) - s->r.left - lo ) * 4;
			uint16x4_t	c = vld1_u16(q);
			uint16x8_t	v = vcombine_u16(vld1_u16(q - 4), vld1_u16(q + 4));

			vst1q_u16(m + (SZ)i * 4, vmlaq_n_u16(v, vcombine_u16(c, c), 3));
		}
		if ( i < w ) {
			CONST UH	*q = row + (SZ)( ( x >> 1 ) - s->r.left - lo ) * 4;

			vst1_u16(m + (SZ)i * 4, vmla_n_u16(vld1_u16(q - 4), vld1_u16(q), 3));
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
		for ( i = 0; i + 2 <= w; i += 2 ) {
			SZ		k = (SZ)i * 4;
			uint16x8_t	v = odd ? vmlaq_n_u16(vld1q_u16(mb + k), vld1q_u16(ma + k), 3)
					    : vmlaq_n_u16(vld1q_u16(ma + k), vld1q_u16(mb + k), 3);

			PUT2(o + i, v, 4);
		}
		for ( ; i < w; i++ ) {
			SZ		k = (SZ)i * 4;
			uint16x4_t	v = odd ? vmla_n_u16(vld1_u16(mb + k), vld1_u16(ma + k), 3)
					    : vmla_n_u16(vld1_u16(ma + k), vld1_u16(mb + k), 3);

			PUT1(o + i, v, 4);
		}
	}
}
