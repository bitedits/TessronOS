/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	v3d_cp_split.h
 *	Where each stage of a window is drawn, and by which shader
 *	(docs/tessronos/13-gpu.md 13.10.5).
 *
 *	A fragment shader's cost is paid for every pixel of the rectangle
 *	it is drawn over, so the rectangles are cut to where the work is:
 *
 *	  - The shadow is not worked out where the window stands, which
 *	    covers it, but in the window's round corners, which cover their
 *	    pixels only in part. Up to eight rectangles round the window.
 *	  - A piece that is solid, not tinted and not washed is the
 *	    window's own pixels as they are, laid by 'copy', which reads
 *	    one word and writes it; only its round corners need 'piece'.
 *	    Up to seven rectangles.
 *
 *	The pixels each rectangle covers are those the stage of compose.c
 *	works on, none twice, so the screen is the same.
 *
 *	Then, once a job's draws are all there, what shows of each and how
 *	each supertile of the job is rendered (cp_cull and after): where a
 *	later copy writes over a draw it is not drawn, a supertile with no
 *	draw is left out or its ground copied by the processor, and one a
 *	window's copy writes all of is loaded from that window's pixels.
 *
 *	And where the levels of a spread are kept while it is worked out
 *	(cp_lv_row, cp_spread_bytes).
 *
 *	The kernel (v3d_compose.c) and the host test of the shaders
 *	(tools/mesa/test_cp_shaders.c) both lay windows with these, after
 *	defining UB, INT, UINT and UD and including v3d_cp_shaders.h.
 */

#ifndef _V3D_CP_SPLIT_H_
#define _V3D_CP_SPLIT_H_

/* A rectangle of the screen: left and top in, right and bottom out */
typedef struct {
	INT	l, t, r, b;
} CP_SR;

#define CP_SPLIT_SHADOW_MAX	8
#define CP_SPLIT_PIECE_MAX	7

/*
 * The levels of a spread (compose.c cp_spread_plan) for a back buffer
 * of 'rows' rows of 'pitch' bytes, each kept with that pitch, its pixel
 * (x, y) at y rows and 4 x bytes from its first: levels 1, 2, 3 one
 * under another in T1, from the row cp_lv_row gives; the deepest level
 * smoothed in T2 under the screen's rows; the spread itself in T2 where
 * the screen's pixels are. Level k has rows / 2^k rows, rounded up.
 * T1 and T2 are cp_spread_bytes each.
 */
LOCAL INT cp_lv_row( INT rows, INT k )
{
	INT	q, r = 0;

	for ( q = 1; q < k; q++ ) {
		r += ( ( rows - 1 ) >> q ) + 1;
	}
	return r;
}

LOCAL UD cp_spread_bytes( UINT pitch, INT rows )
{
	return (UD)pitch * (UD)( rows + ( ( rows - 1 ) >> 1 ) + 1 );
}

LOCAL INT cp_split_put( CP_SR *out, INT n, INT l, INT t, INT r, INT b, CONST CP_SR *in )
{
	if ( l < in->l ) l = in->l;
	if ( t < in->t ) t = in->t;
	if ( r > in->r ) r = in->r;
	if ( b > in->b ) b = in->b;
	if ( l >= r || t >= b ) {
		return n;
	}
	out[n].l = l;
	out[n].t = t;
	out[n].r = r;
	out[n].b = b;
	return n + 1;
}

/*
 * The rectangles of the shadow's extent e that are worked out, for a
 * window standing at o with corners of radius rad (0: square): the
 * rows above and below it, either side of it in its rows, and its
 * corners. How many, up to CP_SPLIT_SHADOW_MAX.
 */
LOCAL INT cp_split_shadow( CONST CP_SR *e, CONST CP_SR *o, INT rad, CP_SR *out )
{
	INT	n = 0;

	n = cp_split_put(out, n, e->l, e->t, e->r, o->t, e);
	n = cp_split_put(out, n, e->l, o->b, e->r, e->b, e);
	n = cp_split_put(out, n, e->l, o->t, o->l, o->b, e);
	n = cp_split_put(out, n, o->r, o->t, e->r, o->b, e);
	if ( rad > 0 ) {
		n = cp_split_put(out, n, o->l, o->t, o->l + rad, o->t + rad, e);
		n = cp_split_put(out, n, o->r - rad, o->t, o->r, o->t + rad, e);
		n = cp_split_put(out, n, o->l, o->b - rad, o->l + rad, o->b, e);
		n = cp_split_put(out, n, o->r - rad, o->b - rad, o->r, o->b, e);
	}
	return n;
}

/*
 * A piece p of a window standing at o with corners of radius rad, as
 * rectangles and the shader each is drawn with (fs[], CP_FS_COPY or
 * CP_FS_PIECE). 'plain' when the piece is solid, not tinted and not
 * washed. How many, up to CP_SPLIT_PIECE_MAX.
 */
LOCAL INT cp_split_piece( CONST CP_SR *p, INT plain, CONST CP_SR *o, INT rad, CP_SR *out, INT *fs )
{
	INT	n = 0, k, m, top, bot;

	if ( !plain ) {
		out[0] = *p;
		fs[0] = CP_FS_PIECE;
		return 1;
	}
	if ( rad <= 0 ) {
		out[0] = *p;
		fs[0] = CP_FS_COPY;
		return 1;
	}
	top = o->t + rad;
	bot = o->b - rad;

	/* the rows clear of the corners */
	m = n;
	n = cp_split_put(out, n, p->l, top, p->r, bot, p);
	for ( k = m; k < n; k++ ) fs[k] = CP_FS_COPY;

	/* the rows of the corners: between them copied, the corners worked out */
	m = n;
	n = cp_split_put(out, n, o->l + rad, o->t, o->r - rad, top, p);
	n = cp_split_put(out, n, o->l + rad, bot, o->r - rad, o->b, p);
	for ( k = m; k < n; k++ ) fs[k] = CP_FS_COPY;
	m = n;
	n = cp_split_put(out, n, o->l, o->t, o->l + rad, top, p);
	n = cp_split_put(out, n, o->r - rad, o->t, o->r, top, p);
	n = cp_split_put(out, n, o->l, bot, o->l + rad, o->b, p);
	n = cp_split_put(out, n, o->r - rad, bot, o->r, o->b, p);
	for ( k = m; k < n; k++ ) fs[k] = CP_FS_PIECE;
	return n;
}

/* ---------------------------------------------------------------- what shows of the draws */

/*
 * The draws of one job, as rectangles of its frame, cut to what shows
 * of them and set against its supertiles (13.10.5):
 *
 *   - A copy writes every pixel of its rectangle without reading what
 *     is behind it, so whatever a draw before it wrote there is lost.
 *     Each draw is cut to what no later copy writes over (cp_cull).
 *   - A supertile no draw meets has nothing to do: a job that loads
 *     from the back buffer and stores to it leaves it out, and in the
 *     first job of a part the processor may copy the ground into it
 *     while the GPU works (cp_share).
 *   - A supertile all of which one copy writes, with nothing drawn in
 *     it before that copy, is loaded from the window's pixels instead,
 *     and the copy is left out of it (cp_classify, cp_cut_bases).
 */

#define CP_CUT_MAX	16		/* rectangles one draw is cut into at most */

/* A supertile's class in a job */
#define CP_ST_SKIP	0		/* not rendered */
#define CP_ST_FREE	1		/* no draw meets it (decided afterwards) */
#define CP_ST_LOAD	2		/* loaded from what the job loads, drawn, stored */
#define CP_ST_CPU	3		/* the processor copies the ground into it */
#define CP_ST_WIN	4		/* loaded from its base copy's pixels, drawn, stored */

typedef struct {
	CP_SR	r;		/* the rectangle it is drawn over, in the frame */
	INT	copy;		/* writes every pixel of it, reading nothing behind */
	INT	wok;		/* and those are a window's pixels, loadable into a tile */
	INT	w;		/* what a pixel of it costs (CP_CW_*) */
	INT	s0, ns;		/* what shows of it: the pool's s0..s0+ns-1 */
} CP_DR;

/* The supertiles of a job's frame */
typedef struct {
	INT	fx, fy, fw, fh;	/* the frame on the screen */
	INT	tile;		/* a tile's pixels each way */
	INT	tx, ty;		/* tiles across and down */
	INT	sw, sh;		/* a supertile's tiles across and down */
	INT	nx, ny;		/* supertiles across and down */
} CP_GRID;

LOCAL INT cp_sr_meet( CONST CP_SR *a, CONST CP_SR *b )
{
	return ( a->l < b->r && b->l < a->r && a->t < b->b && b->t < a->b );
}

LOCAL INT cp_sr_holds( CONST CP_SR *a, CONST CP_SR *b )
{
	return ( a->l <= b->l && a->t <= b->t && a->r >= b->r && a->b >= b->b );
}

/*
 * The rectangles v[0..n-1] with c taken out of them, in place: how many
 * are left, or -1 when that would be more than max (up to CP_CUT_MAX),
 * v then as it was.
 */
LOCAL INT cp_sr_cut( CP_SR *v, INT n, CONST CP_SR *c, INT max )
{
	CP_SR	o[CP_CUT_MAX];
	INT	i, m = 0;

	for ( i = 0; i < n; i++ ) {
		CP_SR	a = v[i];
		INT	t, b;

		if ( !cp_sr_meet(&a, c) ) {
			if ( m >= max ) return -1;
			o[m++] = a;
			continue;
		}
		t = ( c->t > a.t ) ? c->t : a.t;
		b = ( c->b < a.b ) ? c->b : a.b;
		/* above it, below it, and either side of it in the rows they share */
		if ( a.t < c->t ) {
			if ( m >= max ) return -1;
			o[m].l = a.l;  o[m].t = a.t;  o[m].r = a.r;  o[m].b = c->t;  m++;
		}
		if ( c->b < a.b ) {
			if ( m >= max ) return -1;
			o[m].l = a.l;  o[m].t = c->b;  o[m].r = a.r;  o[m].b = a.b;  m++;
		}
		if ( a.l < c->l ) {
			if ( m >= max ) return -1;
			o[m].l = a.l;  o[m].t = t;  o[m].r = c->l;  o[m].b = b;  m++;
		}
		if ( c->r < a.r ) {
			if ( m >= max ) return -1;
			o[m].l = c->r;  o[m].t = t;  o[m].r = a.r;  o[m].b = b;  m++;
		}
	}
	for ( i = 0; i < m; i++ ) v[i] = o[i];
	return m;
}

/*
 * What shows of each draw d[0..nd-1], into the pool from 'at': with
 * 'cull', what no later copy writes over, else the whole rectangle. A
 * draw that would be cut into more than CP_CUT_MAX is kept whole, and
 * so is each draw once the pool keeps only one rectangle for each of
 * the rest. The pool's use after it, or -1 when it does not hold even
 * one rectangle a draw.
 */
LOCAL INT cp_cull( CP_DR *d, INT nd, CP_SR *pool, INT at, INT max, INT cull )
{
	CP_SR	v[CP_CUT_MAX];
	INT	i, k, n, m;

	if ( at + nd > max ) {
		return -1;
	}
	for ( i = 0; i < nd; i++ ) {
		v[0] = d[i].r;
		n = 1;
		for ( k = i + 1; cull && k < nd && n > 0; k++ ) {
			if ( !d[k].copy || !cp_sr_meet(&d[i].r, &d[k].r) ) continue;
			m = cp_sr_cut(v, n, &d[k].r, CP_CUT_MAX);
			if ( m < 0 ) {
				v[0] = d[i].r;
				n = 1;
				break;
			}
			n = m;
		}
		if ( at + n > max - ( nd - 1 - i ) ) {
			v[0] = d[i].r;
			n = 1;
		}
		for ( k = 0; k < n; k++ ) pool[at + k] = v[k];
		d[i].s0 = at;
		d[i].ns = n;
		at += n;
	}
	return at;
}

/*
 * The supertiles of a frame of fw x fh pixels at (fx, fy): 1 x 1 tile,
 * made larger until there are fewer than maxst across and down and at
 * most maxst in all.
 */
LOCAL void cp_grid( CP_GRID *g, INT fx, INT fy, INT fw, INT fh, INT tile, INT maxst )
{
	g->fx = fx;  g->fy = fy;  g->fw = fw;  g->fh = fh;
	g->tile = tile;
	g->tx = ( fw + tile - 1 ) / tile;
	g->ty = ( fh + tile - 1 ) / tile;
	g->sw = g->sh = 1;
	for (;;) {
		g->nx = ( g->tx + g->sw - 1 ) / g->sw;
		g->ny = ( g->ty + g->sh - 1 ) / g->sh;
		if ( g->nx < maxst && g->ny < maxst && g->nx * g->ny <= maxst ) {
			break;
		}
		if ( g->sw < g->sh ) g->sw++; else g->sh++;
	}
}

/* Supertile s, counted along the rows, on the screen and cut to the frame */
LOCAL void cp_grid_at( CONST CP_GRID *g, INT s, CP_SR *r )
{
	INT	pw = g->tile * g->sw, ph = g->tile * g->sh;

	r->l = g->fx + ( s % g->nx ) * pw;
	r->t = g->fy + ( s / g->nx ) * ph;
	r->r = ( r->l + pw < g->fx + g->fw ) ? r->l + pw : g->fx + g->fw;
	r->b = ( r->t + ph < g->fy + g->fh ) ? r->t + ph : g->fy + g->fh;
}

/* The supertiles a rectangle of the frame meets: columns x0..x1, rows y0..y1 */
LOCAL void cp_grid_span( CONST CP_GRID *g, CONST CP_SR *r, INT *x0, INT *y0, INT *x1, INT *y1 )
{
	INT	pw = g->tile * g->sw, ph = g->tile * g->sh;

	*x0 = ( r->l - g->fx ) / pw;
	*y0 = ( r->t - g->fy ) / ph;
	*x1 = ( r->r - 1 - g->fx ) / pw;
	*y1 = ( r->b - 1 - g->fy ) / ph;
}

/*
 * Each supertile's class from what shows of the draws: CP_ST_FREE when
 * none meets it, CP_ST_LOAD when some do, CP_ST_WIN ('wload') when the
 * last copy that writes all of it, base[s], has nothing drawn in it
 * before it. base[] is -1 where it is not CP_ST_WIN.
 */
LOCAL void cp_classify( CONST CP_GRID *g, CONST CP_DR *d, INT nd, CONST CP_SR *pool, INT wload,
			UB *cls, INT *base )
{
	INT	n = g->nx * g->ny, s, i, k, x, y, x0, y0, x1, y1;
	CP_SR	st;

	for ( s = 0; s < n; s++ ) {
		cls[s] = CP_ST_FREE;
		base[s] = -1;
	}
	for ( i = 0; wload && i < nd; i++ ) {
		if ( !d[i].copy || !d[i].wok || d[i].r.r <= d[i].r.l || d[i].r.b <= d[i].r.t ) continue;
		cp_grid_span(g, &d[i].r, &x0, &y0, &x1, &y1);
		for ( y = y0; y <= y1; y++ ) {
			for ( x = x0; x <= x1; x++ ) {
				cp_grid_at(g, y * g->nx + x, &st);
				if ( cp_sr_holds(&d[i].r, &st) ) base[y * g->nx + x] = i;
			}
		}
	}
	for ( i = 0; i < nd; i++ ) {
		for ( k = 0; k < d[i].ns; k++ ) {
			cp_grid_span(g, &pool[d[i].s0 + k], &x0, &y0, &x1, &y1);
			for ( y = y0; y <= y1; y++ ) {
				for ( x = x0; x <= x1; x++ ) {
					s = y * g->nx + x;
					cls[s] = CP_ST_LOAD;
					if ( i < base[s] ) base[s] = -2;	/* drawn in before its copy */
				}
			}
		}
	}
	for ( s = 0; s < n; s++ ) {
		if ( cls[s] == CP_ST_LOAD && base[s] >= 0 ) {
			cls[s] = CP_ST_WIN;
		} else {
			base[s] = -1;
		}
	}
}

/*
 * The supertiles loaded from a copy's pixels taken out of what shows of
 * that copy, a run of them along a row at a time, into the pool from
 * 'at'. A copy that would be cut into too many, or for which the pool
 * has no room, is left as it is, which draws the pixels just loaded
 * over again. The pool's use after it.
 */
LOCAL INT cp_cut_bases( CONST CP_GRID *g, CP_DR *d, INT nd, CP_SR *pool, INT at, INT max,
			CONST UB *cls, CONST INT *base )
{
	CP_SR	v[CP_CUT_MAX], a, z;
	INT	i, k, n, x, y, x2, any;

	for ( i = 0; i < nd; i++ ) {
		any = 0;
		for ( k = 0; k < g->nx * g->ny && !any; k++ ) {
			any = ( cls[k] == CP_ST_WIN && base[k] == i );
		}
		if ( !any ) continue;
		n = d[i].ns;
		for ( k = 0; k < n; k++ ) v[k] = pool[d[i].s0 + k];
		for ( y = 0; y < g->ny && n > 0; y++ ) {
			for ( x = 0; x < g->nx && n > 0; x = x2 ) {
				x2 = x + 1;
				if ( cls[y * g->nx + x] != CP_ST_WIN || base[y * g->nx + x] != i ) continue;
				while ( x2 < g->nx && cls[y * g->nx + x2] == CP_ST_WIN && base[y * g->nx + x2] == i ) {
					x2++;
				}
				cp_grid_at(g, y * g->nx + x, &a);
				cp_grid_at(g, y * g->nx + x2 - 1, &z);
				a.r = z.r;
				n = cp_sr_cut(v, n, &a, CP_CUT_MAX);
			}
		}
		if ( n < 0 || at + n > max ) continue;
		for ( k = 0; k < n; k++ ) pool[at + k] = v[k];
		d[i].s0 = at;
		d[i].ns = n;
		at += n;
	}
	return at;
}

/*
 * What a part costs, in eighths of a nanosecond, for sharing the ground
 * between the GPU and the processor: a pixel of a tile loaded and stored
 * with the caches made ready round it, a pixel of a copy and of the
 * other shaders, a job's own time, and a pixel the processor copies.
 */
#define CP_CW_TILE	12
#define CP_CW_COPY	12
#define CP_CW_SHADE	28
#define CP_CW_JOB	200000
#define CP_CW_CPU	10

/* The GPU's cost of a job as its classes stand, 'tw' that of a rendered tile's pixel */
LOCAL UD cp_cost( CONST CP_GRID *g, CONST UB *cls, CONST CP_DR *d, INT nd, CONST CP_SR *pool, INT tw )
{
	UD	c = 0;
	INT	s, i, k, any = 0;
	CP_SR	st;

	for ( s = 0; s < g->nx * g->ny; s++ ) {
		if ( cls[s] == CP_ST_SKIP || cls[s] == CP_ST_FREE || cls[s] == CP_ST_CPU ) continue;
		cp_grid_at(g, s, &st);
		c += (UD)( st.r - st.l ) * (UD)( st.b - st.t ) * (UD)tw;
		any = 1;
	}
	for ( i = 0; i < nd; i++ ) {
		for ( k = 0; k < d[i].ns; k++ ) {
			CONST CP_SR	*r = &pool[d[i].s0 + k];

			c += (UD)( r->r - r->l ) * (UD)( r->b - r->t ) * (UD)d[i].w;
		}
	}
	return any ? c + CP_CW_JOB : c;
}

/*
 * The first job's free supertiles: those no rectangle of keep[] meets
 * go to the processor ('cpu'), which copies the ground into them while
 * the GPU works, and the rest are loaded and stored as they are. Then,
 * as long as the processor's share costs more than the GPU's ('gpu':
 * the other jobs' and this one's before), the last of the processor's
 * go to the GPU. How many supertiles the processor keeps.
 */
LOCAL INT cp_share( CONST CP_GRID *g, UB *cls, CONST CP_SR *keep, INT nkeep, INT cpu, UD gpu )
{
	INT	n = g->nx * g->ny, s, k, ncpu = 0, any = 0;
	UD	c = 0, px;
	CP_SR	st;

	for ( s = 0; s < n; s++ ) {
		if ( cls[s] == CP_ST_LOAD || cls[s] == CP_ST_WIN ) any = 1;
	}
	for ( s = 0; s < n; s++ ) {
		if ( cls[s] != CP_ST_FREE ) continue;
		cp_grid_at(g, s, &st);
		px = (UD)( st.r - st.l ) * (UD)( st.b - st.t );
		for ( k = 0; cpu && k < nkeep && !cp_sr_meet(&st, &keep[k]); k++ );
		if ( cpu && k >= nkeep ) {
			cls[s] = CP_ST_CPU;
			c += px * CP_CW_CPU;
			ncpu++;
			continue;
		}
		cls[s] = CP_ST_LOAD;
		gpu += px * CP_CW_TILE + ( any ? 0 : CP_CW_JOB );
		any = 1;
	}
	for ( s = n - 1; s >= 0 && c > gpu; s-- ) {
		if ( cls[s] != CP_ST_CPU ) continue;
		cp_grid_at(g, s, &st);
		px = (UD)( st.r - st.l ) * (UD)( st.b - st.t );
		cls[s] = CP_ST_LOAD;
		c -= px * CP_CW_CPU;
		gpu += px * CP_CW_TILE + ( any ? 0 : CP_CW_JOB );
		any = 1;
		ncpu--;
	}
	return ncpu;
}

#endif /* _V3D_CP_SPLIT_H_ */
