/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	compose.h
 *	Laying one window onto the screen being built, stage by stage
 *	(compose.c)
 *
 *	The window manager describes a window as a T_CPWIN -- where it
 *	stands, its pixels, how solid each part of it is, its corners, its
 *	washes, its shadow, how far what shows through it is spread -- and
 *	the stages work from that description alone. Nothing in it is how
 *	the window manager keeps its windows, so another way of doing the
 *	stages can take the same description.
 */

#ifndef __WM_COMPOSE_H__
#define __WM_COMPOSE_H__

#include <ts/dp.h>

#define CP_ROUND_MAX	32		/* the largest corner laid with partial cover */
#define CP_BLUR_MAX	16		/* the furthest what shows through is spread */
#define CP_BLUR_DEPTH	3		/* the most times it is halved on the way */

typedef struct {
	T_DPRECT	outer;		/* where it stands on the screen */
	T_DPRECT	work;		/* its work area, inside that */
	T_DPRECT	band;		/* its title band; empty when it has none */
	CONST UB	*pixels;	/* its own, from its corner, rows 'pitch' long */
	UINT		pitch;
	UINT		a_frame;	/* how solid the frame is, 0..255 */
	UINT		a_work;		/* and the work area */
	BOOL		keyed;		/* a work area pixel with 0xFF on top is not laid */
	INT		round;		/* the corners' radius, laid with partial cover */
	CONST T_DPRGN	*shape;		/* a hard outline of its own instead, or NULL */
	UW		wash;		/* the frame washed toward this */
	UINT		wash_str;	/* by this much, 0..255; 0 not at all */
	UW		bar_wash;	/* the title band toward this */
	UINT		bar_str;
	UW		tint;		/* the program's own colour over the frame */
	UINT		tint_str;
	INT		blur;		/* how far what shows through is spread; 0 not */
	INT		shd_dx;		/* the shadow: thrown so far across */
	INT		shd_dy;		/* and down */
	INT		shd_size;	/* fading out over so many dots */
	UINT		shd_dark;	/* this dark where it is darkest; 0 no shadow */
	T_DPRECT	shown;		/* where it is laid: outer, or less while it opens or closes */
	UINT		fade;		/* how much of it shows while it opens or closes, else 255 */
} T_CPWIN;

/* The back buffer, and the part of it being built */
typedef struct {
	UB		*back;
	UINT		pitch;
	T_DPRECT	box;
	INT		rows;		/* rows the back buffer has */
} T_CPDST;

/* How much of each pixel of a corner of radius rad is covered, rad by rad; NULL for none */
IMPORT CONST UB *cp_corner_cov( INT rad );

/* The radius a round of 'rad' comes to on a rectangle of this size */
IMPORT INT  cp_round_of( CONST T_DPRECT *r, INT rad );

/*
 * The frame's lines drawn round the corners of a window's own pixels, on
 * the curve its corners are cut on: for each corner (0 top left, 1 top
 * right, 2 bottom left, 3 bottom right) how many dots in from the side
 * edge (left or right) and from the end edge (top or bottom) are lines
 * of the frame. Drawing them again changes nothing.
 */
typedef struct {
	INT	side[4];
	INT	end[4];
} T_CPLINES;

IMPORT void cp_corner_lines( UB *pixels, UINT pitch, CONST T_DPRECT *outer, INT round,
			     CONST T_CPLINES *lines );

/* Whether a window is opening or closing (laid scaled and faded) */
#define CP_MOVING(w)	( (w)->fade < 255 || (w)->shown.left != (w)->outer.left \
			  || (w)->shown.top != (w)->outer.top \
			  || (w)->shown.right != (w)->outer.right \
			  || (w)->shown.bottom != (w)->outer.bottom )

/*
 * The stages, in the order a window goes down. Each touches the back
 * buffer only inside d->box.
 *
 *   cp_shadow	what is behind is darkened round the window
 *   cp_blur	what shows through the parts that are not solid is spread
 *   cp_blend	the window's pixels mixed over that, the frame washed
 *		on the way
 *   cp_moving	a window opening or closing, in place of the three
 *
 * cp_window does them in order. cp_shadow_extent says how far the
 * window reaches with its shadow, FALSE for nothing on the screen.
 */
IMPORT BOOL cp_shadow_extent( CONST T_CPWIN *w, T_DPRECT *out );
IMPORT void cp_shadow( CONST T_CPDST *d, CONST T_CPWIN *w );
IMPORT void cp_blur( CONST T_CPDST *d, CONST T_CPWIN *w );
IMPORT void cp_blend( CONST T_CPDST *d, CONST T_CPWIN *w );
IMPORT void cp_moving( CONST T_CPDST *d, CONST T_CPWIN *w );
IMPORT void cp_window( CONST T_CPDST *d, CONST T_CPWIN *w );

/*
 * The windows w[0..n-1] of a part, from the back forward, laid as
 * cp_window lays each in turn and to the same pixels, but each only
 * where those in front of it do not lay theirs solid over it: a pixel
 * a solid window shows is that window's alone, and nothing behind it is
 * laid there. Where a window in front spreads what shows through it,
 * what it reads is laid as before. Of more than CP_LAY_MAX, the
 * furthest back are laid whole.
 *
 *   cp_hide_use	whether what is hidden is left out at all (a
 *			measurement, a test): without it every window is laid
 *			whole
 *   cp_hidden		how many pixels of windows have been left out so far
 */
#define CP_LAY_MAX	64		/* windows cp_lay takes at once */

IMPORT void cp_lay( CONST T_CPDST *d, CONST T_CPWIN *w, INT n );
IMPORT void cp_hide_use( BOOL on );
IMPORT UD   cp_hidden( void );

/*
 * How what shows through a rectangle r of the part being built ('box')
 * is spread, for a distance of 'rad' dots (compose.c): halved 'depth'
 * times, each pixel of a level from the four by four round its place in
 * the level below; smoothed at the deepest level with the weights w
 * (of 64 each way); doubled back as many times, each pixel from the two
 * by two of the level above nearest its centre. The pixel (x, y) of
 * level k stands for the screen's from (x << k, y << k). For each level:
 * the pixels that touch the box, which are all a step reads (past them
 * the one at their edge); those worked out on the way down, of level 0
 * what is read of the screen; and those on the way up, of level 0 the
 * rectangle itself and of the deepest level the smoothed. FALSE when
 * nothing is spread.
 *
 * cp_spread_reach says how far from a pixel what its spread reads lies,
 * at most: a part built alone comes out as the whole screen would if a
 * margin as wide is built round it for each window spread over it.
 */
typedef struct {
	INT		depth;
	INT		w[5];
	T_DPRECT	valid[CP_BLUR_DEPTH + 1];
	T_DPRECT	down[CP_BLUR_DEPTH + 1];
	T_DPRECT	up[CP_BLUR_DEPTH + 1];
} T_CPSPREAD;

IMPORT BOOL cp_spread_plan( CONST T_DPRECT *box, CONST T_DPRECT *r, INT rad, T_CPSPREAD *p );
IMPORT INT  cp_spread_reach( INT rad );

/*
 * The edge of a part of the screen built larger than it was asked
 * for, kept and put back: a spread needs what lies round what it
 * spreads, and what is built past what was asked for is not to change.
 */
IMPORT BOOL cp_margin_keep( UB *back, UINT pitch, CONST T_DPRECT *outer, CONST T_DPRECT *inner );
IMPORT void cp_margin_put( UB *back, UINT pitch, CONST T_DPRECT *outer, CONST T_DPRECT *inner );

/* The room the stages keep, let go of (the spread turned off) */
IMPORT void cp_release( void );

/*
 * The parts of a window as cp_blend lays them: its work area and the
 * strips of frame round it, each cut to what is being built, with how
 * solid it is and the wash it takes. A strip of frame is also tinted.
 */
#define CP_NPIECE	5

typedef struct {
	T_DPRECT	r;
	UINT		a;
	BOOL		frame;
	UW		wcol;
	UINT		wstr;
} T_CPPIECE;

IMPORT INT  cp_pieces( CONST T_CPDST *d, CONST T_CPWIN *w, T_CPPIECE *p );

/*
 * Another way of doing the stages, on a GPU, which a driver puts in.
 * The window manager hands it the ground and the windows from the back
 * forward; it lays the ground and as many of the windows as it can and
 * answers how many, and the processor lays the rest over them as it
 * always does. A screen built so is the one the processor alone builds.
 *
 *   align	the part being built starts at a multiple of this many
 *		pixels; the window manager widens what it builds to it
 *   wants	whether a part this large is worth it now; 'fx' says what
 *		the windows' look asks for (CP_FX_*)
 *   lay	the ground (gpitch bytes a row, 'gen' changes whenever it
 *		is worked out again) and windows 0..n-1: how many it laid,
 *		or an error when it laid nothing and the part is to be
 *		built on the processor from the ground up
 *   note	how long a part took to build, on the GPU or not
 */
#define CP_GPU_MAX	64		/* windows handed over at once */
#define CP_FX_LOOK	0x0001		/* a look beyond solid and square */
#define CP_FX_BLUR	0x0002		/* what shows through is spread */

typedef struct {
	UINT	align;
	BOOL	(*wants)( CONST T_DPRECT *box, UINT fx );
	INT	(*lay)( CONST T_CPDST *d, CONST UW *ground, UINT gpitch, UINT gen,
			CONST T_CPWIN *w, INT n );
	void	(*note)( BOOL gpu, CONST T_DPRECT *box, UD us );
} T_CPGPU;

IMPORT void		cp_gpu_set( CONST T_CPGPU *g );
IMPORT CONST T_CPGPU	*cp_gpu_get( void );

/*
 * The stages' inner loops on the processor's vector unit (compose_simd.c).
 * The kernel is built without the FP/SIMD registers, and they are kept
 * across a task switch only for a task made with TA_FPU, so the stages
 * use those loops only in a task that has said it owns them: the window
 * manager's compositor. Everywhere else, and with cp_simd_use(FALSE),
 * they take the plain loops; both write the same pixels, to the bit.
 *
 *   cp_simd_allow	a task (made with TA_FPU) may use them, or no more
 *   cp_simd_use	whether they are used at all (a measurement, a test)
 *   cp_simd_here	whether the task running may use them now
 *   cp_copy		a row of pixels copied, by whichever the task may use
 */
IMPORT void cp_simd_allow( ID tid, BOOL on );
IMPORT void cp_simd_use( BOOL on );
IMPORT BOOL cp_simd_here( void );
IMPORT void cp_copy( UW *dst, CONST UW *src, INT n );

/* A level of the spread, or the part of it worked out (compose.c) */
typedef struct {
	CONST UW	*p;		/* its pixel at (r.left, r.top) */
	SZ		pitch;		/* words from a row to the next */
	T_DPRECT	r;
} T_CPLEVEL;

/*
 * The loops themselves, called only where cp_simd_here() says so. 'row'
 * and 'mid' are the room compose.c keeps for a row of a level and for
 * the sums of a pass, eight bytes a pixel as the plain loops use them.
 */
IMPORT void cps_copy( UW *d, CONST UW *s, INT n );
IMPORT void cps_lay( UW *dst, CONST UW *src, CONST UW *und, INT n, UINT a, BOOL keyed,
		     UW tint, UINT tstr, UW wcol, UINT wstr );
IMPORT void cps_darken( UW *row, CONST UB *a, INT n );
IMPORT void cps_halve( CONST T_CPLEVEL *s, CONST T_DPRECT *d, UW *out, UH *row, UH *mid );
IMPORT void cps_smooth( CONST T_CPLEVEL *s, CONST T_DPRECT *d, CONST INT *wt, UW *out,
			UH *row, UH *mid );
IMPORT void cps_double( CONST T_CPLEVEL *s, CONST T_DPRECT *d, UW *out, UH *row, UH *mid );

#endif /* __WM_COMPOSE_H__ */
