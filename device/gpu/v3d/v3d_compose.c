/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	v3d_compose.c
 *	Compositing the windows on the V3D (docs/tessronos/13-gpu.md 13.10).
 *
 *	The window manager builds the screen in its back buffer: the ground,
 *	then the windows from the back forward, each in the stages of
 *	compose.c. This file does the same on the GPU, as render jobs: the
 *	frame being built is the part of the back buffer asked for, each
 *	tile of it is loaded from the ground, every window's stages are
 *	drawn over it as rectangles, and the tile is stored back. The
 *	fragment shaders (mesa/v3d_cp_shaders.h, made by tools/mesa/
 *	shaders.sh) work each pixel out with the same integer arithmetic
 *	as compose.c, reading what is behind the pixel from the tile
 *	buffer and the windows' own pixels from memory, so the screen comes
 *	out the same to the bit.
 *
 *	A shader's cost is paid for each pixel it is drawn over, so the
 *	rectangles are cut to where there is work (v3d_cp_split.h): the
 *	shadow is not drawn where the window covers it, the solid, plain
 *	parts of a window, most of a screen, are drawn by 'copy', which
 *	reads the window's word and writes it, and nothing is drawn where a
 *	later copy writes over it.
 *
 *	Loading and storing a tile costs about as much as the processor's
 *	copying it, so each supertile of a job is rendered only as far as it
 *	has to be: one no draw meets is left out of a job that loads from
 *	the back buffer, and in the first job the processor copies the
 *	ground into such supertiles itself while the GPU works on the rest,
 *	as many of them as keeps the two about even; one that a window's
 *	copy writes all of is loaded straight from the window's pixels, and
 *	the copy is left out of it. Each kind of supertile has its generic
 *	tile list, and the renderer's list switches between them.
 *
 *	A window whose frame shows what is behind it spread needs what is
 *	behind it in memory, so the frame is stored before it, spread in a
 *	job for each step of compose.c's spread -- halved level by level,
 *	smoothed at the deepest, doubled back -- every pixel of a level a
 *	fragment of its own, the levels kept in T1 and T2, and a new frame
 *	loaded from the back buffer goes on, cut to what its draws cover.
 *	One part of the screen is therefore one job, or two more for each
 *	level such a window's spread goes down and two besides, all queued
 *	at once and waited for together.
 *
 *	The memory the jobs use is the window manager's own: the back
 *	buffer, the ground and the windows' pixels are entered in the GPU's
 *	MMU as they are (knl_gpu_bo_wrap) and kept there until the kernel
 *	lets them go (knl_vunmap_hook); T1 and T2 are taken when this is set
 *	up. Before the jobs, the processor's caches are cleaned of what the
 *	GPU reads -- the rows of the windows the draws and tiles read -- and
 *	the lines of the back buffer the GPU writes are dropped, without
 *	being written back; after the jobs they are dropped again, so that
 *	the processor reads what the GPU wrote. Each of these is one run of
 *	line operations between two barriers.
 *
 *	How long each step of a part took (the lists, the caches, handing
 *	the jobs over, the processor's share of the ground, the GPU's own
 *	steps as the driver timed them, the wake-up) is kept, printed for
 *	each scene of the check and summed in the statistics.
 *
 *	The GPU is used only once a check at start-up (cg_check) has built
 *	a few scenes both ways and found them the same, and only for parts
 *	of the screen large enough to be worth a job. The ways above beyond
 *	the plain jobs are each turned off in turn when the check finds a
 *	scene differs with them; a job that fails turns the GPU off until
 *	the next start. Every line this file writes to the console begins
 *	"v3d compose:".
 */

#include <sys/machine.h>

#ifdef RPI5

#include "kernel.h"
#include <tk/tkernel.h>
#include <tk/smem.h>
#include <tm/tmonitor.h>
#include <sys/sysdef.h>
#include <ts/disp.h>
#include "sysdepend/sysdepend.h"
#include "sysman/pfalloc.h"
#include "../gpudev.h"
#include "v3d.h"
#include "../../../outer_kernel/wm/compose.h"

#ifndef CNF_V3D
#define CNF_V3D		0
#endif

/* The generated packing: here a GPU address is a plain 32 bit number */
#define __gen_user_data		void
#define __gen_address_type	UW
#define __gen_address_offset(a)	(*(a))
#define __gen_emit_reloc(d, a)	((void)0)
#include "mesa/v3d_packet_v71_pack.h"
#include "mesa/v3d_cp_shaders.h"
#include "v3d_cp_split.h"

/* One render target of 32 bits a pixel: 64 x 64 tiles */
#define TILE			64
#define LOG2_TILE_ENUM		3		/* 8 << 3 = 64 */
#define MAX_SUPERTILES		256
#define TILE_ALLOC_BLOCK	128		/* the first block of each tile's list */
#define TILE_ALLOC_BLOCK_ENUM	1
#define TILE_ALLOC_MORE_ENUM	0		/* 64 byte blocks as a list grows */
#define TILE_STATE_BYTES	256
#define TILE_ALLOC_EXTRA	( 2 * 1024 * 1024 )	/* room for lists to grow before the overflow memory */

/* A frame starts on 64 bytes of the back buffer */
#define ALIGN_PX		16

/* Lists, records, uniforms, corners and vertices of one part of the screen */
#define CMD_ORDER		8		/* 1MB */
#define CMD_BYTES		( PAGE_SIZE << CMD_ORDER )
#define CMD_ZERO		4096		/* zeroes at the start, read for a corner of radius 0 */
#define CMD_TAIL		( 8 * 1024 )	/* never written: the list executor reads 1KB ahead */

#define CODE_BYTES		( 32 * 1024 )
#define CODE_ALIGN		256

#define JOB_DRAWS_MAX		128		/* draws in one job; a frame with more is split */
#define NDRAW			2048		/* draws of one part of the screen */
#define NJOB			128		/* jobs for one part of the screen */
#define NWRAP			96		/* memory kept in the MMU: every window handed over, and more */
#define NJOBBO			( NWRAP + 8 )
#define SUB_MAX			4096		/* rectangles what shows of a part's draws is cut into */
#define NVAR			16		/* windows the tiles of one job are loaded from */
#define NKEEP			64		/* what the processor's share of the ground keeps clear of */
#define WAIT_US			( 4 * 1000 * 1000 )	/* past the common layer's reset at 2 s */

#define STATS_MS		60000		/* the counts are written this often while in use */

/*
 * The ways beyond plain jobs, each of which the check turns off when a
 * scene differs with it: draws cut to what shows of them, supertiles
 * rendered as far as they have to be (left out, the processor's share
 * of the ground), tiles loaded from the windows' pixels, the spread on
 * the GPU (without it, the processor lays from the first window that
 * spreads).
 */
#define CF_CULL			0x01
#define CF_TILES		0x02
#define CF_WLOAD		0x04
#define CF_SPREAD		0x08
#define CF_ALL			( CF_CULL | CF_TILES | CF_WLOAD | CF_SPREAD )

/* ---------------------------------------------------------------- state */

/* Memory of someone else's, entered in the GPU's MMU */
typedef struct {
	UBINT		va;		/* kernel address of its first page; 0 unused */
	UD		npages;
	T_GPUBO		*bo;
	UW		gpu;
	UD		used;		/* the part of the screen it was last used for */
	BOOL		gone;		/* its pages were let go of: not to be used again */
} WRAP;

/* One rectangle drawn by one shader */
typedef struct {
	T_DPRECT	r;		/* within its job's frame: on the screen, or in a level of the spread */
	UW		rec;		/* GPU address of its shader state record */
	UB		*recp;		/* and the processor's, its vertices' address filled in with the lists */
	UW		*unif;		/* its uniforms, the frame's place among them */
	INT		fs;
	UD		reads;		/* words it reads through the TMU for each pixel of r */
	CONST T_CPWIN	*win;		/* the window whose pixels it reads where it draws; NULL */
	BOOL		wok;		/* a copy of them, loadable into a tile: */
	UW		wsrc;		/* their GPU address, row pitch and where the window stands */
	UINT		wpitch;
	INT		wl, wt;
} DRAW;

typedef struct {
	INT		fx, fy, fw, fh;	/* the frame on the screen */
	UW		load;		/* where its tiles are loaded from; 0 none */
	UINT		lpitch;
	UW		store;		/* and stored to; 0 none */
	UINT		spitch;
	BOOL		ground;		/* the first: loads the ground, the processor may share it */
	BOOL		fit;		/* loaded from and stored to the back buffer: cut to its draws */
	BOOL		planned;	/* planned and with something to do */
	UW		back;		/* the back buffer's first pixel, for 'fit' */
	INT		d0, ndraw;	/* its draws, cg.draw[d0..] */
	CP_GRID		g;		/* its supertiles */
	UB		cls[MAX_SUPERTILES];	/* each one's CP_ST_* */
	UB		var[MAX_SUPERTILES];	/* a CP_ST_WIN's window, of those below */
	UW		vaddr[NVAR];	/* the windows its tiles load from: as the frame's first pixel */
	UINT		vpitch[NVAR];
	CONST T_CPWIN	*vwin[NVAR];
	INT		nvar;
} CJOB;

/*
 * Where the time of a part of the screen went, in ticks of the counter,
 * and how much there was to do.
 */
typedef struct {
	UD		build;		/* the draws and the lists worked out */
	UD		before;		/* the caches made ready for the jobs */
	UD		before_win;	/* of that, the rows the GPU reads cleaned (the windows', the ground's when it changed) */
	UD		submit;		/* the jobs handed to the common layer */
	UD		cpu;		/* the processor copying its share of the ground, while the GPU works */
	UD		start;		/* from the jobs handed over to the first job's start */
	UD		gpu;		/* from the first job's start to the last one's end */
	UD		wake;		/* from that end to this task running on */
	UD		after;		/* the back buffer's rows dropped from the caches */
	T_V3DJOBT	jt;		/* the GPU's steps, as the driver timed them */
	UD		njob, ntile, ntile_win, ntile_out, ndraw;
	UD		px;		/* pixels drawn by the shaders */
	UD		px_copy;	/* of them, drawn by 'copy' */
	UD		px_out;		/* pixels of draws left out, a later copy writing over them */
	UD		tmu;		/* words the shaders read through the TMU */
	UD		cpu_px;		/* pixels of the ground the processor copied */
} PHASE;

LOCAL struct {
	BOOL		ready;		/* set up: code, lists, tile memory */
	BOOL		passed;		/* the check at start-up found the GPU's screen the same */
	BOOL		failed;		/* a job failed: off until the next start */
	BOOL		draw_index;	/* the record with draw index (revision 10 on) */
	UINT		feat;		/* the ways in use, CF_* */
	ID		mtx;

	T_GPUBO		*code;
	UW		prog[2 + CP_NFS];	/* GPU address of cs, vs and each fragment shader */
	UW		unif_cs, unif_vs;	/* their uniforms, all constant */

	PFRAME		*cmd_pf;
	UB		*cmd;		/* the processor's address of it */
	T_GPUBO		*cmd_bo;
	UW		cmd_gpu;
	UD		cmd_used;
	BOOL		cmd_over;

	T_GPUBO		*tile_alloc;
	UD		ta_bytes;
	T_GPUBO		*tile_state;
	UD		ts_bytes;
	T_GPUBO		*t1, *t2;	/* the levels of a spread (v3d_cp_split.h cp_lv_row) */
	UD		t_bytes;

	WRAP		wrap[NWRAP];
	UD		part;		/* parts of the screen built */
	T_SPLOCK	wrap_lock;

	UW		cov_at[CP_ROUND_MAX + 1];	/* each radius's corner in the lists, this part */

	CJOB		job[NJOB];
	INT		njob;
	CJOB		*cur;
	DRAW		draw[NDRAW];
	CP_DR		dr[NDRAW];	/* the same, as v3d_cp_split.h plans them */
	INT		ndraw;
	CP_SR		pool[SUB_MAX];	/* what shows of the draws */
	INT		npool;
	INT		base[MAX_SUPERTILES];	/* a job's supertiles' base copies, while it is planned */
	CP_SR		keep[NKEEP];	/* what the processor's share keeps clear of */
	INT		nkeep;
	BOOL		keep_over;

	/* the part's back buffer and ground, for the processor's share */
	UB		*p_back;
	UINT		p_pitch;
	CONST UW	*p_ground;
	UINT		p_gpitch;

	T_GPUJOB	gj[NJOB];	/* the jobs as the common layer takes them */
	INT		ngj;		/* how many: a job with nothing to render is left out */
	T_GPUBO		*jbo[NJOBBO];	/* the BOs the jobs use */
	INT		njbo;

	CONST UW	*ground_at;	/* the ground cleaned from the caches, as of 'gen' */
	UINT		ground_gen;

	UD		thr_plain;	/* parts of at least this many pixels go to the GPU */
	UD		thr_fx;		/* the same with a look beyond solid and square */
	BOOL		blur_ok;	/* a spread is quicker on the GPU than on the processor */

	/* counted for the console */
	UD		st_t0;
	UD		st_gpu, st_cpu, st_jobs, st_fallback, st_partial;
	UD		st_full_gpu_n, st_full_gpu_us, st_full_cpu_n, st_full_cpu_us;
	UD		st_gpu_px, st_gpu_us, st_cpu_px, st_cpu_us;
	PHASE		ph;		/* the part being built */
	PHASE		st_ph;		/* summed over the parts on the GPU */
	UD		st_ph_n;
} cg;

#define CMD_ADDR(p)	( cg.cmd_gpu + (UW)( (UB *)(p) - cg.cmd ) )

/* ---------------------------------------------------------------- time */

LOCAL UD ticks( void )
{
	UD	v;

	Asm("isb; mrs %0, cntvct_el0" : "=r"(v) :: "memory");
	return v;
}

LOCAL UD ticks_us( UD t )
{
	UD	f;

	Asm("mrs %0, cntfrq_el0" : "=r"(f));
	return ( f == 0 ) ? 0 : t * 1000000 / f;
}

LOCAL UD now_ms( void )
{
	SYSTIM	t;

	(void)tk_get_otm(&t);
	return ( (UD)(UW)t.hi << 32 ) | (UW)t.lo;
}

/* ---------------------------------------------------------------- caches */

/*
 * The line operations over a range, without the barriers round them,
 * so that a run of ranges waits for its operations only once:
 *
 *   OP_CLEAN	written to memory, the lines kept (what the GPU reads)
 *   OP_INVAL	the lines dropped, so that what the GPU wrote is read from
 *		memory; a line only partly inside is written back first,
 *		so that what the processor holds of its other part is kept.
 *		Dropped before a job too: whatever the processor held of
 *		rows the GPU writes all over is not wanted, and a line held
 *		dirty would otherwise reach memory after the GPU's pixels.
 */
#define OP_CLEAN	0
#define OP_INVAL	1

LOCAL void dc_lines( UBINT va, UBINT len, INT op )
{
	UBINT	end = va + len, a = va & ~63ULL;

	if ( op == OP_CLEAN ) {
		for ( ; a < end; a += 64 ) {
			Asm("dc cvac, %0" :: "r"(a) : "memory");
		}
		return;
	}
	for ( ; a < end; a += 64 ) {
		if ( a < va || a + 64 > end ) {
			Asm("dc civac, %0" :: "r"(a) : "memory");
		} else {
			Asm("dc ivac, %0" :: "r"(a) : "memory");
		}
	}
}

LOCAL void dc_begin( void )
{
	Asm("dsb sy" ::: "memory");
}

LOCAL void dc_end( void )
{
	Asm("dsb sy" ::: "memory");
}

/* The same for columns x0..x1 of rows y0..y1 of a raster, between the barriers of the caller */
LOCAL void dc_rows( CONST UB *base, UINT pitch, INT x0, INT x1, INT y0, INT y1, INT op )
{
	UBINT	a, len;
	INT	y;

	if ( x1 <= x0 || y1 <= y0 ) {
		return;
	}
	if ( (UINT)( x1 - x0 ) * 4 == pitch ) {		/* whole rows: one run */
		dc_lines((UBINT)base + (UBINT)y0 * pitch, (UBINT)( y1 - y0 ) * pitch, op);
		return;
	}
	len = (UBINT)( x1 - x0 ) * 4;
	a = (UBINT)base + (UBINT)y0 * pitch + (UBINT)x0 * 4;
	for ( y = y0; y < y1; y++, a += pitch ) {
		dc_lines(a, len, op);
	}
}

/* ---------------------------------------------------------------- memory in the MMU */

IMPORT UBINT	knl_pt_l0_k_pa;

/* The kernel let a range of its variable area go: what of it is in the MMU is not used again */
LOCAL void vunmap_seen( UBINT va, UD npages )
{
	UBINT	end = va + ( npages << PAGE_SHIFT );
	UINT	imask;
	INT	i;

	ISpinLock(&cg.wrap_lock, &imask);
	for ( i = 0; i < NWRAP; i++ ) {
		WRAP	*w = &cg.wrap[i];

		if ( w->va != 0 && w->va < end && va < w->va + ( w->npages << PAGE_SHIFT ) ) {
			w->gone = TRUE;
		}
	}
	/* a ground made later at the same address is not to be taken for clean */
	if ( (UBINT)cg.ground_at >= va && (UBINT)cg.ground_at < end ) {
		cg.ground_at = NULL;
	}
	ISpinUnlock(&cg.wrap_lock, &imask);
}

LOCAL void wrap_drop( WRAP *w )
{
	T_GPUBO	*bo = w->bo;
	UINT	imask;

	ISpinLock(&cg.wrap_lock, &imask);
	w->va = 0;
	w->bo = NULL;
	w->gone = FALSE;
	ISpinUnlock(&cg.wrap_lock, &imask);
	if ( bo != NULL ) {
		knl_gpu_bo_put(bo);
	}
}

/* What went since the last part, taken out of the MMU */
LOCAL void wraps_sweep( void )
{
	INT	i;

	for ( i = 0; i < NWRAP; i++ ) {
		if ( cg.wrap[i].va != 0 && cg.wrap[i].gone ) {
			wrap_drop(&cg.wrap[i]);
		}
	}
}

/* The GPU address of 'len' bytes at 'p' (kernel memory), entering it when it is not in; 0 when it cannot be */
LOCAL UW wrap_of( CONST void *p, UD len )
{
	UBINT	va = (UBINT)p & ~(UBINT)( PAGE_SIZE - 1 );
	UD	npages = ( ( (UBINT)p - va ) + len + PAGE_SIZE - 1 ) >> PAGE_SHIFT;
	UD	*pa, i;
	WRAP	*w = NULL, *old = NULL;
	T_GPUBO	*bo;
	T_GPUBOINFO bi;
	UINT	imask;
	INT	k;
	ER	er;

	for ( k = 0; k < NWRAP; k++ ) {
		WRAP	*c = &cg.wrap[k];

		if ( c->va == va && !c->gone && c->npages >= npages ) {
			c->used = cg.part;
			return c->gpu + (UW)( (UBINT)p - va );
		}
	}
	/*
	 * A slot: an empty one, or the one used longest ago that this part
	 * does not use (an entry over the same memory but shorter may be in
	 * use by an earlier draw of this part, so it is not taken over)
	 */
	for ( k = 0; k < NWRAP; k++ ) {
		WRAP	*c = &cg.wrap[k];

		if ( c->va == 0 ) {
			w = c;
			break;
		}
		if ( c->used != cg.part && ( old == NULL || c->used < old->used ) ) {
			old = c;
		}
	}
	if ( w == NULL ) w = old;
	if ( w == NULL ) {
		return 0;			/* all of them are in this part */
	}
	if ( w->va != 0 ) {
		wrap_drop(w);
	}
	pa = (UD *)Kmalloc(sizeof(UD) * npages);
	if ( pa == NULL ) {
		return 0;
	}
	for ( i = 0; i < npages; i++ ) {
		pa[i] = knl_pt_lookup(knl_pt_l0_k_pa, va + ( i << PAGE_SHIFT ));
		if ( pa[i] == ~(UD)0 ) {
			Kfree(pa);
			return 0;
		}
	}
	bo = knl_gpu_bo_wrap(pa, npages, &er);
	Kfree(pa);
	if ( bo == NULL ) {
		return 0;
	}
	knl_gpu_bo_ref(bo, &bi);
	ISpinLock(&cg.wrap_lock, &imask);
	w->va = va;
	w->npages = npages;
	w->bo = bo;
	w->gpu = bi.offset;
	w->used = cg.part;
	w->gone = FALSE;
	ISpinUnlock(&cg.wrap_lock, &imask);
	return w->gpu + (UW)( (UBINT)p - va );
}

LOCAL UW bo_gpu( T_GPUBO *bo )
{
	T_GPUBOINFO	bi;

	knl_gpu_bo_ref(bo, &bi);
	return bi.offset;
}

/* Copy into a BO through the cache, then write the lines to memory */
LOCAL void bo_put( T_GPUBO *bo, UD off, CONST void *src, UD len )
{
	CONST UB	*s = src;

	while ( len > 0 ) {
		UD	in = off & ( PAGE_SIZE - 1 );
		UD	n = PAGE_SIZE - in;
		UB	*d = (UB *)PA2VA(knl_gpu_bo_page(bo, off >> PAGE_SHIFT)) + in;

		if ( n > len ) n = len;
		knl_memcpy(d, s, n);
		knl_gpu_dcache_clean_inval((UBINT)d, n);
		off += n;
		s += n;
		len -= n;
	}
}

/* ---------------------------------------------------------------- the list memory */

LOCAL UB *cmd_take( UD n, UD align )
{
	UD	at = ( cg.cmd_used + align - 1 ) & ~( align - 1 );

	if ( at + n > CMD_BYTES - CMD_TAIL ) {
		cg.cmd_over = TRUE;
		return NULL;
	}
	cg.cmd_used = at + n;
	return cg.cmd + at;
}

typedef struct {
	UB	*buf;
	UW	room;
	UW	len;
	BOOL	over;
} CL;

LOCAL BOOL cl_open( CL *cl, UW room )
{
	cl->buf = cmd_take(room, 32);
	cl->room = room;
	cl->len = 0;
	cl->over = ( cl->buf == NULL );
	return !cl->over;
}

LOCAL UB *cl_space( CL *cl, UW n )
{
	UB	*p;

	if ( cl->over || cl->len + n > cl->room ) {
		cl->over = TRUE;
		return NULL;
	}
	p = cl->buf + cl->len;
	cl->len += n;
	return p;
}

LOCAL UW cl_addr( CL *cl )
{
	return CMD_ADDR(cl->buf + cl->len);
}

LOCAL void cl_bytes( CL *cl, CONST UB *src, UW n )
{
	UB	*p = cl_space(cl, n);

	if ( p != NULL ) knl_memcpy(p, src, n);
}

/* Pack one packet with its header's defaults and the fields given */
#define EMIT(cl, pkt, ...) do { \
		struct V3D71_##pkt _v = { V3D71_##pkt##_header, __VA_ARGS__ }; \
		UB *_p = cl_space((cl), V3D71_##pkt##_length); \
		if ( _p != NULL ) V3D71_##pkt##_pack(NULL, _p, &_v); \
	} while ( 0 )

/* ---------------------------------------------------------------- numbers for the GPU */

/*
 * The bits of the float (p - CP_VP_HALF) / CP_VP_HALF: a pixel's edge in
 * clip space, exact, since CP_VP_HALF is a power of two.
 */
LOCAL UW fclip( INT p )
{
	INT	v = p - CP_VP_HALF, e = 0, shift = 0;
	UW	m, sign = 0;

	while ( ( 1 << shift ) < CP_VP_HALF ) shift++;
	if ( v == 0 ) {
		return 0;
	}
	if ( v < 0 ) {
		sign = 0x80000000U;
		v = -v;
	}
	m = (UW)v;
	while ( ( m >> ( e + 1 ) ) != 0 ) e++;
	return sign | ( (UW)( e - shift + 127 ) << 23 ) | ( ( m << ( 23 - e ) ) & 0x007FFFFFU );
}

/* ---------------------------------------------------------------- draws */

LOCAL CONST UD *cp_code[2 + CP_NFS] = {
	cp_code_cs, cp_code_vs, cp_code_piece, cp_code_shadow, cp_code_halve, cp_code_smooth3, cp_code_smooth5,
	cp_code_double, cp_code_moving, cp_code_fill, cp_code_copy, cp_code_invert
};
LOCAL CONST UINT cp_code_len[2 + CP_NFS] = {
	sizeof(cp_code_cs), sizeof(cp_code_vs), sizeof(cp_code_piece), sizeof(cp_code_shadow),
	sizeof(cp_code_halve), sizeof(cp_code_smooth3), sizeof(cp_code_smooth5), sizeof(cp_code_double),
	sizeof(cp_code_moving), sizeof(cp_code_fill), sizeof(cp_code_copy), sizeof(cp_code_invert)
};
LOCAL CONST UW (*CONST cp_unif[2 + CP_NFS])[2] = {
	cp_unif_cs, cp_unif_vs, cp_unif_piece, cp_unif_shadow, cp_unif_halve, cp_unif_smooth3, cp_unif_smooth5,
	cp_unif_double, cp_unif_moving, cp_unif_fill, cp_unif_copy, cp_unif_invert
};
LOCAL CONST UINT cp_unif_len[2 + CP_NFS] = {
	sizeof(cp_unif_cs) / 8, sizeof(cp_unif_vs) / 8, sizeof(cp_unif_piece) / 8, sizeof(cp_unif_shadow) / 8,
	sizeof(cp_unif_halve) / 8, sizeof(cp_unif_smooth3) / 8, sizeof(cp_unif_smooth5) / 8,
	sizeof(cp_unif_double) / 8, sizeof(cp_unif_moving) / 8, sizeof(cp_unif_fill) / 8,
	sizeof(cp_unif_copy) / 8, sizeof(cp_unif_invert) / 8
};
LOCAL CONST UB *cp_rec[CP_NFS] = {
	cp_rec_piece, cp_rec_shadow, cp_rec_halve, cp_rec_smooth3, cp_rec_smooth5, cp_rec_double,
	cp_rec_moving, cp_rec_fill, cp_rec_copy, cp_rec_invert
};
LOCAL CONST UB *cp_recdi[CP_NFS] = {
	cp_recdi_piece, cp_recdi_shadow, cp_recdi_halve, cp_recdi_smooth3, cp_recdi_smooth5, cp_recdi_double,
	cp_recdi_moving, cp_recdi_fill, cp_recdi_copy, cp_recdi_invert
};

/* A uniform stream filled from the draw's parameters and buffers */
LOCAL UW *unif_fill( INT prog, CONST UW *par, CONST UW *buf )
{
	UINT	i, n = cp_unif_len[prog];
	UW	*u = (UW *)cmd_take(n * 4, 16);

	if ( u == NULL ) {
		return NULL;
	}
	for ( i = 0; i < n; i++ ) {
		UW	kind = cp_unif[prog][i][0], v = cp_unif[prog][i][1];

		if ( kind == CP_U_PARAM ) {
			v = par[v];
		} else if ( kind == CP_U_SSBO ) {
			v = buf[v];
		}
		u[i] = v;
	}
	return u;
}

/* The words a shader reads through the TMU for each pixel of its rectangle, for the counts */
LOCAL UD tmu_reads( INT fs )
{
	switch ( fs ) {
	case CP_FS_PIECE:	return 3;		/* the window, what shows through, the corner */
	case CP_FS_MOVING:	return 2;
	case CP_FS_SHADOW:
	case CP_FS_COPY:	return 1;
	case CP_FS_HALVE:	return 16;
	case CP_FS_SMOOTH3:	return 9;
	case CP_FS_SMOOTH5:	return 25;
	case CP_FS_DOUBLE:	return 4;
	default:		return 0;
	}
}

/*
 * One rectangle of the screen, or of a level of a spread, drawn into the
 * job's frame by fragment shader 'fs' with its parameters and buffers;
 * 'w' the window whose pixels it reads where it draws (a copy of them
 * may be loaded into the tiles instead). Where it shows and where it
 * lies in the frame -- its corners, and the frame's place among its
 * uniforms -- is worked out once the job's draws are all there
 * (job_plan, job_lists). A job's draws are one run of cg.draw; a draw
 * that does not fit makes the whole part go to the processor.
 */
LOCAL void draw( CJOB *j, INT fs, CONST T_DPRECT *r, UW *par, CONST UW *buf, CONST T_CPWIN *w )
{
	T_DPRECT	c = *r;
	DRAW		*dr;
	UW		*unif;
	UB		*rec;

	if ( c.left < j->fx ) c.left = j->fx;
	if ( c.top < j->fy ) c.top = j->fy;
	if ( c.right > j->fx + j->fw ) c.right = j->fx + j->fw;
	if ( c.bottom > j->fy + j->fh ) c.bottom = j->fy + j->fh;
	if ( c.right <= c.left || c.bottom <= c.top ) {
		return;
	}
	if ( j->ndraw >= JOB_DRAWS_MAX || cg.ndraw >= NDRAW
	  || ( j->ndraw > 0 && j->d0 + j->ndraw != cg.ndraw ) ) {
		cg.cmd_over = TRUE;
		return;
	}
	par[CP_P_ORGX] = 0;
	par[CP_P_ORGY] = 0;
	unif = unif_fill(2 + fs, par, buf);
	rec = cmd_take(CP_REC_LEN + CP_ATTR_LEN, 32);
	if ( unif == NULL || rec == NULL ) {
		return;
	}
	knl_memcpy(rec, cg.draw_index ? cp_recdi[fs] : cp_rec[fs], CP_REC_LEN);
	if ( cg.draw_index ) {
		*(UW *)( rec + CP_RECDI_FS_CODE ) += cg.prog[2 + fs];
		*(UW *)( rec + CP_RECDI_FS_UNIF ) += CMD_ADDR(unif);
		*(UW *)( rec + CP_RECDI_VS_CODE ) += cg.prog[1];
		*(UW *)( rec + CP_RECDI_VS_UNIF ) += cg.unif_vs;
		*(UW *)( rec + CP_RECDI_CS_CODE ) += cg.prog[0];
		*(UW *)( rec + CP_RECDI_CS_UNIF ) += cg.unif_cs;
	} else {
		*(UW *)( rec + CP_REC_FS_CODE ) += cg.prog[2 + fs];
		*(UW *)( rec + CP_REC_FS_UNIF ) += CMD_ADDR(unif);
		*(UW *)( rec + CP_REC_VS_CODE ) += cg.prog[1];
		*(UW *)( rec + CP_REC_VS_UNIF ) += cg.unif_vs;
		*(UW *)( rec + CP_REC_CS_CODE ) += cg.prog[0];
		*(UW *)( rec + CP_REC_CS_UNIF ) += cg.unif_cs;
	}
	knl_memcpy(rec + CP_REC_LEN, cp_attr, CP_ATTR_LEN);

	if ( j->ndraw == 0 ) {
		j->d0 = cg.ndraw;
	}
	dr = &cg.draw[cg.ndraw++];
	knl_memset(dr, 0, sizeof(*dr));
	dr->r = c;
	dr->rec = CMD_ADDR(rec);
	dr->recp = rec;
	dr->unif = unif;
	dr->fs = fs;
	dr->reads = tmu_reads(fs);
	if ( w != NULL && !CP_MOVING(w) ) {
		dr->win = w;
		if ( fs == CP_FS_COPY ) {
			dr->wok = TRUE;
			dr->wsrc = buf[CP_B_SRC];
			dr->wpitch = w->pitch;
			dr->wl = w->outer.left;
			dr->wt = w->outer.top;
		}
	}
	j->ndraw++;
}

/* ---------------------------------------------------------------- planning a job */

LOCAL INT align_down( INT v )
{
	return v & ~( ALIGN_PX - 1 );
}

/* What the processor's share of the ground keeps clear of: a rectangle of the screen */
LOCAL void keep_add( INT l, INT t, INT r, INT b )
{
	if ( cg.nkeep >= NKEEP ) {
		cg.keep_over = TRUE;
		return;
	}
	cg.keep[cg.nkeep].l = l;
	cg.keep[cg.nkeep].t = t;
	cg.keep[cg.nkeep].r = r;
	cg.keep[cg.nkeep].b = b;
	cg.nkeep++;
}

/*
 * A job planned (v3d_cp_split.h): its frame settled -- one loaded from
 * and stored to the back buffer cut to the bounds of its draws, its left
 * edge kept on ALIGN_PX, since the back buffer round them is already
 * what it is to be -- the frame's place in each draw's uniforms, what
 * shows of each draw, each supertile's class, and the windows its tiles
 * are loaded from. A window is loaded from only where its pixels lie at
 * a GPU address the frame's first pixel can be counted from, and up to
 * NVAR of them. 1 when planned, 0 when it has nothing to do, -1 when
 * the part does not fit.
 */
LOCAL INT job_plan( CJOB *j )
{
	DRAW	*d0 = &cg.draw[j->d0];
	CP_DR	*r0 = &cg.dr[j->d0];
	INT	i, k, n, s, v;

	if ( j->ndraw == 0 && !j->ground ) {
		return 0;
	}
	if ( j->fit ) {
		T_DPRECT	u = d0->r;

		for ( i = 1; i < j->ndraw; i++ ) {
			DRAW	*dr = &d0[i];

			if ( dr->r.left < u.left ) u.left = dr->r.left;
			if ( dr->r.top < u.top ) u.top = dr->r.top;
			if ( dr->r.right > u.right ) u.right = dr->r.right;
			if ( dr->r.bottom > u.bottom ) u.bottom = dr->r.bottom;
		}
		j->fx = align_down(u.left);
		j->fy = u.top;
		j->fw = u.right - j->fx;
		j->fh = u.bottom - u.top;
		j->load = j->store = j->back + (UW)j->fy * j->spitch + (UW)j->fx * 4;
		keep_add(j->fx, j->fy, j->fx + j->fw, j->fy + j->fh);
	}
	for ( i = 0; i < j->ndraw; i++ ) {
		DRAW	*dr = &d0[i];
		CP_DR	*rr = &r0[i];

		n = cp_unif_len[2 + dr->fs];
		for ( k = 0; k < n; k++ ) {
			if ( cp_unif[2 + dr->fs][k][0] != CP_U_PARAM ) continue;
			if ( cp_unif[2 + dr->fs][k][1] == CP_P_ORGX ) dr->unif[k] = (UW)j->fx;
			else if ( cp_unif[2 + dr->fs][k][1] == CP_P_ORGY ) dr->unif[k] = (UW)j->fy;
		}
		knl_memset(rr, 0, sizeof(*rr));
		rr->r.l = dr->r.left;
		rr->r.t = dr->r.top;
		rr->r.r = dr->r.right;
		rr->r.b = dr->r.bottom;
		rr->copy = ( dr->fs == CP_FS_COPY );
		rr->wok = dr->wok;
		rr->w = ( dr->fs == CP_FS_COPY ) ? CP_CW_COPY : CP_CW_SHADE;
	}
	cp_grid(&j->g, j->fx, j->fy, j->fw, j->fh, TILE, MAX_SUPERTILES);
	cg.npool = cp_cull(r0, j->ndraw, cg.pool, cg.npool, SUB_MAX, ( cg.feat & CF_CULL ) != 0);
	if ( cg.npool < 0 ) {
		return -1;
	}
	cp_classify(&j->g, r0, j->ndraw, cg.pool, ( cg.feat & ( CF_TILES | CF_WLOAD ) ) == ( CF_TILES | CF_WLOAD ),
		    j->cls, cg.base);

	/* the windows the tiles load from, counted from the frame's first pixel */
	j->nvar = 0;
	for ( s = 0; s < j->g.nx * j->g.ny; s++ ) {
		DRAW	*c;
		D	a;

		j->var[s] = 0;
		if ( j->cls[s] != CP_ST_WIN ) continue;
		c = &d0[cg.base[s]];
		a = (D)c->wsrc + (D)( j->fy - c->wt ) * (D)c->wpitch + (D)( j->fx - c->wl ) * 4;
		v = j->nvar;
		if ( a >= 0 && a <= (D)0xFFFFFFFFU ) {
			for ( v = 0; v < j->nvar && !( j->vaddr[v] == (UW)a && j->vpitch[v] == c->wpitch ); v++ );
		} else {
			v = NVAR;		/* not to be counted from the frame: loaded as it would be */
		}
		if ( v == j->nvar && v < NVAR ) {
			j->vaddr[v] = (UW)a;
			j->vpitch[v] = c->wpitch;
			j->vwin[v] = c->win;
			j->nvar++;
		}
		if ( v < j->nvar ) {
			j->var[s] = (UB)v;
		} else {
			j->cls[s] = CP_ST_LOAD;
			cg.base[s] = -1;
		}
	}
	cg.npool = cp_cut_bases(&j->g, r0, j->ndraw, cg.pool, cg.npool, SUB_MAX, j->cls, cg.base);
	return 1;
}

/* ---------------------------------------------------------------- jobs */

/* A new job over a frame, or NULL when there is no room for another */
LOCAL CJOB *job_open( INT fx, INT fy, INT fw, INT fh, UW load, UINT lpitch, UW store, UINT spitch )
{
	CJOB	*j;

	if ( cg.njob >= NJOB || fw <= 0 || fh <= 0 ) {
		cg.cmd_over = TRUE;
		return NULL;
	}
	j = &cg.job[cg.njob++];
	j->fx = fx;
	j->fy = fy;
	j->fw = fw;
	j->fh = fh;
	j->load = load;
	j->lpitch = lpitch;
	j->store = store;
	j->spitch = spitch;
	j->ground = FALSE;
	j->fit = FALSE;
	j->planned = FALSE;
	j->back = 0;
	j->d0 = cg.ndraw;
	j->ndraw = 0;
	j->nvar = 0;
	return j;
}

/* What every tile of a kind does: loaded from 'load' (0: nothing), its draws, stored as the job stores */
LOCAL UW gtl_make( CL *gtl, CONST CJOB *j, UW load, UINT lpitch, UW *p_end )
{
	UW	start = cl_addr(gtl);

	EMIT(gtl, TILE_COORDINATES_IMPLICIT);
	if ( load != 0 ) {
		EMIT(gtl, LOAD_TILE_BUFFER_GENERAL,
		     .buffer_to_load = RENDER_TARGET_0,
		     .address = load,
		     .input_image_format = V3D_OUTPUT_IMAGE_FORMAT_RGBA8,
		     .memory_format = V3D_MEMORY_FORMAT_RASTER,
		     .height_in_ub_or_stride = lpitch,
		     .decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0);
	}
	EMIT(gtl, END_OF_LOADS);
	EMIT(gtl, PRIM_LIST_FORMAT, .primitive_type = LIST_TRIANGLES);
	EMIT(gtl, SET_INSTANCEID, .instance_id = 0);
	EMIT(gtl, BRANCH_TO_IMPLICIT_TILE_LIST);
	if ( j->store != 0 ) {
		EMIT(gtl, STORE_TILE_BUFFER_GENERAL,
		     .buffer_to_store = RENDER_TARGET_0,
		     .address = j->store,
		     .clear_buffer_being_stored = false,
		     .output_image_format = V3D_OUTPUT_IMAGE_FORMAT_RGBA8,
		     .memory_format = V3D_MEMORY_FORMAT_RASTER,
		     .height_in_ub_or_stride = j->spitch,
		     .decimate_mode = V3D_DECIMATE_MODE_SAMPLE_0);
	} else {
		EMIT(gtl, STORE_TILE_BUFFER_GENERAL, .buffer_to_store = NONE);
	}
	EMIT(gtl, END_OF_TILE_MARKER);
	EMIT(gtl, RETURN_FROM_SUB_LIST);
	*p_end = cl_addr(gtl);
	return start;
}

/*
 * The lists of a planned job: the binner sets up the tiles and bins
 * each draw as two triangles for each rectangle of what shows of it;
 * the renderer runs, for each supertile rendered, the generic tile list
 * of its kind -- loaded from what the job loads, or from a window's
 * pixels -- switching lists only where the kind changes. Filled into
 * T_GPUJOB. 1 when made, 0 when no supertile is rendered, -1 when it
 * does not fit.
 */
LOCAL INT job_lists( CJOB *j, T_GPUJOB *gj )
{
	CP_GRID	*g = &j->g;
	UINT	nst = (UINT)( g->nx * g->ny ), i, k, s, nrend = 0, cur;
	UW	ta, gl[NVAR + 1], ge[NVAR + 1];
	CL	bcl, rcl, gtl;
	UB	*zs;
	UW	one = 0x3F800000U;		/* 1.0f, the depth clear value */
	BOOL	tmuw = FALSE;
	CP_SR	st;

	for ( s = 0; s < nst; s++ ) {
		if ( j->cls[s] == CP_ST_LOAD || j->cls[s] == CP_ST_WIN ) nrend++;
	}
	if ( nrend == 0 ) {
		return 0;
	}
	ta = ( ( (UW)( g->tx * g->ty ) * TILE_ALLOC_BLOCK + 4095 ) & ~4095U ) + 8192 + TILE_ALLOC_EXTRA;
	if ( ta > cg.ta_bytes || (UD)g->tx * g->ty * TILE_STATE_BYTES > cg.ts_bytes ) {
		return -1;			/* larger than the tile memory taken at the start */
	}

	if ( !cl_open(&bcl, 256 + sizeof(cp_state) + (UW)j->ndraw * 32)
	  || !cl_open(&rcl, 512 + nst * 12)
	  || !cl_open(&gtl, 128 * ( 1 + j->nvar )) ) {
		return -1;
	}

	/* the binner */
	EMIT(&bcl, NUMBER_OF_LAYERS, .number_of_layers = 1);
	EMIT(&bcl, TILE_BINNING_MODE_CFG,
	     .width_in_pixels = (UW)j->fw,
	     .height_in_pixels = (UW)j->fh,
	     .log2_tile_width = LOG2_TILE_ENUM,
	     .log2_tile_height = LOG2_TILE_ENUM,
	     .tile_allocation_initial_block_size = TILE_ALLOC_BLOCK_ENUM,
	     .tile_allocation_block_size = TILE_ALLOC_MORE_ENUM);
	EMIT(&bcl, FLUSH_VCD_CACHE);
	EMIT(&bcl, OCCLUSION_QUERY_COUNTER, .address = 0);
	EMIT(&bcl, START_TILE_BINNING);
	cl_bytes(&bcl, cp_state, sizeof(cp_state));
	EMIT(&bcl, CLIP_WINDOW,
	     .clip_window_left_pixel_coordinate = 0,
	     .clip_window_bottom_pixel_coordinate = 0,
	     .clip_window_width_in_pixels = (UW)j->fw,
	     .clip_window_height_in_pixels = (UW)j->fh);
	for ( i = 0; i < (UINT)j->ndraw; i++ ) {
		DRAW	*dr = &cg.draw[j->d0 + i];
		CP_DR	*rr = &cg.dr[j->d0 + i];
		UD	area = (UD)( dr->r.right - dr->r.left ) * (UD)( dr->r.bottom - dr->r.top ), shown = 0;
		UW	*v;

		if ( ( CP_FS_TMU_WRITES & ( 1U << dr->fs ) ) != 0 ) tmuw = TRUE;
		if ( rr->ns == 0 ) {
			cg.ph.px_out += area;
			continue;
		}
		/* each rectangle two triangles: top left, top right, bottom left; top right, bottom right, bottom left */
		v = (UW *)cmd_take((UD)rr->ns * 12 * 4, 16);
		if ( v == NULL ) {
			return -1;
		}
		for ( k = 0; k < (UINT)rr->ns; k++ ) {
			CONST CP_SR	*q = &cg.pool[rr->s0 + k];
			UW		l = fclip(q->l - j->fx), t = fclip(q->t - j->fy);
			UW		r = fclip(q->r - j->fx), b = fclip(q->b - j->fy);
			UW		*p = v + k * 12;

			p[0] = l;  p[1] = t;   p[2] = r;  p[3] = t;   p[4] = l;   p[5] = b;
			p[6] = r;  p[7] = t;   p[8] = r;  p[9] = b;   p[10] = l;  p[11] = b;
			shown += (UD)( q->r - q->l ) * (UD)( q->b - q->t );
		}
		*(UW *)( dr->recp + CP_REC_LEN + CP_ATTR_ADDR ) += CMD_ADDR(v);
		cl_bytes(&bcl, cp_vcm, sizeof(cp_vcm));
		EMIT(&bcl, GL_SHADER_STATE, .address = dr->rec, .number_of_attribute_arrays = 1);
		EMIT(&bcl, VERTEX_ARRAY_PRIMS, .mode = V3D_PRIM_TRIANGLES, .length = (UW)rr->ns * 6,
		     .index_of_first_vertex = 0);

		cg.ph.ndraw++;
		cg.ph.px += shown;
		cg.ph.px_out += ( area > shown ) ? area - shown : 0;
		cg.ph.tmu += shown * dr->reads;
		if ( dr->fs == CP_FS_COPY ) {
			cg.ph.px_copy += shown;
		}
	}
	EMIT(&bcl, FLUSH);

	/* the renderer: the frame and its one render target, 8 bit RGBA */
	EMIT(&rcl, TILE_RENDERING_MODE_CFG_COMMON,
	     .early_z_disable = true,
	     .image_width_pixels = (UW)j->fw,
	     .image_height_pixels = (UW)j->fh,
	     .number_of_render_targets = 1,
	     .log2_tile_width = LOG2_TILE_ENUM,
	     .log2_tile_height = LOG2_TILE_ENUM,
	     .internal_depth_type = V3D_INTERNAL_TYPE_DEPTH_32F);
	EMIT(&rcl, TILE_RENDERING_MODE_CFG_RENDER_TARGET_PART1,
	     .clear_color_low_bits = 0,
	     .internal_bpp = V3D_INTERNAL_BPP_32,
	     .internal_type_and_clamping = V3D_RENDER_TARGET_TYPE_CLAMP_8,
	     .stride = CP_RT_STRIDE,
	     .base_address = 0,
	     .render_target_number = 0);
	zs = cl_space(&rcl, V3D71_TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES_length);
	if ( zs != NULL ) {
		struct V3D71_TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES c = {
			V3D71_TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES_header, .stencil_clear_value = 0 };

		V3D71_TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES_pack(NULL, zs, &c);
		knl_memcpy(zs + 3, &one, 4);		/* the float field, packed by hand */
	}
	EMIT(&rcl, TILE_LIST_INITIAL_BLOCK_SIZE,
	     .use_auto_chained_tile_lists = true,
	     .size_of_first_block_in_chained_tile_lists = TILE_ALLOC_BLOCK_ENUM);
	EMIT(&rcl, MULTICORE_RENDERING_TILE_LIST_SET_BASE, .address = bo_gpu(cg.tile_alloc));
	EMIT(&rcl, MULTICORE_RENDERING_SUPERTILE_CFG,
	     .number_of_bin_tile_lists = 1,
	     .total_frame_width_in_tiles = (UW)g->tx,
	     .total_frame_height_in_tiles = (UW)g->ty,
	     .supertile_width_in_tiles = (UW)g->sw,
	     .supertile_height_in_tiles = (UW)g->sh,
	     .total_frame_width_in_supertiles = (UW)g->nx,
	     .total_frame_height_in_supertiles = (UW)g->ny);
	for ( i = 0; i < 2; i++ ) {			/* two tiles first, the first clearing */
		EMIT(&rcl, TILE_COORDINATES, .tile_row_number = 0, .tile_column_number = 0);
		EMIT(&rcl, END_OF_LOADS);
		EMIT(&rcl, STORE_TILE_BUFFER_GENERAL, .buffer_to_store = NONE);
		if ( i == 0 ) {
			EMIT(&rcl, CLEAR_RENDER_TARGETS);
		}
		EMIT(&rcl, END_OF_TILE_MARKER);
	}
	EMIT(&rcl, FLUSH_VCD_CACHE);

	/* what the tiles of each kind do, then the supertiles, each with its kind's list */
	gl[0] = gtl_make(&gtl, j, j->load, j->lpitch, &ge[0]);
	for ( i = 0; i < (UINT)j->nvar; i++ ) {
		gl[1 + i] = gtl_make(&gtl, j, j->vaddr[i], j->vpitch[i], &ge[1 + i]);
	}
	cur = ~0U;
	for ( s = 0; s < nst; s++ ) {
		UINT	kind, ntile;

		if ( j->cls[s] != CP_ST_LOAD && j->cls[s] != CP_ST_WIN ) {
			if ( j->cls[s] != CP_ST_CPU ) {
				cp_grid_at(g, (INT)s, &st);
				cg.ph.ntile_out += (UD)( ( st.r - st.l + TILE - 1 ) / TILE ) * (UD)( ( st.b - st.t + TILE - 1 ) / TILE );
			}
			continue;
		}
		kind = ( j->cls[s] == CP_ST_WIN ) ? 1U + j->var[s] : 0U;
		if ( kind != cur ) {
			EMIT(&rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, .start = gl[kind], .end = ge[kind]);
			cur = kind;
		}
		EMIT(&rcl, SUPERTILE_COORDINATES,
		     .row_number_in_supertiles = s / (UINT)g->nx,
		     .column_number_in_supertiles = s % (UINT)g->nx);
		cp_grid_at(g, (INT)s, &st);
		ntile = (UINT)( ( st.r - st.l + TILE - 1 ) / TILE ) * (UINT)( ( st.b - st.t + TILE - 1 ) / TILE );
		cg.ph.ntile += ntile;
		if ( kind != 0 ) cg.ph.ntile_win += ntile;
	}
	EMIT(&rcl, END_OF_RENDERING);

	if ( bcl.over || rcl.over || gtl.over ) {
		return -1;
	}
	knl_memset(gj, 0, sizeof(*gj));
	gj->kind = GPU_JOB_CL;
	gj->u.cl.bcl_start = CMD_ADDR(bcl.buf);
	gj->u.cl.bcl_end = CMD_ADDR(bcl.buf + bcl.len);
	gj->u.cl.rcl_start = CMD_ADDR(rcl.buf);
	gj->u.cl.rcl_end = CMD_ADDR(rcl.buf + rcl.len);
	gj->u.cl.qma = bo_gpu(cg.tile_alloc);
	gj->u.cl.qms = ta;
	gj->u.cl.qts = bo_gpu(cg.tile_state);
	/*
	 * The GPU's caches cleaned after a job only when its shaders wrote
	 * memory through the TMU, which none of them does now: what is in
	 * the TMU's cache then is written out before the next job drops it.
	 * The tiles go to memory from the tile buffer, not through it, and
	 * what a later job reads of them is fresh, the driver dropping the
	 * cache before each step.
	 */
	gj->u.cl.flags = tmuw ? GPU_CL_FLUSH_CACHE : 0;
	return 1;
}

/* ---------------------------------------------------------------- the stages as draws */

LOCAL BOOL cut( T_DPRECT *r, CONST T_DPRECT *by )
{
	if ( by->left   > r->left )   r->left   = by->left;
	if ( by->top    > r->top )    r->top    = by->top;
	if ( by->right  < r->right )  r->right  = by->right;
	if ( by->bottom < r->bottom ) r->bottom = by->bottom;

	return ( r->left < r->right && r->top < r->bottom );
}

/* The corner of radius rad in the lists for this part; 0 (zeroes) for none */
LOCAL UW cov_of( INT rad )
{
	CONST UB	*t;
	UB		*p;

	if ( rad <= 0 || rad > CP_ROUND_MAX ) {
		return cg.cmd_gpu;
	}
	if ( cg.cov_at[rad] != 0 ) {
		return cg.cov_at[rad];
	}
	t = cp_corner_cov(rad);
	p = cmd_take((UD)( rad * rad + 3 ) & ~3UL, 16);
	if ( t == NULL || p == NULL ) {
		return cg.cmd_gpu;
	}
	knl_memcpy(p, t, (SZ)( rad * rad ));
	cg.cov_at[rad] = CMD_ADDR(p);
	return cg.cov_at[rad];
}

LOCAL INT round_of( CONST T_CPWIN *w, CONST T_DPRECT *r )
{
	INT	rad = ( w->shape == NULL ) ? cp_round_of(r, w->round) : 0;

	return ( cp_corner_cov(rad) == NULL ) ? 0 : rad;
}

typedef struct {
	CONST T_CPDST	*d;
	UW		back;		/* GPU address of the back buffer's first pixel */
	UW		t1, t2;		/* and of T1's and T2's */
	UW		dummy;		/* a buffer the size of the screen, read where nothing is meant */
	UW		ground;
	UINT		gpitch;
} PART;

/*
 * The current job, or a new one over the box loaded from the back
 * buffer, cut to its draws once they are all there (job_plan)
 */
LOCAL CJOB *seg( PART *pt )
{
	CONST T_DPRECT	*b = &pt->d->box;
	UINT		pitch = pt->d->pitch;
	UW		at = pt->back + (UW)b->top * pitch + (UW)b->left * 4;

	if ( cg.cur != NULL && cg.cur->ndraw < JOB_DRAWS_MAX ) {
		return cg.cur;
	}
	cg.cur = job_open(b->left, b->top, b->right - b->left, b->bottom - b->top, at, pitch, at, pitch);
	if ( cg.cur != NULL ) {
		cg.cur->fit = TRUE;
		cg.cur->back = pt->back;
	}
	return cg.cur;
}

/* A rectangle drawn in the current job, nothing when it is outside the box; w the window it reads, or NULL */
LOCAL BOOL lay_rect( PART *pt, INT fs, INT l, INT t, INT r, INT b, UW *par, CONST UW *buf, CONST T_CPWIN *w )
{
	T_DPRECT	c;
	CJOB		*j;

	c.left = l;  c.top = t;  c.right = r;  c.bottom = b;
	if ( !cut(&c, &pt->d->box) ) {
		return TRUE;
	}
	j = seg(pt);
	if ( j == NULL ) {
		return FALSE;
	}
	draw(j, fs, &c, par, buf, w);
	return TRUE;
}

/* Where level k (1 .. CP_BLUR_DEPTH) of a spread is kept: in T1 (v3d_cp_split.h cp_lv_row) */
LOCAL UW lv_at( CONST PART *pt, INT k )
{
	return pt->t1 + (UW)cp_lv_row(pt->d->rows, k) * pt->d->pitch;
}

/*
 * One step of a spread: a job whose frame is the rectangles r[0..n-1]
 * of a level, its left edge on ALIGN_PX, stored at 'to' (the level's
 * pixel (0, 0)), each pixel drawn by 'fs' reading the level at 'from',
 * held to its pixels that touch the box, v. What the frame holds
 * besides the rectangles is stored too, where nothing reads it.
 */
LOCAL BOOL spread_step( INT fs, UW from, UW to, UINT pitch, CONST T_DPRECT *v, CONST T_DPRECT *r, INT n,
			UW *par, UW *buf )
{
	T_DPRECT	u = r[0];
	CJOB		*j;
	INT		i;

	for ( i = 1; i < n; i++ ) {
		if ( r[i].left < u.left ) u.left = r[i].left;
		if ( r[i].top < u.top ) u.top = r[i].top;
		if ( r[i].right > u.right ) u.right = r[i].right;
		if ( r[i].bottom > u.bottom ) u.bottom = r[i].bottom;
	}
	u.left = align_down(u.left);
	j = job_open(u.left, u.top, u.right - u.left, u.bottom - u.top, 0, 0,
		     to + (UW)u.top * pitch + (UW)u.left * 4, pitch);
	if ( j == NULL ) {
		return FALSE;
	}
	par[CP_PL_VL] = (UW)v->left;
	par[CP_PL_VT] = (UW)v->top;
	par[CP_PL_VR] = (UW)v->right;
	par[CP_PL_VB] = (UW)v->bottom;
	buf[CP_B_SRC] = from;
	for ( i = 0; i < n; i++ ) {
		draw(j, fs, &r[i], par, buf, NULL);
	}
	return TRUE;
}

/*
 * What shows through the parts of a window that are not solid, spread
 * into T2 where the screen's pixels are (compose.c cp_spread_plan), a
 * job for each step: halved from the back buffer as it stands, level by
 * level into T1; the deepest level smoothed into T2 below the screen's
 * rows; doubled back level by level into T1, over the levels halved,
 * which nothing reads any more, and the last into T2. What the first
 * step reads of the back buffer is kept clear of the processor's share.
 */
LOCAL BOOL spread_jobs( PART *pt, CONST T_CPPIECE *pc, INT npc, INT rad )
{
	CONST T_CPDST	*d = pt->d;
	T_CPSPREAD	sp[CP_NPIECE];
	T_DPRECT	r[CP_NPIECE];
	UINT		pitch = d->pitch;
	UW		par[CP_NPARAM], buf[CP_NBUF], below = pt->t2 + (UW)d->rows * pitch;
	INT		i, k, n = 0, depth;

	for ( i = 0; i < npc; i++ ) {
		if ( pc[i].a < 255 && cp_spread_plan(&d->box, &pc[i].r, rad, &sp[n]) ) {
			keep_add(sp[n].down[0].left, sp[n].down[0].top, sp[n].down[0].right, sp[n].down[0].bottom);
			n++;
		}
	}
	if ( n == 0 ) {
		return TRUE;
	}
	depth = sp[0].depth;
	knl_memset(par, 0, sizeof(par));
	par[CP_PL_PITCH] = pitch;
	par[CP_PL_A] = (UW)sp[0].w[0];
	par[CP_PL_B] = (UW)sp[0].w[1];
	par[CP_PL_M] = (UW)sp[0].w[2];
	buf[CP_B_AUX] = pt->dummy;
	buf[CP_B_COV] = cg.cmd_gpu;

	for ( k = 1; k <= depth; k++ ) {
		for ( i = 0; i < n; i++ ) r[i] = sp[i].down[k];
		if ( !spread_step(CP_FS_HALVE, ( k == 1 ) ? pt->back : lv_at(pt, k - 1), lv_at(pt, k), pitch,
				  &sp[0].valid[k - 1], r, n, par, buf) ) {
			return FALSE;
		}
	}
	for ( i = 0; i < n; i++ ) r[i] = sp[i].up[depth];
	if ( !spread_step(( sp[0].w[0] != 0 ) ? CP_FS_SMOOTH5 : CP_FS_SMOOTH3,
			  ( depth == 0 ) ? pt->back : lv_at(pt, depth), ( depth == 0 ) ? pt->t2 : below, pitch,
			  &sp[0].valid[depth], r, n, par, buf) ) {
		return FALSE;
	}
	for ( k = depth; k >= 1; k-- ) {
		for ( i = 0; i < n; i++ ) r[i] = sp[i].up[k - 1];
		if ( !spread_step(CP_FS_DOUBLE, ( k == depth ) ? below : lv_at(pt, k), ( k == 1 ) ? pt->t2 : lv_at(pt, k - 1),
				  pitch, &sp[0].valid[k], r, n, par, buf) ) {
			return FALSE;
		}
	}
	return TRUE;
}

/* A window opening or closing (cp_moving) */
LOCAL BOOL lay_moving( PART *pt, CONST T_CPWIN *w, UW src )
{
	T_DPRECT	v = w->shown;
	INT		ow = w->outer.right - w->outer.left, oh = w->outer.bottom - w->outer.top;
	INT		sw = w->shown.right - w->shown.left, sh = w->shown.bottom - w->shown.top;
	UW		par[CP_NPARAM], buf[CP_NBUF];
	INT		rad;

	if ( sw <= 0 || sh <= 0 || ow <= 0 || oh <= 0 || !cut(&v, &pt->d->box) ) {
		return TRUE;
	}
	rad = cp_round_of(&w->shown, w->round);
	if ( cp_corner_cov(rad) == NULL ) rad = 0;
	knl_memset(par, 0, sizeof(par));
	par[CP_PM_PITCH] = w->pitch;
	par[CP_PM_SL] = (UW)w->shown.left;
	par[CP_PM_ST] = (UW)w->shown.top;
	par[CP_PM_SR] = (UW)w->shown.right;
	par[CP_PM_SB] = (UW)w->shown.bottom;
	par[CP_PM_STEPX] = ( (UW)ow << 16 ) / (UW)sw;
	par[CP_PM_STEPY] = ( (UW)oh << 16 ) / (UW)sh;
	par[CP_PM_OL] = (UW)w->outer.left;
	par[CP_PM_OT] = (UW)w->outer.top;
	par[CP_PM_OR] = (UW)w->outer.right;
	par[CP_PM_OB] = (UW)w->outer.bottom;
	par[CP_PM_WL] = (UW)w->work.left;
	par[CP_PM_WT] = (UW)w->work.top;
	par[CP_PM_WR] = (UW)w->work.right;
	par[CP_PM_WB] = (UW)w->work.bottom;
	par[CP_PM_AW] = w->a_work * w->fade / 255U;
	par[CP_PM_AF] = w->a_frame * w->fade / 255U;
	par[CP_PM_RAD] = (UW)rad;
	par[CP_PM_TINT] = w->tint;
	par[CP_PM_TSTR] = w->tint_str;
	par[CP_PM_WASH] = w->wash;
	par[CP_PM_WSTR] = w->wash_str;
	par[CP_PM_BWASH] = w->bar_wash;
	par[CP_PM_BSTR] = w->bar_str;
	par[CP_PM_BAND] = ( w->band.right > w->band.left ) ? 1 : 0;
	buf[CP_B_SRC] = src;
	buf[CP_B_AUX] = pt->dummy;
	buf[CP_B_COV] = cov_of(rad);
	return lay_rect(pt, CP_FS_MOVING, v.left, v.top, v.right, v.bottom, par, buf, NULL);
}

/* A window in its stages: shadow, spread, pieces */
LOCAL BOOL lay_window( PART *pt, CONST T_CPWIN *w, UW src )
{
	CONST T_CPDST	*d = pt->d;
	T_CPPIECE	pc[CP_NPIECE];
	T_DPRECT	e;
	UW		par[CP_NPARAM], cpar[CP_NPARAM], buf[CP_NBUF];
	CP_SR		so, sp, sr[CP_SPLIT_PIECE_MAX + CP_SPLIT_SHADOW_MAX];
	INT		npc, rad, brad, i, k, n, sfs[CP_SPLIT_PIECE_MAX];
	BOOL		spread = FALSE;

	rad = round_of(w, &w->outer);
	buf[CP_B_SRC] = src;
	buf[CP_B_AUX] = pt->dummy;
	buf[CP_B_COV] = cov_of(rad);
	so.l = w->outer.left;   so.t = w->outer.top;
	so.r = w->outer.right;  so.b = w->outer.bottom;

	/* 1. the shadow, round the window */
	if ( cp_shadow_extent(w, &e) && cut(&e, &d->box) ) {
		CP_SR	se;

		knl_memset(par, 0, sizeof(par));
		par[CP_PS_OL] = (UW)w->outer.left;
		par[CP_PS_OT] = (UW)w->outer.top;
		par[CP_PS_OR] = (UW)w->outer.right;
		par[CP_PS_OB] = (UW)w->outer.bottom;
		par[CP_PS_DX] = (UW)w->shd_dx;
		par[CP_PS_DY] = (UW)w->shd_dy;
		par[CP_PS_SIZE] = (UW)w->shd_size;
		par[CP_PS_DARK] = w->shd_dark;
		par[CP_PS_RAD] = (UW)rad;
		se.l = e.left;   se.t = e.top;
		se.r = e.right;  se.b = e.bottom;
		n = cp_split_shadow(&se, &so, rad, sr);
		for ( k = 0; k < n; k++ ) {
			if ( !lay_rect(pt, CP_FS_SHADOW, sr[k].l, sr[k].t, sr[k].r, sr[k].b, par, buf, NULL) ) {
				return FALSE;
			}
		}
	}

	/* 2. what shows through, spread: what is behind has to be in memory first */
	npc = cp_pieces(d, w, pc);
	brad = ( w->blur > CP_BLUR_MAX ) ? CP_BLUR_MAX : w->blur;
	if ( brad > 0 ) {
		for ( i = 0; i < npc; i++ ) {
			if ( pc[i].a < 255 ) spread = TRUE;
		}
	}
	if ( spread ) {
		if ( pt->t1 == 0 ) return FALSE;
		cg.cur = NULL;			/* the frame so far is stored */
		if ( !spread_jobs(pt, pc, npc, brad) ) return FALSE;
		buf[CP_B_AUX] = pt->t2;
	}

	/* 3 and 4. the pieces, washed; where one is solid and plain, the window's pixels as they are */
	knl_memset(cpar, 0, sizeof(cpar));
	cpar[CP_PD_PITCH] = w->pitch;
	cpar[CP_PD_OL] = (UW)w->outer.left;
	cpar[CP_PD_OT] = (UW)w->outer.top;
	cpar[CP_PD_W] = (UW)( w->outer.right - w->outer.left );
	cpar[CP_PD_H] = (UW)( w->outer.bottom - w->outer.top );
	for ( i = 0; i < npc; i++ ) {
		UINT	tstr = pc[i].frame ? w->tint_str : 0;
		BOOL	plain = ( pc[i].a >= 255 && pc[i].wstr == 0 && tstr == 0 );

		knl_memset(par, 0, sizeof(par));
		par[CP_PP_PITCH] = w->pitch;
		par[CP_PP_OL] = (UW)w->outer.left;
		par[CP_PP_OT] = (UW)w->outer.top;
		par[CP_PP_OR] = (UW)w->outer.right;
		par[CP_PP_OB] = (UW)w->outer.bottom;
		par[CP_PP_A] = pc[i].a;
		par[CP_PP_RAD] = (UW)rad;
		par[CP_PP_TINT] = w->tint;
		par[CP_PP_TSTR] = tstr;
		par[CP_PP_WASH] = pc[i].wcol;
		par[CP_PP_WSTR] = pc[i].wstr;
		par[CP_PP_UND] = ( spread && pc[i].a < 255 ) ? 1 : 0;
		par[CP_PP_UPITCH] = d->pitch;
		par[CP_PP_SW] = d->pitch / 4;
		par[CP_PP_SH] = (UW)d->rows;
		sp.l = pc[i].r.left;   sp.t = pc[i].r.top;
		sp.r = pc[i].r.right;  sp.b = pc[i].r.bottom;
		n = cp_split_piece(&sp, plain, &so, rad, sr, sfs);
		for ( k = 0; k < n; k++ ) {
			if ( !lay_rect(pt, sfs[k], sr[k].l, sr[k].t, sr[k].r, sr[k].b,
				       ( sfs[k] == CP_FS_COPY ) ? cpar : par, buf, w) ) {
				return FALSE;
			}
		}
	}
	return TRUE;
}

/* ---------------------------------------------------------------- the caches */

/* A rectangle of a window's pixels cleaned from the caches, cut to where the window stands */
LOCAL void win_rect_clean( CONST T_CPWIN *w, INT l, INT t, INT r, INT b )
{
	if ( l < w->outer.left ) l = w->outer.left;
	if ( t < w->outer.top ) t = w->outer.top;
	if ( r > w->outer.right ) r = w->outer.right;
	if ( b > w->outer.bottom ) b = w->outer.bottom;
	dc_rows(w->pixels, w->pitch, l - w->outer.left, r - w->outer.left, t - w->outer.top, b - w->outer.top,
		OP_CLEAN);
}

/*
 * The processor's caches cleaned of what the GPU reads of the windows,
 * between the caller's barriers: of a window opening or closing, all of
 * its pixels, since it is laid scaled; of the rest, what shows of each
 * draw that reads them, and the supertiles loaded from them. A helper
 * pixel of a 2 x 2 quad may read next to that, but what it reads is
 * not used.
 */
LOCAL void wins_clean( CONST T_CPWIN *w, INT nlaid )
{
	INT	i, k, s;

	for ( i = 0; i < nlaid; i++ ) {
		if ( CP_MOVING(&w[i]) ) {
			dc_lines((UBINT)w[i].pixels, (UBINT)w[i].pitch
				 * (UBINT)( w[i].outer.bottom - w[i].outer.top ), OP_CLEAN);
		}
	}
	for ( i = 0; i < cg.njob; i++ ) {
		CJOB	*j = &cg.job[i];

		if ( !j->planned ) continue;
		for ( k = 0; k < j->ndraw; k++ ) {
			DRAW	*dr = &cg.draw[j->d0 + k];
			CP_DR	*rr = &cg.dr[j->d0 + k];
			INT	m;

			if ( dr->win == NULL ) continue;
			for ( m = 0; m < rr->ns; m++ ) {
				CONST CP_SR	*q = &cg.pool[rr->s0 + m];

				win_rect_clean(dr->win, q->l, q->t, q->r, q->b);
			}
		}
		for ( s = 0; s < j->g.nx * j->g.ny; s++ ) {
			CP_SR	st;

			if ( j->cls[s] != CP_ST_WIN ) continue;
			cp_grid_at(&j->g, s, &st);
			win_rect_clean(j->vwin[j->var[s]], st.l, st.t, st.r, st.b);
		}
	}
}

/*
 * The rows of the back buffer the GPU writes, the line operation 'op'
 * over them between the caller's barriers: the part, but for the
 * processor's share of the ground, run by run along each row of the
 * first job's supertiles.
 */
LOCAL void back_rows( CONST T_CPDST *d, INT op )
{
	CJOB	*j = &cg.job[0];
	CP_SR	a, z;
	INT	x, y, x2, n;

	if ( cg.njob == 0 || !j->ground || !j->planned ) {
		dc_rows(d->back, d->pitch, d->box.left, d->box.right, d->box.top, d->box.bottom, op);
		return;
	}
	n = j->g.nx;
	for ( y = 0; y < j->g.ny; y++ ) {
		for ( x = 0; x < n; x = x2 ) {
			x2 = x + 1;
			if ( j->cls[y * n + x] == CP_ST_CPU ) continue;
			while ( x2 < n && j->cls[y * n + x2] != CP_ST_CPU ) x2++;
			cp_grid_at(&j->g, y * n + x, &a);
			cp_grid_at(&j->g, y * n + x2 - 1, &z);
			dc_rows(d->back, d->pitch, a.l, z.r, a.t, a.b, op);
		}
	}
}

/* The processor's share of the ground copied into the back buffer, run by run along each row of supertiles */
LOCAL void cpu_share( void )
{
	CJOB	*j = &cg.job[0];
	CP_SR	a, z;
	INT	x, y, x2, n, row;
	UD	t0;

	if ( cg.njob == 0 || !j->ground || !j->planned || cg.p_ground == NULL ) {
		return;
	}
	t0 = ticks();
	n = j->g.nx;
	for ( y = 0; y < j->g.ny; y++ ) {
		for ( x = 0; x < n; x = x2 ) {
			x2 = x + 1;
			if ( j->cls[y * n + x] != CP_ST_CPU ) continue;
			while ( x2 < n && j->cls[y * n + x2] == CP_ST_CPU ) x2++;
			cp_grid_at(&j->g, y * n + x, &a);
			cp_grid_at(&j->g, y * n + x2 - 1, &z);
			for ( row = a.t; row < a.b; row++ ) {
				knl_memcpy(cg.p_back + (UBINT)row * cg.p_pitch + (UBINT)a.l * 4,
					   (CONST UB *)cg.p_ground + (UBINT)row * cg.p_gpitch + (UBINT)a.l * 4,
					   (SZ)( z.r - a.l ) * 4);
			}
			cg.ph.cpu_px += (UD)( z.r - a.l ) * (UD)( a.b - a.t );
		}
	}
	cg.ph.cpu += ticks() - t0;
}

/* ---------------------------------------------------------------- a part's jobs */

LOCAL void jbo_add( T_GPUBO *bo )
{
	INT	i;

	if ( bo == NULL ) return;
	for ( i = 0; i < cg.njbo; i++ ) {
		if ( cg.jbo[i] == bo ) return;
	}
	if ( cg.njbo < NJOBBO ) cg.jbo[cg.njbo++] = bo;
}

/* The BO of a GPU address entered by wrap_of */
LOCAL T_GPUBO *wrap_bo( UW gpu )
{
	INT	i;

	for ( i = 0; i < NWRAP; i++ ) {
		WRAP	*w = &cg.wrap[i];

		if ( w->va != 0 && gpu >= w->gpu && gpu < w->gpu + ( (UW)w->npages << PAGE_SHIFT ) ) {
			return w->bo;
		}
	}
	return NULL;
}

/* T1 and T2, 'want' bytes each (v3d_cp_split.h cp_spread_bytes) */
LOCAL BOOL spread_alloc( UD want )
{
	ER	er = E_OK;

	if ( cg.t1 != NULL && cg.t_bytes >= want ) {
		return TRUE;
	}
	if ( cg.t1 != NULL ) knl_gpu_bo_put(cg.t1);
	if ( cg.t2 != NULL ) knl_gpu_bo_put(cg.t2);
	cg.t1 = knl_gpu_bo_create((SZ)want, 0, &er);
	cg.t2 = ( cg.t1 != NULL ) ? knl_gpu_bo_create((SZ)want, 0, &er) : NULL;
	if ( cg.t2 == NULL ) {
		if ( cg.t1 != NULL ) knl_gpu_bo_put(cg.t1);
		cg.t1 = NULL;
		cg.t_bytes = 0;
		tm_printf((UB *)"v3d compose: no memory to spread what shows through (%d); laid on the processor\n", er);
		return FALSE;
	}
	cg.t_bytes = want;
	return TRUE;
}

/*
 * Every job planned, the first job's free supertiles shared with the
 * processor as far as the rest of the GPU's work allows, and the lists
 * made, into cg.gj; FALSE when they do not fit.
 */
LOCAL BOOL jobs_make( void )
{
	CJOB	*j0 = &cg.job[0];
	UD	gpu = 0;
	INT	i, s, r;

	cg.ngj = 0;
	for ( i = 0; i < cg.njob; i++ ) {
		r = job_plan(&cg.job[i]);
		if ( r < 0 ) {
			return FALSE;
		}
		cg.job[i].planned = ( r > 0 );
	}
	for ( i = 0; i < cg.njob; i++ ) {
		CJOB	*j = &cg.job[i];

		if ( !j->planned || ( i == 0 && j->ground ) ) continue;
		for ( s = 0; s < j->g.nx * j->g.ny; s++ ) {
			if ( j->cls[s] == CP_ST_FREE ) j->cls[s] = ( cg.feat & CF_TILES ) ? CP_ST_SKIP : CP_ST_LOAD;
		}
		gpu += cp_cost(&j->g, j->cls, &cg.dr[j->d0], j->ndraw, cg.pool, CP_CW_TILE);
	}
	if ( cg.njob > 0 && j0->ground && j0->planned ) {
		if ( ( cg.feat & CF_TILES ) != 0 ) {
			gpu += cp_cost(&j0->g, j0->cls, &cg.dr[j0->d0], j0->ndraw, cg.pool, CP_CW_TILE);
			(void)cp_share(&j0->g, j0->cls, cg.keep, cg.nkeep, cg.p_ground != NULL && !cg.keep_over, gpu);
		} else {
			for ( s = 0; s < j0->g.nx * j0->g.ny; s++ ) {
				if ( j0->cls[s] == CP_ST_FREE ) j0->cls[s] = CP_ST_LOAD;
			}
		}
	}
	for ( i = 0; i < cg.njob; i++ ) {
		if ( !cg.job[i].planned ) continue;
		r = job_lists(&cg.job[i], &cg.gj[cg.ngj]);
		if ( r < 0 ) {
			return FALSE;
		}
		if ( r > 0 ) cg.ngj++;
	}
	return !cg.cmd_over;
}

/* A part begun: the lists empty, its back buffer and ground kept for the processor's share */
LOCAL void part_begin( CONST T_CPDST *d, CONST UW *ground, UINT gpitch )
{
	cg.part++;
	wraps_sweep();
	cg.cmd_used = CMD_ZERO;
	cg.cmd_over = FALSE;
	cg.njob = 0;
	cg.ndraw = 0;
	cg.npool = 0;
	cg.nkeep = 0;
	cg.keep_over = FALSE;
	cg.ngj = 0;
	cg.njbo = 0;
	cg.cur = NULL;
	knl_memset(cg.cov_at, 0, sizeof(cg.cov_at));
	cg.p_back = d->back;
	cg.p_pitch = d->pitch;
	cg.p_ground = ground;
	cg.p_gpitch = gpitch;
}

/*
 * A part's draws all there: the jobs made, and the caches made ready
 * for them -- what the GPU reads in memory, and nothing held of the rows
 * of the back buffer it writes -- with one wait for all of it. 'src' and
 * 'len' are more memory the GPU reads (the check's), w[0..nlaid-1] the
 * windows laid. E_OK, or an error when the part is to be built on the
 * processor.
 */
LOCAL ER part_ready( CONST T_CPDST *d, CONST UW *ground, UINT gpitch, UINT gen,
		     CONST T_CPWIN *w, INT nlaid, CONST void *src, UD len, UD t0 )
{
	CJOB	*j0 = &cg.job[0];
	UD	t1, t2;
	INT	s;
	BOOL	ground_read = FALSE;

	if ( cg.cmd_over || !jobs_make() ) {
		return E_LIMIT;			/* more than the lists hold: all of it on the processor */
	}
	t1 = ticks();
	if ( cg.njob > 0 && j0->ground && j0->planned ) {
		for ( s = 0; s < j0->g.nx * j0->g.ny && !ground_read; s++ ) {
			ground_read = ( j0->cls[s] == CP_ST_LOAD );
		}
	}
	dc_begin();
	if ( ground_read && ( cg.ground_at != ground || cg.ground_gen != gen ) ) {
		dc_lines((UBINT)ground, (UBINT)gpitch * (UBINT)d->rows, OP_CLEAN);
		cg.ground_at = ground;
		cg.ground_gen = gen;
	}
	if ( src != NULL ) {
		dc_lines((UBINT)src, len, OP_CLEAN);
	}
	wins_clean(w, nlaid);
	t2 = ticks();
	if ( cg.ngj > 0 ) {
		back_rows(d, OP_INVAL);
	}
	dc_lines((UBINT)cg.cmd, cg.cmd_used, OP_CLEAN);
	dc_end();
	cg.ph.build += t1 - t0;
	cg.ph.before_win += t2 - t1;
	cg.ph.before += ticks() - t1;
	return E_OK;
}

/*
 * The jobs that lay the ground and windows 0..n-1 of a part of the
 * screen, made ready to submit: how many windows they lay (the rest are
 * the processor's), or an error when they cannot be made and the part
 * is to be built on the processor. Called with cg.mtx held.
 */
LOCAL INT lay_now( CONST T_CPDST *d, CONST UW *ground, UINT gpitch, UINT gen,
		   CONST T_CPWIN *w, INT n )
{
	CONST T_DPRECT	*b = &d->box;
	PART		pt;
	UW		src[CP_GPU_MAX];
	INT		i, k, nlaid;
	BOOL		any_spread = FALSE;
	UD		t0 = ticks();
	ER		er;

	if ( !cg.ready || ground == NULL || ( b->left & ( ALIGN_PX - 1 ) ) != 0
	  || ( d->pitch & 63 ) != 0 || ( gpitch & 63 ) != 0
	  || b->right <= b->left || b->bottom <= b->top ) {
		return E_PAR;
	}
	if ( n > CP_GPU_MAX ) n = CP_GPU_MAX;
	part_begin(d, ground, gpitch);

	knl_memset(&pt, 0, sizeof(pt));
	pt.d = d;
	pt.gpitch = gpitch;
	pt.back = wrap_of(d->back, (UD)d->pitch * (UD)d->rows);
	pt.ground = wrap_of(ground, (UD)gpitch * (UD)d->rows);
	if ( pt.back == 0 || pt.ground == 0 ) {
		return E_NOMEM;
	}
	pt.dummy = pt.back;

	/* which windows: up to the first the GPU does not do */
	for ( k = 0; k < n; k++ ) {
		if ( w[k].shape != NULL || w[k].keyed ) break;
		src[k] = wrap_of(w[k].pixels, (UD)w[k].pitch * (UD)( w[k].outer.bottom - w[k].outer.top ));
		if ( src[k] == 0 ) break;
		if ( !CP_MOVING(&w[k]) && w[k].blur > 0 ) any_spread = TRUE;
	}
	nlaid = k;
	if ( any_spread ) {
		(void)spread_alloc(cp_spread_bytes(d->pitch, d->rows) + PAGE_SIZE);
	}
	if ( ( cg.feat & CF_SPREAD ) != 0 && cg.t1 != NULL && cg.t_bytes >= cp_spread_bytes(d->pitch, d->rows) ) {
		pt.t1 = bo_gpu(cg.t1);
		pt.t2 = bo_gpu(cg.t2);
	} else {
		/* no spread on the GPU, or no room for it: the processor lays from the first window that spreads */
		for ( k = 0; k < nlaid; k++ ) {
			if ( !CP_MOVING(&w[k]) && w[k].blur > 0 && ( w[k].a_frame < 255 || w[k].a_work < 255 ) ) {
				nlaid = k;
				break;
			}
		}
	}

	/* the draws */
	cg.cur = job_open(b->left, b->top, b->right - b->left, b->bottom - b->top,
			  pt.ground + (UW)b->top * gpitch + (UW)b->left * 4, gpitch,
			  pt.back + (UW)b->top * d->pitch + (UW)b->left * 4, d->pitch);
	if ( cg.cur != NULL ) {
		cg.cur->ground = TRUE;
	}
	for ( i = 0; i < nlaid && cg.cur != NULL; i++ ) {
		BOOL	ok = CP_MOVING(&w[i]) ? lay_moving(&pt, &w[i], src[i]) : lay_window(&pt, &w[i], src[i]);

		if ( !ok ) break;
	}
	if ( cg.cmd_over || i < nlaid ) {
		return E_LIMIT;			/* more than the lists hold: all of it on the processor */
	}

	/* the BOs the jobs use */
	jbo_add(cg.cmd_bo);
	jbo_add(cg.code);
	jbo_add(cg.tile_alloc);
	jbo_add(cg.tile_state);
	if ( pt.t1 != 0 ) {
		jbo_add(cg.t1);
		jbo_add(cg.t2);
	}
	jbo_add(wrap_bo(pt.back));
	jbo_add(wrap_bo(pt.ground));
	for ( i = 0; i < nlaid; i++ ) {
		jbo_add(wrap_bo(src[i]));
	}

	er = part_ready(d, ground, gpitch, gen, w, nlaid, NULL, 0, t0);
	return ( er < E_OK ) ? er : nlaid;
}

/* ---------------------------------------------------------------- the backend */

LOCAL void turn_off( CONST char *why, ER er )
{
	if ( !cg.failed ) {
		tm_printf((UB *)"v3d compose: %s (%d); compositing on the processor from now on\n", why, (INT)er);
	}
	cg.failed = TRUE;
}

/*
 * The jobs made ready (cg.gj), all queued; the processor copies its
 * share of the ground meanwhile, then waits: the last one's end is all
 * of theirs. Either way the back buffer's rows the GPU wrote are read
 * from memory afterwards.
 */
LOCAL ER jobs_run( CONST T_CPDST *d, UD *p_jobs )
{
	D		fence[NJOB];
	INT		i, nsub = 0;
	ER		er = E_OK;
	T_V3DJOBT	jt;
	UD		t0, t1, t2;

	v3d_jobt_take(&jt);			/* what ran before is not this part's */
	t0 = ticks();
	for ( i = 0; i < cg.ngj; i++ ) {
		fence[i] = knl_gpu_submit(&cg.gj[i], cg.jbo, cg.njbo);
		if ( fence[i] <= 0 ) {
			er = (ER)fence[i];
			break;
		}
		nsub++;
	}
	t1 = ticks();
	cpu_share();
	if ( nsub > 0 ) {
		ER	w8 = knl_gpu_wait(fence[nsub - 1], WAIT_US);

		if ( w8 < E_OK && er >= E_OK ) er = w8;
		for ( i = 0; i < nsub - 1 && er >= E_OK; i++ ) {
			ER	e = knl_gpu_wait(fence[i], TMO_POL);

			if ( e < E_OK ) er = e;
		}
	}
	t2 = ticks();
	if ( p_jobs != NULL ) *p_jobs = (UD)nsub;
	if ( cg.ngj > 0 ) {
		dc_begin();
		back_rows(d, OP_INVAL);
		dc_end();
	}

	v3d_jobt_take(&jt);
	cg.ph.submit += t1 - t0;
	cg.ph.after += ticks() - t2;
	cg.ph.njob += (UD)nsub;
	cg.ph.jt.njob += jt.njob;
	cg.ph.jt.setup += jt.setup;
	cg.ph.jt.bin += jt.bin;
	cg.ph.jt.mid += jt.mid;
	cg.ph.jt.render += jt.render;
	cg.ph.jt.clean += jt.clean;
	if ( jt.njob > 0 && jt.first_kick >= t0 && jt.last_end >= jt.first_kick && t2 >= jt.last_end ) {
		cg.ph.start += jt.first_kick - t0;
		cg.ph.gpu += jt.last_end - jt.first_kick;
		cg.ph.wake += t2 - jt.last_end;
	}
	return er;
}

LOCAL INT lay_run( CONST T_CPDST *d, CONST UW *ground, UINT gpitch, UINT gen,
		   CONST T_CPWIN *w, INT n, UD *p_jobs )
{
	INT	nlaid;
	ER	er;

	if ( p_jobs != NULL ) *p_jobs = 0;
	knl_memset(&cg.ph, 0, sizeof(cg.ph));
	nlaid = lay_now(d, ground, gpitch, gen, w, n);
	if ( nlaid < 0 ) {
		return nlaid;
	}
	er = jobs_run(d, p_jobs);
	return ( er < E_OK ) ? er : nlaid;
}

/*
 * The ground and one draw of fragment shader fs over r, for the check:
 * its parameters, and in buf[CP_B_SRC] memory of 'len' bytes for it to
 * read; a copy reads it as a window standing at r, rows par[CP_PD_PITCH]
 * long. Called with cg.mtx held.
 */
LOCAL ER lay_one( CONST T_CPDST *d, CONST UW *ground, UINT gpitch, INT fs, CONST T_DPRECT *r,
		  UW *par, CONST void *src, UD len, UD *p_jobs )
{
	CONST T_DPRECT	*b = &d->box;
	UW		back, gr, buf[CP_NBUF];
	T_CPWIN		win;
	CJOB		*j;
	UD		t0 = ticks();
	ER		er;

	if ( p_jobs != NULL ) *p_jobs = 0;
	knl_memset(&cg.ph, 0, sizeof(cg.ph));
	part_begin(d, ground, gpitch);
	back = wrap_of(d->back, (UD)d->pitch * (UD)d->rows);
	gr = wrap_of(ground, (UD)gpitch * (UD)d->rows);
	buf[CP_B_SRC] = ( src != NULL ) ? wrap_of(src, len) : back;
	buf[CP_B_AUX] = back;
	buf[CP_B_COV] = cg.cmd_gpu;
	if ( back == 0 || gr == 0 || buf[CP_B_SRC] == 0 ) {
		return E_NOMEM;
	}
	j = job_open(b->left, b->top, b->right - b->left, b->bottom - b->top,
		     gr + (UW)b->top * gpitch + (UW)b->left * 4, gpitch,
		     back + (UW)b->top * d->pitch + (UW)b->left * 4, d->pitch);
	if ( j == NULL ) {
		return E_LIMIT;
	}
	j->ground = TRUE;
	knl_memset(&win, 0, sizeof(win));
	win.outer = *r;
	win.shown = *r;
	win.pixels = src;
	win.pitch = par[CP_PD_PITCH];
	win.fade = 255;
	draw(j, fs, r, par, buf, ( fs == CP_FS_COPY && src != NULL ) ? &win : NULL);
	jbo_add(cg.cmd_bo);
	jbo_add(cg.code);
	jbo_add(cg.tile_alloc);
	jbo_add(cg.tile_state);
	jbo_add(wrap_bo(back));
	jbo_add(wrap_bo(gr));
	jbo_add(wrap_bo(buf[CP_B_SRC]));
	er = part_ready(d, ground, gpitch, 0, NULL, 0, src, ( src != NULL ) ? len : 0, t0);
	if ( er < E_OK ) {
		return er;
	}
	return jobs_run(d, p_jobs);
}

/* ---------------------------------------------------------------- where the time went */

LOCAL void ph_add( PHASE *s, CONST PHASE *p )
{
	s->build += p->build;
	s->before += p->before;
	s->before_win += p->before_win;
	s->submit += p->submit;
	s->cpu += p->cpu;
	s->start += p->start;
	s->gpu += p->gpu;
	s->wake += p->wake;
	s->after += p->after;
	s->jt.njob += p->jt.njob;
	s->jt.setup += p->jt.setup;
	s->jt.bin += p->jt.bin;
	s->jt.mid += p->jt.mid;
	s->jt.render += p->jt.render;
	s->jt.clean += p->jt.clean;
	s->njob += p->njob;
	s->ntile += p->ntile;
	s->ntile_win += p->ntile_win;
	s->ntile_out += p->ntile_out;
	s->ndraw += p->ndraw;
	s->px += p->px;
	s->px_copy += p->px_copy;
	s->px_out += p->px_out;
	s->tmu += p->tmu;
	s->cpu_px += p->cpu_px;
}

/*
 * One line of where the time of a part went, divided by 'n' (the parts
 * summed), for the console. The steps one after the other: the draws
 * and lists made (build), the caches readied (caches, of which the
 * rows the GPU reads), the jobs handed over (hand over), the processor
 * copying its share of the ground while the GPU works (ground by the
 * processor), the wait for the first job to start (to start, counted
 * from the jobs handed over and so with the processor's share in it),
 * the GPU from the first start to the last end (GPU, and inside it as
 * the driver timed each job: the caches readied and the binner, the
 * caches readied again and the renderer, the clean after it), this
 * task woken (wake), the back buffer's rows dropped from the caches
 * (after). Then how much there was: jobs, tiles rendered (of them
 * loaded from windows' pixels, and how many were left out or were the
 * processor's), draws, thousands of pixels the shaders drew, of them
 * copied, and of draws left out under later copies, thousands of words
 * read through the TMU, and thousands of pixels of the ground the
 * processor copied.
 */
LOCAL void ph_print( CONST char *what, CONST PHASE *p, UD n )
{
	if ( n == 0 ) n = 1;
	tm_printf((UB *)"v3d compose: phases %s: build %ld, caches %ld (rows read %ld), hand over %ld, "
		  "ground by the processor %ld, to start %ld, GPU %ld (ready-bin %ld, bin %ld, ready-render %ld, "
		  "render %ld, clean %ld), wake %ld, after %ld us; %ld jobs, %ld tiles (%ld from windows, "
		  "%ld left out), %ld draws, %ld Kpx drawn, %ld copied, %ld left out under copies, "
		  "%ld K TMU reads, %ld Kpx of ground by the processor\n",
		  what, ticks_us(p->build) / n, ticks_us(p->before) / n, ticks_us(p->before_win) / n,
		  ticks_us(p->submit) / n, ticks_us(p->cpu) / n, ticks_us(p->start) / n, ticks_us(p->gpu) / n,
		  ticks_us(p->jt.setup) / n, ticks_us(p->jt.bin) / n, ticks_us(p->jt.mid) / n,
		  ticks_us(p->jt.render) / n, ticks_us(p->jt.clean) / n, ticks_us(p->wake) / n,
		  ticks_us(p->after) / n, p->njob / n, p->ntile / n, p->ntile_win / n, p->ntile_out / n,
		  p->ndraw / n, p->px / n / 1000, p->px_copy / n / 1000, p->px_out / n / 1000,
		  p->tmu / n / 1000, p->cpu_px / n / 1000);
}

LOCAL INT cg_lay( CONST T_CPDST *d, CONST UW *ground, UINT gpitch, UINT gen, CONST T_CPWIN *w, INT n )
{
	UD	jobs = 0;
	INT	r;

	if ( !cg.passed || cg.failed ) {
		return E_NOSPT;
	}
	tk_loc_mtx(cg.mtx, TMO_FEVR);
	r = lay_run(d, ground, gpitch, gen, w, n, &jobs);
	cg.st_jobs += jobs;
	if ( r >= E_OK ) {
		ph_add(&cg.st_ph, &cg.ph);
		cg.st_ph_n++;
	}
	if ( r == E_IO || r == E_TMOUT ) {
		turn_off("a job failed", r);
	} else if ( r < E_OK ) {
		cg.st_fallback++;
	} else if ( r < n ) {
		cg.st_partial++;
	}
	tk_unl_mtx(cg.mtx);
	return r;
}

LOCAL BOOL cg_wants( CONST T_DPRECT *box, UINT fx )
{
	UD	px;

	if ( !cg.passed || cg.failed ) {
		return FALSE;
	}
	if ( ( fx & CP_FX_BLUR ) != 0 && !cg.blur_ok ) {
		return FALSE;		/* the processor spreads quicker */
	}
	px = (UD)( box->right - box->left ) * (UD)( box->bottom - box->top );
	return ( px >= ( ( fx & CP_FX_LOOK ) != 0 ? cg.thr_fx : cg.thr_plain ) );
}

LOCAL void stats_print( UD now )
{
	tm_printf((UB *)"v3d compose: in %ld s: %ld parts on the GPU (%ld jobs, %ld Mpx, %ld us), "
		  "%ld on the processor (%ld Mpx, %ld us); fell back %ld, partly %ld%s\n",
		  ( now - cg.st_t0 ) / 1000, cg.st_gpu, cg.st_jobs, cg.st_gpu_px / 1000000, cg.st_gpu_us,
		  cg.st_cpu, cg.st_cpu_px / 1000000, cg.st_cpu_us, cg.st_fallback, cg.st_partial,
		  cg.failed ? ", turned off" : "");
	tm_printf((UB *)"v3d compose: whole screen: GPU %ld times, %ld us each; processor %ld times, %ld us each\n",
		  cg.st_full_gpu_n, ( cg.st_full_gpu_n != 0 ) ? cg.st_full_gpu_us / cg.st_full_gpu_n : 0,
		  cg.st_full_cpu_n, ( cg.st_full_cpu_n != 0 ) ? cg.st_full_cpu_us / cg.st_full_cpu_n : 0);
	if ( cg.st_ph_n != 0 ) {
		ph_print("of a part on the GPU, on average", &cg.st_ph, cg.st_ph_n);
	}
	knl_memset(&cg.st_ph, 0, sizeof(cg.st_ph));
	cg.st_ph_n = 0;
	cg.st_t0 = now;
	cg.st_gpu = cg.st_cpu = cg.st_jobs = cg.st_fallback = cg.st_partial = 0;
	cg.st_full_gpu_n = cg.st_full_gpu_us = cg.st_full_cpu_n = cg.st_full_cpu_us = 0;
	cg.st_gpu_px = cg.st_gpu_us = cg.st_cpu_px = cg.st_cpu_us = 0;
}

LOCAL void cg_note( BOOL gpu, CONST T_DPRECT *box, UD us )
{
	T_DISPSPEC	sp;
	UD		px = (UD)( box->right - box->left ) * (UD)( box->bottom - box->top );
	UD		now = now_ms();
	BOOL		full = FALSE;

	if ( ts_disp_ref(&sp) >= E_OK && px * 10 >= (UD)sp.width * sp.height * 9 ) {
		full = TRUE;
	}
	if ( gpu ) {
		cg.st_gpu++;
		cg.st_gpu_px += px;
		cg.st_gpu_us += us;
		if ( full ) {
			cg.st_full_gpu_n++;
			cg.st_full_gpu_us += us;
		}
	} else {
		cg.st_cpu++;
		cg.st_cpu_px += px;
		cg.st_cpu_us += us;
		if ( full ) {
			cg.st_full_cpu_n++;
			cg.st_full_cpu_us += us;
		}
	}
	if ( cg.st_t0 == 0 ) {
		cg.st_t0 = now;
	} else if ( now - cg.st_t0 >= STATS_MS ) {
		stats_print(now);
	}
}

LOCAL CONST T_CPGPU	cg_ops = {
	ALIGN_PX,
	cg_wants,
	cg_lay,
	cg_note
};

/* ---------------------------------------------------------------- setting up */

#define CK_W		1024
#define CK_H		768
#define CK_PITCH	( CK_W * 4 )

EXPORT ER v3d_compose_init( void )
{
	T_GPUINFO	gi;
	T_DISPSPEC	sp;
	T_CMTX		cm;
	UD		pa[CMD_BYTES / PAGE_SIZE], i, off, tiles, tb;
	INT		k;
	ER		er;

	if ( cg.ready ) {
		return E_OK;
	}
	if ( knl_gpu_ref(&gi) < E_OK || gi.kind != GPU_KIND_V3D || gi.ver != CP_V3D_VER
	  || gi.rev < CP_V3D_REV_MIN ) {
		tm_printf((UB *)"v3d compose: the shaders are for V3D %d.%d revision %d or later; "
			  "compositing stays on the processor\n", CP_V3D_VER / 10, CP_V3D_VER % 10, CP_V3D_REV_MIN);
		return E_NOSPT;
	}
	cg.draw_index = ( gi.rev >= CP_V3D_REV_DRAW_INDEX );
	cg.feat = CF_ALL;

	/* the vertex and coordinate shaders' uniforms are constants */
	for ( k = 0; k < 2; k++ ) {
		for ( i = 0; i < cp_unif_len[k]; i++ ) {
			if ( cp_unif[k][i][0] != CP_U_CONST ) {
				tm_printf((UB *)"v3d compose: shader %d takes a parameter\n", k);
				return E_SYS;
			}
		}
	}

	cm.exinf = NULL;
	cm.mtxatr = TA_TFIFO;
	cg.mtx = tk_cre_mtx(&cm);
	if ( cg.mtx <= 0 ) {
		return E_LIMIT;
	}

	/* the code, and the two constant uniform streams after it */
	cg.code = knl_gpu_bo_create(CODE_BYTES, 0, &er);
	if ( cg.code == NULL ) goto nomem;
	off = 0;
	for ( k = 0; k < 2 + CP_NFS; k++ ) {
		bo_put(cg.code, off, cp_code[k], cp_code_len[k]);
		cg.prog[k] = bo_gpu(cg.code) + (UW)off;
		off = ( off + cp_code_len[k] + CODE_ALIGN - 1 ) & ~(UD)( CODE_ALIGN - 1 );
	}
	for ( k = 0; k < 2; k++ ) {
		UW	v[64];

		for ( i = 0; i < cp_unif_len[k] && i < 64; i++ ) v[i] = cp_unif[k][i][1];
		bo_put(cg.code, off, v, cp_unif_len[k] * 4);
		if ( k == 0 ) cg.unif_cs = bo_gpu(cg.code) + (UW)off;
		else cg.unif_vs = bo_gpu(cg.code) + (UW)off;
		off = ( off + cp_unif_len[k] * 4 + 63 ) & ~63UL;
	}
	if ( off + 4096 > CODE_BYTES ) {
		tm_printf((UB *)"v3d compose: the code does not fit\n");
		return E_SYS;
	}

	/* the list memory: contiguous, written through the linear mapping */
	cg.cmd_pf = knl_alloc_pages(CMD_ORDER, ZONE_NORMAL, KAF_ZERO);
	if ( cg.cmd_pf == NULL ) {
		cg.cmd_pf = knl_alloc_pages(CMD_ORDER, ZONE_DMA32, KAF_ZERO | KAF_DMA32);
	}
	if ( cg.cmd_pf == NULL ) goto nomem;
	for ( i = 0; i < CMD_BYTES / PAGE_SIZE; i++ ) {
		pa[i] = knl_pf_to_pa(cg.cmd_pf) + i * PAGE_SIZE;
	}
	cg.cmd = (UB *)PA2VA(pa[0]);
	knl_gpu_dcache_clean_inval((UBINT)cg.cmd, CMD_BYTES);
	cg.cmd_bo = knl_gpu_bo_wrap(pa, CMD_BYTES / PAGE_SIZE, &er);
	if ( cg.cmd_bo == NULL ) goto nomem;
	cg.cmd_gpu = bo_gpu(cg.cmd_bo);

	/* the tile memory, for a frame the size of the screen */
	if ( ts_disp_ref(&sp) < E_OK ) {
		sp.width = 1920;
		sp.height = 1080;
		sp.pitch = 1920 * 4;
	}
	tb = cp_spread_bytes(sp.pitch, (INT)sp.height);
	if ( sp.width < 1024 ) sp.width = 1024;
	if ( sp.height < 768 ) sp.height = 768;
	tiles = (UD)( ( sp.width + TILE - 1 ) / TILE ) * ( ( sp.height + TILE - 1 ) / TILE );
	cg.ta_bytes = ( ( tiles * TILE_ALLOC_BLOCK + 4095 ) & ~4095UL ) + 8192 + TILE_ALLOC_EXTRA;
	cg.ts_bytes = tiles * TILE_STATE_BYTES;
	cg.tile_alloc = knl_gpu_bo_create((SZ)cg.ta_bytes, 0, &er);
	cg.tile_state = knl_gpu_bo_create((SZ)cg.ts_bytes, 0, &er);
	if ( cg.tile_alloc == NULL || cg.tile_state == NULL ) goto nomem;

	/*
	 * T1 and T2, for the screen's back buffer or the check's, whichever
	 * is the larger, taken now: taking, zeroing and cleaning them is
	 * milliseconds, not to be paid by the first part that spreads
	 */
	if ( tb < cp_spread_bytes(CK_PITCH, CK_H) ) tb = cp_spread_bytes(CK_PITCH, CK_H);
	(void)spread_alloc(tb + PAGE_SIZE);

	knl_vunmap_hook = vunmap_seen;
	cg.thr_plain = cg.thr_fx = (UD)-1;
	cg.ready = TRUE;
	cp_gpu_set(&cg_ops);
	tm_printf((UB *)"v3d compose: ready: code %ld bytes at GPU %08x, lists %d KB at GPU %08x, "
		  "tile memory %ld KB, spread %ld KB twice, %s records\n", off, cg.prog[0], CMD_BYTES / 1024,
		  cg.cmd_gpu, ( cg.ta_bytes + cg.ts_bytes ) / 1024, cg.t_bytes / 1024,
		  cg.draw_index ? "draw index" : "plain");
	return E_OK;

nomem:
	tm_printf((UB *)"v3d compose: no memory to set up (%d); compositing stays on the processor\n", er);
	return E_NOMEM;
}

/* ---------------------------------------------------------------- the check at start-up */

/*
 * A few scenes built both ways, on the processor with compose.c and on
 * the GPU, over the same ground into two back buffers of their own, and
 * compared pixel by pixel. The GPU is used only when every scene comes
 * out the same, but for at most one step in a channel. The scenes
 * between them use every stage and every shader: solid windows, washed
 * and tinted frames, frames and whole windows seen through, round
 * corners, shadows, what shows through spread (twice over, so that
 * a frame is stored and loaded again between), a part of the screen
 * with a spread cut at its edge, a window opening, and more windows
 * than one job draws.
 *
 * They are built first with every way in use (CF_ALL); when a scene
 * differs, again without the ways most likely at fault, one after the
 * other, down to plain jobs, and the first set of ways with which they
 * all come out the same is the one used.
 *
 * Then the time either way takes, for a small, a middling and a whole
 * part, with the windows plain and with a look, gives the size of part
 * from which the GPU is the quicker.
 */
#define CK_NBUF		4		/* windows' pixels: windows share them */
#define CK_NWIN		24		/* more draws than one job takes (JOB_DRAWS_MAX) */
#define CK_REPEAT	3		/* timings: the least of this many */

IMPORT void knl_wm_hold( BOOL hold );

LOCAL struct {
	UB	*back_c, *back_g;
	UW	*ground;
	UB	*pix[CK_NBUF];
	UW	seed;
	INT	nscene, npass;
} ck;

/* The ways tried, in turn, until the scenes come out the same */
LOCAL CONST UB ck_feats[] = {
	CF_ALL,
	CF_ALL & ~CF_WLOAD,
	CF_ALL & ~CF_SPREAD,
	CF_CULL | CF_TILES,
	CF_CULL,
	0
};

LOCAL void feat_name( UINT f, char *s )
{
	tm_sprintf((UB *)s, (UB *)"%s%s%s%s%s", ( f & CF_CULL ) ? " cut to what shows" : "",
		   ( f & CF_TILES ) ? ", supertiles as needed" : "", ( f & CF_WLOAD ) ? ", tiles from windows" : "",
		   ( f & CF_SPREAD ) ? ", spread on the GPU" : "", ( f == 0 ) ? " plain jobs" : "");
}

LOCAL UW ck_rnd( void )
{
	UW	x = ck.seed;

	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	ck.seed = x;
	return x;
}

LOCAL UD ck_pages( void )
{
	return ( (UD)CK_PITCH * CK_H + PAGE_SIZE - 1 ) / PAGE_SIZE;
}

LOCAL void ck_free( void )
{
	INT	i;

	if ( ck.back_c != NULL ) knl_vunmap(ck.back_c, ck_pages());
	if ( ck.back_g != NULL ) knl_vunmap(ck.back_g, ck_pages());
	if ( ck.ground != NULL ) knl_vunmap(ck.ground, ck_pages());
	for ( i = 0; i < CK_NBUF; i++ ) {
		if ( ck.pix[i] != NULL ) knl_vunmap(ck.pix[i], ck_pages());
		ck.pix[i] = NULL;
	}
	ck.back_c = ck.back_g = NULL;
	ck.ground = NULL;
}

LOCAL BOOL ck_alloc( void )
{
	INT	i, x, y;

	ck.back_c = (UB *)knl_vmap(ck_pages(), 0);
	ck.back_g = (UB *)knl_vmap(ck_pages(), 0);
	ck.ground = (UW *)knl_vmap(ck_pages(), 0);
	for ( i = 0; i < CK_NBUF; i++ ) {
		ck.pix[i] = (UB *)knl_vmap(ck_pages(), 0);
	}
	if ( ck.back_c == NULL || ck.back_g == NULL || ck.ground == NULL || ck.pix[CK_NBUF - 1] == NULL ) {
		ck_free();
		return FALSE;
	}
	/* the ground: smooth runs and noise, every top byte */
	ck.seed = 0x1234567U;
	for ( y = 0; y < CK_H; y++ ) {
		for ( x = 0; x < CK_W; x++ ) {
			UW	n = ck_rnd();

			ck.ground[y * CK_W + x] = ( ( (UW)x * 255 / CK_W ) << 16 ) | ( ( (UW)y * 255 / CK_H ) << 8 )
						  | ( ( x ^ y ) & 0xFF ) | ( n & 0xFF000000U );
			if ( ( x / 64 + y / 64 ) % 3 == 0 ) ck.ground[y * CK_W + x] ^= n & 0x00FFFFFFU;
		}
	}
	/* the windows' pixels: noise in some rows, flat runs in others */
	for ( i = 0; i < CK_NBUF; i++ ) {
		UW	*p = (UW *)ck.pix[i];

		for ( y = 0; y < CK_H; y++ ) {
			for ( x = 0; x < CK_W; x++ ) {
				p[y * CK_W + x] = ( ( y / 8 ) % 2 == 0 ) ? ck_rnd()
						  : ( 0x00102030U * (UW)( i + 1 ) + (UW)x ) | ( ck_rnd() & 0xFF000000U );
			}
		}
	}

	/*
	 * Entered in the MMU before the scenes, as the window manager's own
	 * memory is after its first part, so that no scene's time has it
	 */
	tk_loc_mtx(cg.mtx, TMO_FEVR);
	cg.part++;
	(void)wrap_of(ck.back_g, (UD)CK_PITCH * CK_H);
	(void)wrap_of(ck.ground, (UD)CK_PITCH * CK_H);
	for ( i = 0; i < CK_NBUF; i++ ) {
		(void)wrap_of(ck.pix[i], (UD)CK_PITCH * CK_H);
	}
	tk_unl_mtx(cg.mtx);
	return TRUE;
}

/* A window at (l, t), w x h, over buffer 'k', with a frame as the window manager draws one */
LOCAL void ck_win( T_CPWIN *w, INT k, INT l, INT t, INT ww, INT hh )
{
	knl_memset(w, 0, sizeof(*w));
	w->outer.left = l;
	w->outer.top = t;
	w->outer.right = l + ww;
	w->outer.bottom = t + hh;
	w->work.left = l + 3;
	w->work.top = t + 22;
	w->work.right = l + ww - 4;
	w->work.bottom = t + hh - 4;
	w->band = w->outer;
	w->band.bottom = w->work.top;
	w->pixels = ck.pix[k % CK_NBUF];
	w->pitch = CK_PITCH;
	w->a_frame = 255;
	w->a_work = 255;
	w->shown = w->outer;
	w->fade = 255;
}

LOCAL void ck_look( T_CPWIN *w, UINT a_frame, INT round, INT blur, UINT dark )
{
	w->a_frame = a_frame;
	w->round = round;
	w->blur = blur;
	w->wash = 0x202838;
	w->wash_str = 90;
	w->bar_wash = 0x405878;
	w->bar_str = 120;
	w->tint = 0x336699;
	w->tint_str = 50;
	if ( dark != 0 ) {
		w->shd_dx = 6;
		w->shd_dy = 8;
		w->shd_size = 10;
		w->shd_dark = dark;
	}
}

/* The part 'box' of a scene on the processor, into back_c */
LOCAL void ck_cpu( CONST T_DPRECT *box, CONST T_CPWIN *w, INT n )
{
	T_CPDST	d;
	INT	y, i;

	d.back = ck.back_c;
	d.pitch = CK_PITCH;
	d.box = *box;
	d.rows = CK_H;
	for ( y = box->top; y < box->bottom; y++ ) {
		knl_memcpy(ck.back_c + (UBINT)y * CK_PITCH + (UBINT)box->left * 4,
			   ck.ground + (SZ)y * CK_W + box->left, (SZ)( box->right - box->left ) * 4);
	}
	for ( i = 0; i < n; i++ ) {
		cp_window(&d, &w[i]);
	}
}

/* And on the GPU into back_g, the processor finishing what the GPU left: how many it laid */
LOCAL INT ck_gpu( CONST T_DPRECT *box, CONST T_CPWIN *w, INT n, UD *p_jobs )
{
	T_CPDST	d;
	INT	r, i;

	d.back = ck.back_g;
	d.pitch = CK_PITCH;
	d.box = *box;
	d.rows = CK_H;
	tk_loc_mtx(cg.mtx, TMO_FEVR);
	r = lay_run(&d, ck.ground, CK_PITCH, 1, w, n, p_jobs);
	tk_unl_mtx(cg.mtx);
	for ( i = ( r < 0 ) ? n : r; i < n; i++ ) {
		cp_window(&d, &w[i]);
	}
	return r;
}

/* What a back buffer holds before a scene, pixel by pixel */
LOCAL UW ck_junk_at( UD i )
{
	return 0xA5000000U ^ ( (UW)i * 0x9E3779B1U );
}

LOCAL void ck_junk( void )
{
	UW	*c = (UW *)ck.back_c, *g = (UW *)ck.back_g;
	UD	i, n = (UD)CK_W * CK_H;

	for ( i = 0; i < n; i++ ) {
		c[i] = g[i] = ck_junk_at(i);
	}
	knl_gpu_dcache_clean_inval((UBINT)ck.back_g, (UBINT)CK_PITCH * CK_H);
}

/*
 * The two back buffers compared, with what went into them: 'r' what the
 * GPU's part answered (the windows it laid, or an error), out of 'n',
 * and 'want' how many it is to lay. TRUE when the same.
 */
LOCAL BOOL ck_compare( CONST char *name, CONST T_DPRECT *box, INT n, INT want, INT r, UD jobs, UD gpu_us,
		       UD cpu_us )
{
	UW	*c = (UW *)ck.back_c, *g = (UW *)ck.back_g;
	UD	i, np = (UD)CK_W * CK_H, ndiff = 0, ntop = 0;
	UD	nold = 0, nground = 0;
	UINT	maxd = 0;
	INT	shown = 0, k, x0 = CK_W, y0 = CK_H, x1 = -1, y1 = -1;
	BOOL	same;

	for ( i = 0; i < np; i++ ) {
		UW	a = c[i], b = g[i];
		UINT	d = 0;

		if ( a == b ) continue;
		if ( ( ( a ^ b ) & 0xFF000000U ) != 0 ) ntop++;
		for ( k = 0; k < 3; k++ ) {
			INT	x = (INT)( ( a >> ( 8 * k ) ) & 0xFF ) - (INT)( ( b >> ( 8 * k ) ) & 0xFF );

			if ( x < 0 ) x = -x;
			if ( (UINT)x > d ) d = (UINT)x;
		}
		if ( d == 0 ) continue;
		ndiff++;
		if ( d > maxd ) maxd = d;
		if ( b == ck_junk_at(i) ) nold++;
		else if ( b == ck.ground[i] ) nground++;
		if ( (INT)( i % CK_W ) < x0 ) x0 = (INT)( i % CK_W );
		if ( (INT)( i % CK_W ) > x1 ) x1 = (INT)( i % CK_W );
		if ( (INT)( i / CK_W ) < y0 ) y0 = (INT)( i / CK_W );
		if ( (INT)( i / CK_W ) > y1 ) y1 = (INT)( i / CK_W );
		if ( shown < 3 ) {
			tm_printf((UB *)"v3d compose: check %s: at (%d,%d) GPU %08x, processor %08x\n",
				  name, (INT)( i % CK_W ), (INT)( i / CK_W ), b, a);
			shown++;
		}
	}
	same = ( r == want && maxd <= 1 );
	tm_printf((UB *)"v3d compose: check %s, %d windows in %dx%d at (%d,%d): %s; %ld of %ld pixels differ, "
		  "by %d at most, top bytes %ld; GPU %d window(s) in %ld job(s) %ld us, processor %ld us\n",
		  name, n, box->right - box->left, box->bottom - box->top, box->left, box->top,
		  same ? ( ndiff == 0 ? "same" : "same within 1" ) : "DIFFERENT",
		  ndiff, np, (INT)maxd, ntop, r, jobs, gpu_us, cpu_us);
	if ( ndiff != 0 ) {
		tm_printf((UB *)"v3d compose: check %s: they lie within (%d,%d)-(%d,%d); of them %ld were left "
			  "as they were before, %ld show the ground\n", name, x0, y0, x1, y1, nold, nground);
	}
	ck.nscene++;
	if ( same ) ck.npass++;
	return same;
}

/*
 * How many of a scene's windows the GPU lays: all of them, or without
 * the spread on the GPU those before the first that spreads
 */
LOCAL INT ck_want( CONST T_CPWIN *w, INT n )
{
	INT	k;

	if ( ( cg.feat & CF_SPREAD ) != 0 ) {
		return n;
	}
	for ( k = 0; k < n; k++ ) {
		if ( !CP_MOVING(&w[k]) && w[k].blur > 0 && ( w[k].a_frame < 255 || w[k].a_work < 255 ) ) {
			return k;
		}
	}
	return n;
}

/* One scene both ways, compared. TRUE when the same */
LOCAL BOOL ck_scene( CONST char *name, CONST T_DPRECT *box, CONST T_CPWIN *w, INT n )
{
	UD	t0, cpu_us, gpu_us, jobs = 0;
	INT	r;
	BOOL	same;

	ck_junk();
	knl_wm_hold(TRUE);
	t0 = ticks();
	ck_cpu(box, w, n);
	cpu_us = ticks_us(ticks() - t0);
	t0 = ticks();
	r = ck_gpu(box, w, n, &jobs);
	gpu_us = ticks_us(ticks() - t0);
	knl_wm_hold(FALSE);
	same = ck_compare(name, box, n, ck_want(w, n), r, jobs, gpu_us, cpu_us);
	ph_print(name, &cg.ph, 1);
	if ( knl_strcmp(name, "spread") == 0 ) {
		/* the whole screen with windows seen through and spread */
		cg.blur_ok = (BOOL)( same && gpu_us < cpu_us && ( cg.feat & CF_SPREAD ) != 0 );
	}
	return same;
}

/*
 * One draw of a shader that does one thing, over the ground: a colour
 * (the draws reach the pixels where they should), a copy of a window's
 * pixels (the TMU, and the tiles it covers loaded from them), what is
 * behind turned over (the tile buffer read). Where the colour landed is
 * told as well, so that a draw put in the wrong place shows where.
 */
LOCAL BOOL ck_one( CONST char *name, INT fs )
{
	static CONST T_DPRECT	all = { 0, 0, CK_W, CK_H }, r = { 100, 60, 400, 260 };
	CONST UW		colour = 0x00336699U;
	UW			par[CP_NPARAM], *c = (UW *)ck.back_c, *g = (UW *)ck.back_g;
	UW			*p0 = (UW *)ck.pix[0];
	T_CPDST			d;
	UD			jobs = 0, t0, gpu_us, n = 0;
	INT			x, y, x0 = CK_W, y0 = CK_H, x1 = -1, y1 = -1;
	ER			er;
	BOOL			same;

	ck_junk();
	for ( y = 0; y < CK_H; y++ ) {
		for ( x = 0; x < CK_W; x++ ) {
			UW	v = ck.ground[y * CK_W + x];

			if ( x >= r.left && x < r.right && y >= r.top && y < r.bottom ) {
				if ( fs == CP_FS_FILL ) v = colour;
				else if ( fs == CP_FS_COPY ) v = p0[( y - r.top ) * CK_W + ( x - r.left )];
				else v ^= 0x00FFFFFFU;
			}
			c[y * CK_W + x] = v;
		}
	}
	knl_memset(par, 0, sizeof(par));
	par[CP_PD_COLOUR] = colour;
	par[CP_PD_PITCH] = CK_PITCH;
	par[CP_PD_OL] = (UW)r.left;
	par[CP_PD_OT] = (UW)r.top;
	par[CP_PD_W] = (UW)( r.right - r.left );
	par[CP_PD_H] = (UW)( r.bottom - r.top );
	d.back = ck.back_g;
	d.pitch = CK_PITCH;
	d.box = all;
	d.rows = CK_H;
	t0 = ticks();
	tk_loc_mtx(cg.mtx, TMO_FEVR);
	er = lay_one(&d, ck.ground, CK_PITCH, fs, &r, par, ( fs == CP_FS_COPY ) ? ck.pix[0] : NULL,
		     (UD)CK_PITCH * CK_H, &jobs);
	tk_unl_mtx(cg.mtx);
	gpu_us = ticks_us(ticks() - t0);
	if ( fs == CP_FS_FILL ) {
		for ( y = 0; y < CK_H; y++ ) {
			for ( x = 0; x < CK_W; x++ ) {
				if ( g[y * CK_W + x] != colour ) continue;
				n++;
				if ( x < x0 ) x0 = x;
				if ( x > x1 ) x1 = x;
				if ( y < y0 ) y0 = y;
				if ( y > y1 ) y1 = y;
			}
		}
		tm_printf((UB *)"v3d compose: check %s: the colour is in %ld pixels within (%d,%d)-(%d,%d), "
			  "meant for (%d,%d)-(%d,%d)\n", name, n, x0, y0, x1, y1,
			  r.left, r.top, r.right - 1, r.bottom - 1);
	}
	same = ck_compare(name, &all, 0, 0, ( er < E_OK ) ? er : 0, jobs, gpu_us, 0);
	ph_print(name, &cg.ph, 1);
	return same;
}

/* The time a part takes either way, the least of a few, and where the GPU's least went */
LOCAL void ck_time( CONST T_DPRECT *box, CONST T_CPWIN *w, INT n, UD *p_cpu, UD *p_gpu, PHASE *p_ph )
{
	UD	t0, c = (UD)-1, g = (UD)-1, us, jobs;
	INT	i;

	for ( i = 0; i < CK_REPEAT; i++ ) {
		knl_wm_hold(TRUE);
		t0 = ticks();
		ck_cpu(box, w, n);
		us = ticks_us(ticks() - t0);
		if ( us < c ) c = us;
		t0 = ticks();
		(void)ck_gpu(box, w, n, &jobs);
		us = ticks_us(ticks() - t0);
		if ( us < g ) {
			g = us;
			*p_ph = cg.ph;
		}
		knl_wm_hold(FALSE);
	}
	*p_cpu = c;
	*p_gpu = g;
}

/* From the times of a small and a whole part, the size from which the GPU is quicker */
LOCAL UD ck_threshold( CONST char *what, CONST T_CPWIN *w, INT n )
{
	static CONST T_DPRECT	part[3] = {
		{ 256, 192, 320, 256 },			/* 64 x 64 */
		{ 384, 256, 640, 512 },			/* 256 x 256 */
		{ 0, 0, CK_W, CK_H }
	};
	UD	cpu[3], gpu[3], a[3], thr;
	PHASE	ph[3];
	INT	i;
	char	name[48];

	for ( i = 0; i < 3; i++ ) {
		ck_time(&part[i], w, n, &cpu[i], &gpu[i], &ph[i]);
		a[i] = (UD)( part[i].right - part[i].left ) * (UD)( part[i].bottom - part[i].top );
	}
	for ( i = 0; i < 3; i++ ) {
		tm_sprintf((UB *)name, (UB *)"time %s %dx%d", what, part[i].right - part[i].left,
			   part[i].bottom - part[i].top);
		ph_print(name, &ph[i], 1);
	}
	if ( gpu[2] >= cpu[2] ) {
		thr = (UD)-1;				/* not quicker even for all of it */
	} else if ( gpu[0] <= cpu[0] ) {
		thr = a[0];
	} else {
		thr = a[0] + ( gpu[0] - cpu[0] ) * ( a[2] - a[0] ) / ( ( cpu[2] - gpu[2] ) + ( gpu[0] - cpu[0] ) );
	}
	if ( thr == (UD)-1 ) {
		tm_printf((UB *)"v3d compose: time %s: 64x64 GPU %ld us / processor %ld us, 256x256 %ld / %ld us, "
			  "%dx%d %ld / %ld us (%ld / %ld ns a pixel); the processor for every size\n",
			  what, gpu[0], cpu[0], gpu[1], cpu[1], CK_W, CK_H, gpu[2], cpu[2],
			  gpu[2] * 1000 / a[2], cpu[2] * 1000 / a[2]);
	} else {
		tm_printf((UB *)"v3d compose: time %s: 64x64 GPU %ld us / processor %ld us, 256x256 %ld / %ld us, "
			  "%dx%d %ld / %ld us (%ld / %ld ns a pixel); the GPU from %ld pixels\n",
			  what, gpu[0], cpu[0], gpu[1], cpu[1], CK_W, CK_H, gpu[2], cpu[2],
			  gpu[2] * 1000 / a[2], cpu[2] * 1000 / a[2], thr);
	}
	return thr;
}

/* The scenes, every one both ways and compared. TRUE when all come out the same */
LOCAL BOOL ck_scenes( T_CPWIN *w )
{
	T_DPRECT	all = { 0, 0, CK_W, CK_H }, part = { 112, 96, 688, 520 };
	INT		i;

	ck.nscene = ck.npass = 0;

	/* the ground alone, all of it and a part: tiles loaded and stored, shared with the processor */
	ck_scene("ground", &all, w, 0);
	ck_scene("ground in a part", &part, w, 0);

	/* one draw of one thing: the pixels reached, memory read, the tile buffer read */
	ck_one("fill", CP_FS_FILL);
	ck_one("copy", CP_FS_COPY);
	ck_one("invert", CP_FS_INVERT);

	/* one solid window: the draws, the shaders, the windows' memory */
	ck_win(&w[0], 0, 40, 30, 500, 380);
	ck_scene("one window", &all, w, 1);

	/* solid windows, square, nothing round them */
	ck_win(&w[0], 0, 40, 30, 500, 380);
	ck_win(&w[1], 1, 300, 200, 560, 420);
	ck_scene("plain", &all, w, 2);

	/* frames washed and tinted, still solid */
	ck_look(&w[0], 255, 0, 0, 0);
	ck_look(&w[1], 255, 0, 0, 0);
	ck_scene("washed", &all, w, 2);

	/* frames seen through, one window seen through all over */
	ck_look(&w[0], 150, 0, 0, 0);
	ck_look(&w[1], 150, 0, 0, 0);
	w[1].a_work = 200;
	ck_scene("see-through", &all, w, 2);

	/* round corners and shadows */
	ck_win(&w[0], 0, 40, 30, 500, 380);
	ck_win(&w[1], 1, 300, 200, 560, 420);
	ck_look(&w[0], 255, 12, 0, 150);
	ck_look(&w[1], 170, 12, 0, 150);
	ck_scene("corners", &all, w, 2);

	/* what shows through spread, behind three windows overlapping */
	ck_win(&w[0], 0, 40, 30, 500, 380);
	ck_win(&w[1], 1, 300, 200, 560, 420);
	ck_win(&w[2], 2, 160, 120, 420, 300);
	for ( i = 0; i < 3; i++ ) ck_look(&w[i], 140, 8, 6, 120);
	w[2].a_work = 190;
	ck_scene("spread", &all, w, 3);
	ck_scene("spread in a part", &part, w, 3);

	/* a window opening over another */
	ck_win(&w[0], 0, 40, 30, 500, 380);
	ck_look(&w[0], 255, 10, 0, 150);
	ck_win(&w[1], 1, 300, 200, 560, 420);
	ck_look(&w[1], 200, 10, 0, 0);
	w[1].shown.left = 328;
	w[1].shown.top = 221;
	w[1].shown.right = 832;
	w[1].shown.bottom = 599;
	w[1].fade = 160;
	ck_scene("opening", &all, w, 2);

	/* many windows, some spread, more draws than one job holds */
	for ( i = 0; i < CK_NWIN; i++ ) {
		ck_win(&w[i], i, 20 + ( i * 70 ) % 640, 10 + ( i * 50 ) % 480, 300 + ( i % 3 ) * 40,
		       200 + ( i % 4 ) * 30);
		ck_look(&w[i], ( i % 2 ) ? 160 : 255, i % 13, ( i % 4 == 1 ) ? 4 : 0, ( i % 3 ) ? 100 : 0);
	}
	ck_scene("many", &all, w, CK_NWIN);

	return ( ck.npass == ck.nscene );
}

/*
 * The check. TRUE when the GPU builds the scenes as the processor does;
 * the GPU is then used from now on for the parts the times say, in the
 * ways with which the scenes came out the same.
 */
EXPORT BOOL v3d_compose_check( void )
{
	T_CPWIN		w[CK_NWIN];
	INT		k;
	BOOL		ok = FALSE;
	char		fn[96];

	if ( !cg.ready ) {
		tm_printf((UB *)"v3d compose: check skipped: not set up\n");
		return FALSE;
	}
	if ( !ck_alloc() ) {
		tm_printf((UB *)"v3d compose: check skipped: no memory for the scenes\n");
		return FALSE;
	}
	for ( k = 0; k < (INT)( sizeof(ck_feats) / sizeof(ck_feats[0]) ) && !ok; k++ ) {
		cg.feat = ck_feats[k];
		cg.failed = FALSE;
		feat_name(cg.feat, fn);
		tm_printf((UB *)"v3d compose: check start, with%s\n", fn);
		ok = ck_scenes(w) && !cg.failed;
		if ( !ok ) {
			tm_printf((UB *)"v3d compose: check with%s: %d of %d scenes the same\n", fn, ck.npass, ck.nscene);
		}
	}
	if ( ok ) {
		ck_win(&w[0], 0, 40, 30, 500, 380);
		ck_win(&w[1], 1, 300, 200, 560, 420);
		cg.thr_plain = ck_threshold("plain", w, 2);
		ck_look(&w[0], 170, 12, 0, 150);
		ck_look(&w[1], 170, 12, 0, 150);
		cg.thr_fx = ck_threshold("with a look", w, 2);
	}
	ck_free();
	if ( ok && !cg.failed ) {
		cg.passed = TRUE;
		if ( cg.thr_plain == (UD)-1 && cg.thr_fx == (UD)-1 ) {
			tm_printf((UB *)"v3d compose: check passed (%d/%d) with%s; the processor is quicker at every "
				  "size, so it keeps compositing\n", ck.npass, ck.nscene, fn);
		} else {
			tm_printf((UB *)"v3d compose: check passed (%d/%d) with%s: to the GPU, parts of %ld pixels and "
				  "more without a look, %ld with one (0: never); a spread on the %s\n",
				  ck.npass, ck.nscene, fn,
				  ( cg.thr_plain == (UD)-1 ) ? (UD)0 : cg.thr_plain,
				  ( cg.thr_fx == (UD)-1 ) ? (UD)0 : cg.thr_fx,
				  cg.blur_ok ? "GPU" : "processor");
		}
	} else {
		tm_printf((UB *)"v3d compose: check FAILED (%d/%d scenes the same, even with plain jobs%s); "
			  "compositing stays on the processor\n", ck.npass, ck.nscene, cg.failed ? ", a job failed" : "");
	}
	return ok;
}

/* ---------------------------------------------------------------- without the tests */

#define CHECK_DELAY_MS		2000
#define CHECK_PRI		30
#define CHECK_STKSZ		32768

LOCAL void check_task( INT stacd, void *exinf )
{
	tk_dly_tsk(CHECK_DELAY_MS);
	(void)v3d_compose_check();
	tk_ext_tsk();
}

/*
 * The check in a task of its own, a little after the start (V3D=3; with
 * V3D=2 the tests on the board run it after theirs, v3d_test.c).
 */
EXPORT void v3d_compose_start( void )
{
	T_CTSK	ctsk;
	ID	tskid;

	if ( !cg.ready ) {
		return;
	}
	knl_memset(&ctsk, 0, sizeof(ctsk));
	ctsk.tskatr = TA_HLNG | TA_RNG0;
	ctsk.task = (FP)check_task;
	ctsk.itskpri = CHECK_PRI;
	ctsk.stksz = CHECK_STKSZ;
	tskid = tk_cre_tsk(&ctsk);
	if ( tskid <= 0 || tk_sta_tsk(tskid, 0) < E_OK ) {
		tm_printf((UB *)"v3d compose: the check could not start (%d)\n", (INT)tskid);
	}
}

/* ---------------------------------------------------------------- the check of compute dispatch */

#define CSD_WORDS	( CP_CSD_NWG * CP_CSD_WG_SIZE )
#define CSD_PATTERN	0xDEADBEEFU		/* what the buffer holds before the dispatch */

/*
 * One dispatch of the check's compute shader (cp_code_csd) through the
 * job queue, as a process's would go: CP_CSD_NWG workgroups of
 * CP_CSD_WG_SIZE invocations, invocation n writing 3 * n + 1 at word n
 * of the buffer. One workgroup a supergroup and one batch of 16 a
 * workgroup, so the dispatch is CP_CSD_NWG batches; from revision 6 on
 * the count is written as it is, before it one less. Compute dispatch
 * is offered for the time of the check alone; TRUE when every word came
 * back right, and the console tells which did not.
 */
EXPORT BOOL v3d_csd_check( void )
{
	T_GPUBO		*bo[3] = { NULL, NULL, NULL };	/* code, uniforms, the buffer */
	T_GPUINFO	gi;
	T_GPUJOB	job;
	UW		unif[sizeof(cp_unif_csd) / 8], fill[CSD_WORDS], *res;
	UW		buf;
	UINT		i, nunif = sizeof(cp_unif_csd) / 8, bad = 0;
	UD		t0;
	D		fence;
	ER		er = E_OK;
	BOOL		ok = FALSE;

	if ( knl_gpu_ref(&gi) < E_OK || gi.ver != CP_V3D_VER ) {
		return FALSE;
	}
	for ( i = 0; i < 3 && er >= E_OK; i++ ) {
		bo[i] = knl_gpu_bo_create(PAGE_SIZE, 0, &er);
	}
	if ( er < E_OK ) {
		tm_printf((UB *)"v3d: compute dispatch: no BO for the check (%d)\n", er);
		goto out;
	}
	buf = bo_gpu(bo[2]);
	for ( i = 0; i < nunif; i++ ) {
		unif[i] = ( cp_unif_csd[i][0] == CP_U_SSBO ) ? buf : cp_unif_csd[i][1];
	}
	for ( i = 0; i < CSD_WORDS; i++ ) {
		fill[i] = CSD_PATTERN;
	}
	bo_put(bo[0], 0, cp_code_csd, sizeof(cp_code_csd));
	bo_put(bo[1], 0, unif, nunif * 4);
	bo_put(bo[2], 0, fill, sizeof(fill));

	knl_memset(&job, 0, sizeof(job));
	job.kind = GPU_JOB_CSD;
	job.u.csd.cfg[0] = (UW)CP_CSD_NWG << 16;		/* workgroups in x, from 0 */
	job.u.csd.cfg[1] = 1U << 16;
	job.u.csd.cfg[2] = 1U << 16;
	job.u.csd.cfg[3] = ( 1U << 8 ) | ( 0U << 12 ) | CP_CSD_WG_SIZE;	/* a workgroup a supergroup, one batch */
	job.u.csd.cfg[4] = ( gi.rev >= 6 ) ? CP_CSD_NWG : CP_CSD_NWG - 1;
	job.u.csd.cfg[5] = bo_gpu(bo[0]) | ( CP_CSD_SINGLE_SEG ? 0x2U : 0 )
					 | ( CP_CSD_THREADS == 4 ? 0x1U : 0 );
	job.u.csd.cfg[6] = bo_gpu(bo[1]);

	v3d_csd_offer(TRUE);
	t0 = ticks();
	fence = knl_gpu_submit(&job, bo, 3);
	er = ( fence > 0 ) ? knl_gpu_wait(fence, 1000 * 1000) : (ER)fence;
	if ( er < E_OK ) {
		tm_printf((UB *)"v3d: compute dispatch: the check's job failed (%d)\n", er);
		goto out;
	}
	res = (UW *)PA2VA(knl_gpu_bo_page(bo[2], 0));
	knl_gpu_dcache_clean_inval((UBINT)res, sizeof(fill));
	for ( i = 0; i < CSD_WORDS; i++ ) {
		if ( res[i] != 3 * i + 1 ) {
			if ( bad < 4 ) {
				tm_printf((UB *)"v3d: compute dispatch: word %u is %08x, not %08x\n", i, res[i], 3 * i + 1);
			}
			bad++;
		}
	}
	ok = ( bad == 0 );
	tm_printf((UB *)"v3d: compute dispatch: %u of %u words right in %lu us; %s\n",
		  CSD_WORDS - bad, CSD_WORDS, ticks_us(ticks() - t0),
		  ok ? "offered (OpenGL ES 3.1)" : "not offered");
out:
	if ( !ok ) {
		v3d_csd_offer(FALSE);
	}
	for ( i = 0; i < 3; i++ ) {
		if ( bo[i] != NULL ) knl_gpu_bo_put(bo[i]);
	}
	return ok;
}

#endif /* RPI5 */
