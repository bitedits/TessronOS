/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	hc.h (host)
 *	compose.c's stages behind plain C types (host_compose.c), so that
 *	the host test of the GPU's shaders, which is built with Mesa's
 *	headers, need not see the kernel's.
 */

#ifndef _HOSTSHIM_HC_H_
#define _HOSTSHIM_HC_H_

#include <stdint.h>

typedef struct {
	int	left, top, right, bottom;
} HC_RECT;

typedef struct {
	HC_RECT		outer, work, band;
	const uint8_t	*pixels;
	unsigned	pitch;
	unsigned	a_frame, a_work;
	int		keyed;
	int		round;
	uint32_t	wash;
	unsigned	wash_str;
	uint32_t	bar_wash;
	unsigned	bar_str;
	uint32_t	tint;
	unsigned	tint_str;
	int		blur;
	int		shd_dx, shd_dy, shd_size;
	unsigned	shd_dark;
	HC_RECT		shown;
	unsigned	fade;
} HC_WIN;

typedef struct {
	HC_RECT		r;
	unsigned	a;
	int		frame;
	uint32_t	wcol;
	unsigned	wstr;
} HC_PIECE;

#define HC_ROUND_MAX	32
#define HC_BLUR_MAX	16
#define HC_BLUR_DEPTH	3

/* compose.c's T_CPSPREAD */
typedef struct {
	int		depth;
	int		w[5];
	HC_RECT		valid[HC_BLUR_DEPTH + 1], down[HC_BLUR_DEPTH + 1], up[HC_BLUR_DEPTH + 1];
} HC_SPREAD;

void		hc_window( uint8_t *back, unsigned pitch, HC_RECT box, int rows, const HC_WIN *w );
int		hc_pieces( uint8_t *back, unsigned pitch, HC_RECT box, int rows, const HC_WIN *w, HC_PIECE *p );
int		hc_round_of( HC_RECT r, int rad );
const uint8_t	*hc_corner_cov( int rad );
int		hc_shadow_extent( const HC_WIN *w, HC_RECT *out );
int		hc_moving( const HC_WIN *w );
int		hc_spread_plan( HC_RECT box, HC_RECT r, int rad, HC_SPREAD *p );

#endif /* _HOSTSHIM_HC_H_ */
