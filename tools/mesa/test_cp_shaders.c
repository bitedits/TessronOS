/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	test_cp_shaders.c
 *	The compositing shaders checked on the host, before any board sees
 *	them (docs/tessronos/13-gpu.md 13.10.4).
 *
 *	Each shader is built as gen_cp_shaders.c builds it, prepared as it
 *	is handed to the V3D compiler, and then run here by an interpreter
 *	of NIR, pixel by pixel: the parameters and the memory it reads are
 *	the test's, what is behind a pixel is read as the tile buffer gives
 *	it (eight bits through a half float), and what it writes is turned
 *	back into eight bits the same way; what it writes to memory goes to
 *	memory. Random scenes are laid both with compose.c (built for the
 *	host, hostshim/) and with the shaders, and the two screens compared.
 *
 *	The shaders' way is laid as device/gpu/v3d/v3d_compose.c lays it:
 *	the draws gathered into jobs, each job's draws cut to what shows of
 *	them and its supertiles classed with v3d_cp_split.h, and then each
 *	job run as the GPU runs it -- a rendered supertile loaded from the
 *	ground, the back buffer or a window's pixels, the draws over it in
 *	order, and stored -- while the processor's share of the ground is
 *	copied last, so that a job that reads or writes it would show. The
 *	ways of doing it that the kernel can turn on and off are chosen at
 *	random for each scene, as are the tiles' size and how many
 *	supertiles a frame may have. Every read the shaders make is checked
 *	to fall inside the buffer it is meant for, and every write to land
 *	where it may.
 *
 *	What this does not check is the V3D compiler's translation and the
 *	hardware; the check at start-up on the board does.
 */

#define main gen_cp_main
#include "gen_cp_shaders.c"
#undef main

#include "util/half_float.h"
#include "compiler/nir/nir_constant_expressions.h"
#include "hc.h"

/* The numbers the kernel uses, from the header it is built with */
typedef uint8_t		UB;
typedef uint32_t	UW;
typedef uint64_t	UD;
typedef int		INT;
typedef unsigned	UINT;
#define LOCAL		static
#define CONST		const
#include "../../device/gpu/v3d/mesa/v3d_cp_shaders.h"
#include "../../device/gpu/v3d/v3d_cp_split.h"

/* ---------------------------------------------------------------- memory */

typedef struct {
	uint32_t	base;
	uint8_t		*p;
	uint32_t	size;
	int		wr;		/* a shader may write it */
} REGION;

#define NREGION		64
static REGION	region[NREGION];
static int	nregion;
static long	oob, bad_writes;

static void region_set( uint32_t base, void *p, uint32_t size, int wr )
{
	int	i;

	for ( i = 0; i < nregion; i++ ) {
		if ( region[i].base == base ) break;
	}
	if ( i == nregion ) {
		if ( nregion == NREGION ) die("regions");
		nregion++;
	}
	region[i].base = base;
	region[i].p = p;
	region[i].size = size;
	region[i].wr = wr;
}

static REGION *region_of( uint32_t addr )
{
	int	i;

	for ( i = 0; i < nregion; i++ ) {
		REGION	*r = &region[i];

		if ( addr >= r->base && addr + 4 <= r->base + r->size ) return r;
	}
	return NULL;
}

static uint32_t mem_read( uint32_t addr )
{
	REGION		*r = region_of(addr);
	uint32_t	v;

	if ( r == NULL ) {
		oob++;
		return 0xDEADBEEF;
	}
	memcpy(&v, r->p + ( addr - r->base ), 4);
	return v;
}

/* A word written by a shader ('shader'), which may go only where shaders write, or by a tile's store */
static void mem_write( uint32_t addr, uint32_t v, int shader )
{
	REGION	*r = region_of(addr);

	if ( r == NULL || ( shader && !r->wr ) ) {
		bad_writes++;
		return;
	}
	memcpy(r->p + ( addr - r->base ), &v, 4);
}

/* ---------------------------------------------------------------- the interpreter */

typedef struct {
	float		fx, fy;		/* the fragment's coordinates, the pixel's centre */
	const uint32_t	*par;
	const uint32_t	*buf;
	uint32_t	dst;		/* what is behind it */
	int		discarded;
	int		wrote;
	float		out[4];
} PIX;

enum { J_NONE, J_BREAK, J_CONTINUE, J_HALT };

typedef struct {
	nir_shader		*s;
	nir_function_impl	*impl;
	nir_const_value		(*val)[NIR_MAX_VEC_COMPONENTS];
	nir_block		*prev;
} RUN;

static float behind( uint32_t dst, int c )
{
	return _mesa_half_to_float(_mesa_float_to_half(( ( dst >> ( 8 * c ) ) & 0xFF ) / 255.0f));
}

static int exec_list( RUN *r, struct exec_list *list, PIX *px );

static void exec_alu( RUN *r, nir_alu_instr *alu )
{
	nir_const_value	src[NIR_ALU_MAX_INPUTS][NIR_MAX_VEC_COMPONENTS];
	nir_const_value	*srcs[NIR_ALU_MAX_INPUTS];
	unsigned	bit_size = 0, i, j;

	memset(src, 0, sizeof(src));
	if ( !nir_alu_type_get_type_size(nir_op_infos[alu->op].output_type) ) {
		bit_size = alu->def.bit_size;
	}
	for ( i = 0; i < nir_op_infos[alu->op].num_inputs; i++ ) {
		if ( bit_size == 0 && !nir_alu_type_get_type_size(nir_op_infos[alu->op].input_types[i]) ) {
			bit_size = alu->src[i].src.ssa->bit_size;
		}
		for ( j = 0; j < nir_ssa_alu_instr_src_components(alu, i); j++ ) {
			src[i][j] = r->val[alu->src[i].src.ssa->index][alu->src[i].swizzle[j]];
		}
		srcs[i] = src[i];
	}
	if ( bit_size == 0 ) bit_size = 32;
	memset(r->val[alu->def.index], 0, sizeof(r->val[0]));
	nir_eval_const_opcode(alu->op, r->val[alu->def.index], NULL, alu->def.num_components, bit_size,
			      srcs, r->s->info.float_controls_execution_mode);
}

static int as_bool( nir_const_value v, unsigned bit_size )
{
	return ( bit_size == 1 ) ? v.b : ( v.u32 != 0 );
}

static int exec_intrinsic( RUN *r, nir_intrinsic_instr *in, PIX *px )
{
	nir_const_value	*d = in->intrinsic != nir_intrinsic_store_output
			     && nir_intrinsic_infos[in->intrinsic].has_dest ? r->val[in->def.index] : NULL;
	unsigned	c;

	switch ( in->intrinsic ) {
	case nir_intrinsic_load_input: {
		float	v[4] = { px->fx, px->fy, 0.0f, 1.0f };
		unsigned	comp = nir_intrinsic_component(in);

		for ( c = 0; c < in->num_components; c++ ) d[c].f32 = v[comp + c];
		return J_NONE;
	}
	case nir_intrinsic_load_uniform: {
		uint32_t	off = nir_intrinsic_base(in) + r->val[in->src[0].ssa->index][0].u32;

		if ( ( off & 3 ) != 0 ) die("a uniform at %u", off);
		for ( c = 0; c < in->num_components; c++ ) d[c].u32 = px->par[off / 4 + c];
		return J_NONE;
	}
	case nir_intrinsic_load_ssbo: {
		uint32_t	idx = r->val[in->src[0].ssa->index][0].u32;
		uint32_t	off = r->val[in->src[1].ssa->index][0].u32;

		if ( idx > 2 ) die("buffer %u", idx);
		for ( c = 0; c < in->num_components; c++ ) d[c].u32 = mem_read(px->buf[idx] + off + 4 * c);
		return J_NONE;
	}
	case nir_intrinsic_store_ssbo: {
		nir_const_value	*v = r->val[in->src[0].ssa->index];
		uint32_t	idx = r->val[in->src[1].ssa->index][0].u32;
		uint32_t	off = r->val[in->src[2].ssa->index][0].u32;
		unsigned	mask = nir_intrinsic_write_mask(in);

		if ( idx > 2 ) die("buffer %u", idx);
		for ( c = 0; c < in->src[0].ssa->num_components; c++ ) {
			if ( mask & ( 1u << c ) ) mem_write(px->buf[idx] + off + 4 * c, v[c].u32, 1);
		}
		return J_NONE;
	}
	case nir_intrinsic_load_helper_invocation:
	case nir_intrinsic_is_helper_invocation:
		memset(d, 0, sizeof(*d) * in->def.num_components);
		return J_NONE;
	case nir_intrinsic_load_output: {
		unsigned	comp = nir_intrinsic_component(in);

		for ( c = 0; c < in->num_components; c++ ) d[c].f32 = behind(px->dst, (int)( comp + c ));
		return J_NONE;
	}
	case nir_intrinsic_store_output: {
		unsigned	comp = nir_intrinsic_component(in), mask = nir_intrinsic_write_mask(in);
		nir_const_value	*v = r->val[in->src[0].ssa->index];

		for ( c = 0; c < in->src[0].ssa->num_components; c++ ) {
			if ( mask & ( 1u << c ) ) px->out[comp + c] = v[c].f32;
		}
		px->wrote = 1;
		return J_NONE;
	}
	case nir_intrinsic_terminate_if:
	case nir_intrinsic_demote_if:
		if ( as_bool(r->val[in->src[0].ssa->index][0], in->src[0].ssa->bit_size) ) {
			px->discarded = 1;
			return J_HALT;
		}
		return J_NONE;
	case nir_intrinsic_terminate:
	case nir_intrinsic_demote:
		px->discarded = 1;
		return J_HALT;
	default:
		die("the interpreter does not know %s", nir_intrinsic_infos[in->intrinsic].name);
	}
	return J_NONE;
}

static int exec_block( RUN *r, nir_block *b, PIX *px )
{
	nir_const_value	tmp[64][NIR_MAX_VEC_COMPONENTS];
	unsigned	n = 0, i;

	/* the phis together, from the block run before */
	nir_foreach_phi(phi, b) {
		nir_phi_src	*src = nir_phi_get_src_from_block(phi, r->prev);

		if ( n >= 64 ) die("phis");
		if ( src != NULL ) memcpy(tmp[n], r->val[src->src.ssa->index], sizeof(tmp[0]));
		else memset(tmp[n], 0, sizeof(tmp[0]));
		n++;
	}
	i = 0;
	nir_foreach_phi(phi, b) {
		memcpy(r->val[phi->def.index], tmp[i++], sizeof(tmp[0]));
	}

	nir_foreach_instr(instr, b) {
		int	j;

		switch ( instr->type ) {
		case nir_instr_type_phi:
			break;
		case nir_instr_type_alu:
			exec_alu(r, nir_instr_as_alu(instr));
			break;
		case nir_instr_type_load_const: {
			nir_load_const_instr	*lc = nir_instr_as_load_const(instr);

			memset(r->val[lc->def.index], 0, sizeof(r->val[0]));
			memcpy(r->val[lc->def.index], lc->value, sizeof(nir_const_value) * lc->def.num_components);
			break;
		}
		case nir_instr_type_undef:
			memset(r->val[nir_instr_as_undef(instr)->def.index], 0, sizeof(r->val[0]));
			break;
		case nir_instr_type_intrinsic:
			j = exec_intrinsic(r, nir_instr_as_intrinsic(instr), px);
			if ( j != J_NONE ) {
				r->prev = b;
				return j;
			}
			break;
		case nir_instr_type_jump: {
			nir_jump_instr	*jp = nir_instr_as_jump(instr);

			r->prev = b;
			if ( jp->type == nir_jump_break ) return J_BREAK;
			if ( jp->type == nir_jump_continue ) return J_CONTINUE;
			if ( jp->type == nir_jump_halt || jp->type == nir_jump_return ) return J_HALT;
			die("a jump of type %d", jp->type);
		}
		default:
			die("the interpreter does not know instruction type %d", instr->type);
		}
	}
	r->prev = b;
	return J_NONE;
}

static int exec_list( RUN *r, struct exec_list *list, PIX *px )
{
	foreach_list_typed(nir_cf_node, node, node, list) {
		int	j = J_NONE;

		switch ( node->type ) {
		case nir_cf_node_block:
			j = exec_block(r, nir_cf_node_as_block(node), px);
			break;
		case nir_cf_node_if: {
			nir_if	*nif = nir_cf_node_as_if(node);
			int	c = as_bool(r->val[nif->condition.ssa->index][0], nif->condition.ssa->bit_size);

			j = exec_list(r, c ? &nif->then_list : &nif->else_list, px);
			break;
		}
		case nir_cf_node_loop: {
			nir_loop	*loop = nir_cf_node_as_loop(node);
			long		guard = 0;

			if ( nir_loop_has_continue_construct(loop) ) die("a loop with a continue construct");
			for (;;) {
				j = exec_list(r, &loop->body, px);
				if ( j == J_BREAK ) {
					j = J_NONE;
					break;
				}
				if ( j == J_HALT ) break;
				if ( ++guard > 100000 ) die("a loop that does not end");
			}
			break;
		}
		default:
			die("cf node %d", node->type);
		}
		if ( j != J_NONE ) return j;
	}
	return J_NONE;
}

static RUN	runs[NFS];

static void runs_make( void )
{
	nir_shader	*(*make[NFS])( void ) = { fs_piece, fs_shadow, fs_halve, fs_smooth3, fs_smooth5,
						  fs_double, fs_moving, fs_fill, fs_copy, fs_invert };
	int		i;

	for ( i = 0; i < NFS; i++ ) {
		RUN	*r = &runs[i];

		r->s = make[i]();
		prepare(r->s);
		r->impl = nir_shader_get_entrypoint(r->s);
		nir_index_ssa_defs(r->impl);
		r->val = calloc(r->impl->ssa_alloc + 1, sizeof(r->val[0]));
	}
}

/*
 * One pixel (x, y) of a frame at (fx, fy) through fragment shader fs:
 * TRUE when it wrote the tile buffer, the word in *out.
 */
static int run_pixel( int fs, int x, int y, int fx, int fy, const uint32_t *par, const uint32_t *buf,
		      uint32_t dst, uint32_t *out )
{
	RUN	*r = &runs[fs];
	PIX	px;
	int	c;

	memset(&px, 0, sizeof(px));
	px.fx = (float)( x - fx ) + 0.5f;
	px.fy = (float)( y - fy ) + 0.5f;
	px.par = par;
	px.buf = buf;
	px.dst = dst;
	r->prev = NULL;
	exec_list(r, &r->impl->body, &px);
	if ( px.discarded ) return 0;
	if ( !px.wrote ) {
		die("shader %d wrote nothing", fs);
	}
	*out = 0;
	for ( c = 0; c < 4; c++ ) {
		float	h = _mesa_half_to_float(_mesa_float_to_half(px.out[c]));

		if ( h < 0.0f ) h = 0.0f;
		if ( h > 1.0f ) h = 1.0f;
		*out |= (uint32_t)( h * 255.0f + 0.5f ) << ( 8 * c );
	}
	return 1;
}

/* ---------------------------------------------------------------- the scenes */

#define SW		128
#define SH		96
#define PITCH		( SW * 4 )
#define NW		3

#define A_BACK		0x10000000U
#define A_GROUND	0x20000000U
#define A_T1		0x30000000U
#define A_T2		0x40000000U
#define A_COV		0x50000000U
#define A_WIN		0x60000000U

#define T_WORDS		( SW * ( SH + ( SH + 1 ) / 2 ) )	/* cp_spread_bytes(PITCH, SH) / 4 */

static uint32_t	ground[SW * SH], back_c[SW * SH], back_g[SW * SH], t1[T_WORDS], t2[T_WORDS];
static uint32_t	pix[NW][SW * SH];
static uint8_t	cov[HC_ROUND_MAX + 1][HC_ROUND_MAX * HC_ROUND_MAX + 4];
static uint32_t	seed = 1;
static long	draws, px_drawn, px_copied, px_culled, st_rendered, st_win, st_cpu, st_skip;

static uint32_t rnd( void )
{
	seed ^= seed << 13;
	seed ^= seed >> 17;
	seed ^= seed << 5;
	return seed;
}

static int rr( int lo, int hi )		/* lo..hi */
{
	return lo + (int)( rnd() % (uint32_t)( hi - lo + 1 ) );
}

static uint32_t cov_at( int rad )
{
	if ( rad <= 0 ) return A_COV;
	return A_COV + (uint32_t)rad * 0x1000;
}

static HC_RECT rect( int l, int t, int r, int b )
{
	HC_RECT	x = { l, t, r, b };

	return x;
}

static CP_SR sr_of( HC_RECT r )
{
	CP_SR	s = { r.left, r.top, r.right, r.bottom };

	return s;
}

static int round_of( const HC_WIN *w, HC_RECT r )
{
	int	rad = hc_round_of(r, w->round);

	return ( hc_corner_cov(rad) == NULL ) ? 0 : rad;
}

/* ---------------------------------------------------------------- the shaders' way, as jobs */

#define ALIGN_PX	16
#define HD_MAX		2048
#define HJ_MAX		64
#define HPOOL		16384
#define HKEEP		64
#define HST_MAX		256

typedef struct {
	int		fs;
	CP_SR		r;		/* in its job's frame */
	uint32_t	par[CP_NPARAM], buf[CP_NBUF];
	int		wok;		/* a copy of a window's pixels */
	uint32_t	wsrc;		/* and where: the address of its corner, its pitch, where it stands */
	unsigned	wpitch;
	int		wl, wt;
} HDRAW;

typedef struct {
	int		fx, fy, fw, fh;
	int		fit;		/* loaded from and stored to the back buffer: cut to its draws */
	uint32_t	load, store;	/* the frame's first pixel in each; 0 none */
	unsigned	lpitch, spitch;
	int		d0, nd;
	CP_GRID		g;
	UB		cls[HST_MAX];
	int		base[HST_MAX];
} HJOB;

static HDRAW	hd[HD_MAX];
static CP_DR	hdr[HD_MAX];
static int	nhd;
static HJOB	hj[HJ_MAX];
static int	nhj;
static HJOB	*hcur;
static CP_SR	pool[HPOOL], keep[HKEEP];
static int	npool, nkeep, keep_over;
static HC_RECT	hbox;
static uint8_t	cpu_mask[SW * SH];
static uint32_t	frame[SW * SH * 8];

/* What the kernel can turn on and off, and its sizes, for this scene */
static int	f_cull, f_tiles, f_wload, f_cpu, ht_tile, ht_maxst, jdraws_max, nvar_max;
static long	faults;

static void fault( const char *what, int x, int y )
{
	if ( faults++ < 8 ) fprintf(stderr, "  %s at (%d,%d)\n", what, x, y);
}

static HJOB *hjob_open( int fx, int fy, int fw, int fh, uint32_t load, unsigned lpitch, uint32_t store, unsigned spitch )
{
	HJOB	*j;

	if ( nhj >= HJ_MAX ) die("jobs");
	if ( fw <= 0 || fh <= 0 ) die("an empty frame");
	j = &hj[nhj++];
	memset(j, 0, sizeof(*j));
	j->fx = fx;  j->fy = fy;  j->fw = fw;  j->fh = fh;
	j->load = load;  j->lpitch = lpitch;
	j->store = store;  j->spitch = spitch;
	j->d0 = nhd;
	return j;
}

static void hdraw( HJOB *j, int fs, HC_RECT r, const uint32_t *par, const uint32_t *buf, const HDRAW *win )
{
	HDRAW	*d;

	if ( r.left < j->fx ) r.left = j->fx;
	if ( r.top < j->fy ) r.top = j->fy;
	if ( r.right > j->fx + j->fw ) r.right = j->fx + j->fw;
	if ( r.bottom > j->fy + j->fh ) r.bottom = j->fy + j->fh;
	if ( r.right <= r.left || r.bottom <= r.top ) return;
	if ( nhd >= HD_MAX ) die("draws");
	if ( j->nd > 0 && j->d0 + j->nd != nhd ) die("a job's draws apart");
	d = &hd[nhd];
	memset(d, 0, sizeof(*d));
	d->fs = fs;
	d->r = sr_of(r);
	memcpy(d->par, par, sizeof(d->par));
	memcpy(d->buf, buf, sizeof(d->buf));
	if ( win != NULL && fs == CP_FS_COPY ) {
		d->wok = 1;
		d->wsrc = win->wsrc;
		d->wpitch = win->wpitch;
		d->wl = win->wl;
		d->wt = win->wt;
	}
	if ( j->nd == 0 ) j->d0 = nhd;
	nhd++;
	j->nd++;
	draws++;
}

static void keep_add( int l, int t, int r, int b )
{
	if ( nkeep >= HKEEP ) {
		keep_over = 1;
		return;
	}
	keep[nkeep].l = l;  keep[nkeep].t = t;  keep[nkeep].r = r;  keep[nkeep].b = b;
	nkeep++;
}

/* The current job, or a new one over the box loaded from the back buffer */
static HJOB *hseg( void )
{
	uint32_t	at = A_BACK + (uint32_t)hbox.top * PITCH + (uint32_t)hbox.left * 4;

	if ( hcur != NULL && hcur->nd < jdraws_max ) return hcur;
	hcur = hjob_open(hbox.left, hbox.top, hbox.right - hbox.left, hbox.bottom - hbox.top, at, PITCH, at, PITCH);
	hcur->fit = 1;
	return hcur;
}

static void hlay_rect( int fs, int l, int t, int r, int b, const uint32_t *par, const uint32_t *buf, const HDRAW *win )
{
	HC_RECT	c = rect(l, t, r, b);

	if ( c.left < hbox.left ) c.left = hbox.left;
	if ( c.top < hbox.top ) c.top = hbox.top;
	if ( c.right > hbox.right ) c.right = hbox.right;
	if ( c.bottom > hbox.bottom ) c.bottom = hbox.bottom;
	if ( c.right <= c.left || c.bottom <= c.top ) return;
	hdraw(hseg(), fs, c, par, buf, win);
}

/* Where level k of a spread is kept in T1 */
static uint32_t hlv_at( int k )
{
	return A_T1 + (uint32_t)cp_lv_row(SH, k) * PITCH;
}

/* One step of a spread, a job over the rectangles r[0..n-1] of a level */
static void hstep( int fs, uint32_t from, uint32_t to, HC_RECT v, const HC_RECT *r, int n, uint32_t *par, uint32_t *buf )
{
	HC_RECT	u = r[0];
	HJOB	*j;
	int	i;

	for ( i = 1; i < n; i++ ) {
		if ( r[i].left < u.left ) u.left = r[i].left;
		if ( r[i].top < u.top ) u.top = r[i].top;
		if ( r[i].right > u.right ) u.right = r[i].right;
		if ( r[i].bottom > u.bottom ) u.bottom = r[i].bottom;
	}
	u.left &= ~( ALIGN_PX - 1 );
	j = hjob_open(u.left, u.top, u.right - u.left, u.bottom - u.top, 0, 0,
		      to + (uint32_t)u.top * PITCH + (uint32_t)u.left * 4, PITCH);
	par[CP_PL_VL] = (uint32_t)v.left;
	par[CP_PL_VT] = (uint32_t)v.top;
	par[CP_PL_VR] = (uint32_t)v.right;
	par[CP_PL_VB] = (uint32_t)v.bottom;
	buf[CP_B_SRC] = from;
	for ( i = 0; i < n; i++ ) hdraw(j, fs, r[i], par, buf, NULL);
}

/*
 * What shows through the parts of a window that are not solid, spread
 * into T2, a job for each step: halved into T1, smoothed into T2 under
 * the screen's rows, doubled back into T1 and last into T2
 */
static void hspread( const HC_PIECE *pc, int npc, int rad )
{
	HC_SPREAD	sp[5];
	HC_RECT		r[5];
	uint32_t	par[CP_NPARAM], buf[CP_NBUF] = { A_BACK, A_BACK, A_COV }, below = A_T2 + SH * PITCH;
	int		i, k, n = 0, depth;

	for ( i = 0; i < npc; i++ ) {
		if ( pc[i].a < 255 && hc_spread_plan(hbox, pc[i].r, rad, &sp[n]) ) {
			keep_add(sp[n].down[0].left, sp[n].down[0].top, sp[n].down[0].right, sp[n].down[0].bottom);
			n++;
		}
	}
	if ( n == 0 ) return;
	depth = sp[0].depth;
	memset(par, 0, sizeof(par));
	par[CP_PL_PITCH] = PITCH;
	par[CP_PL_A] = (uint32_t)sp[0].w[0];
	par[CP_PL_B] = (uint32_t)sp[0].w[1];
	par[CP_PL_M] = (uint32_t)sp[0].w[2];

	for ( k = 1; k <= depth; k++ ) {
		for ( i = 0; i < n; i++ ) r[i] = sp[i].down[k];
		hstep(CP_FS_HALVE, ( k == 1 ) ? A_BACK : hlv_at(k - 1), hlv_at(k), sp[0].valid[k - 1], r, n, par, buf);
	}
	for ( i = 0; i < n; i++ ) r[i] = sp[i].up[depth];
	hstep(( sp[0].w[0] != 0 ) ? CP_FS_SMOOTH5 : CP_FS_SMOOTH3, ( depth == 0 ) ? A_BACK : hlv_at(depth),
	      ( depth == 0 ) ? A_T2 : below, sp[0].valid[depth], r, n, par, buf);
	for ( k = depth; k >= 1; k-- ) {
		for ( i = 0; i < n; i++ ) r[i] = sp[i].up[k - 1];
		hstep(CP_FS_DOUBLE, ( k == depth ) ? below : hlv_at(k), ( k == 1 ) ? A_T2 : hlv_at(k - 1),
		      sp[0].valid[k], r, n, par, buf);
	}
}

/* A window, as v3d_compose.c lays it */
static void hwindow( const HC_WIN *w, int k )
{
	HC_PIECE	pc[5];
	HC_RECT		e;
	HDRAW		win;
	uint32_t	par[CP_NPARAM], cpar[CP_NPARAM], buf[CP_NBUF];
	CP_SR		so, se, sp, sr[CP_SPLIT_SHADOW_MAX + CP_SPLIT_PIECE_MAX];
	int		rad, npc, i, m, n, brad, spread = 0, sfs[CP_SPLIT_PIECE_MAX];

	if ( hc_moving(w) ) {
		int	ow = w->outer.right - w->outer.left, oh = w->outer.bottom - w->outer.top;
		int	sw = w->shown.right - w->shown.left, sh = w->shown.bottom - w->shown.top;

		if ( sw <= 0 || sh <= 0 || ow <= 0 || oh <= 0 ) return;
		rad = hc_round_of(w->shown, w->round);
		if ( hc_corner_cov(rad) == NULL ) rad = 0;
		memset(par, 0, sizeof(par));
		par[CP_PM_PITCH] = w->pitch;
		par[CP_PM_SL] = (uint32_t)w->shown.left;
		par[CP_PM_ST] = (uint32_t)w->shown.top;
		par[CP_PM_SR] = (uint32_t)w->shown.right;
		par[CP_PM_SB] = (uint32_t)w->shown.bottom;
		par[CP_PM_STEPX] = ( (uint32_t)ow << 16 ) / (uint32_t)sw;
		par[CP_PM_STEPY] = ( (uint32_t)oh << 16 ) / (uint32_t)sh;
		par[CP_PM_OL] = (uint32_t)w->outer.left;
		par[CP_PM_OT] = (uint32_t)w->outer.top;
		par[CP_PM_OR] = (uint32_t)w->outer.right;
		par[CP_PM_OB] = (uint32_t)w->outer.bottom;
		par[CP_PM_WL] = (uint32_t)w->work.left;
		par[CP_PM_WT] = (uint32_t)w->work.top;
		par[CP_PM_WR] = (uint32_t)w->work.right;
		par[CP_PM_WB] = (uint32_t)w->work.bottom;
		par[CP_PM_AW] = w->a_work * w->fade / 255U;
		par[CP_PM_AF] = w->a_frame * w->fade / 255U;
		par[CP_PM_RAD] = (uint32_t)rad;
		par[CP_PM_TINT] = w->tint;
		par[CP_PM_TSTR] = w->tint_str;
		par[CP_PM_WASH] = w->wash;
		par[CP_PM_WSTR] = w->wash_str;
		par[CP_PM_BWASH] = w->bar_wash;
		par[CP_PM_BSTR] = w->bar_str;
		par[CP_PM_BAND] = ( w->band.right > w->band.left ) ? 1 : 0;
		buf[CP_B_SRC] = A_WIN + (uint32_t)k * 0x1000000;
		buf[CP_B_AUX] = A_BACK;
		buf[CP_B_COV] = cov_at(rad);
		hlay_rect(CP_FS_MOVING, w->shown.left, w->shown.top, w->shown.right, w->shown.bottom, par, buf, NULL);
		return;
	}

	rad = round_of(w, w->outer);
	buf[CP_B_SRC] = A_WIN + (uint32_t)k * 0x1000000;
	buf[CP_B_AUX] = A_BACK;
	buf[CP_B_COV] = cov_at(rad);
	so = sr_of(w->outer);
	if ( hc_shadow_extent(w, &e) ) {
		memset(par, 0, sizeof(par));
		par[CP_PS_OL] = (uint32_t)w->outer.left;
		par[CP_PS_OT] = (uint32_t)w->outer.top;
		par[CP_PS_OR] = (uint32_t)w->outer.right;
		par[CP_PS_OB] = (uint32_t)w->outer.bottom;
		par[CP_PS_DX] = (uint32_t)w->shd_dx;
		par[CP_PS_DY] = (uint32_t)w->shd_dy;
		par[CP_PS_SIZE] = (uint32_t)w->shd_size;
		par[CP_PS_DARK] = w->shd_dark;
		par[CP_PS_RAD] = (uint32_t)rad;
		se = sr_of(e);
		n = cp_split_shadow(&se, &so, rad, sr);
		for ( i = 0; i < n; i++ ) {
			hlay_rect(CP_FS_SHADOW, sr[i].l, sr[i].t, sr[i].r, sr[i].b, par, buf, NULL);
		}
	}
	npc = hc_pieces((uint8_t *)back_g, PITCH, hbox, SH, w, pc);
	brad = ( w->blur > HC_BLUR_MAX ) ? HC_BLUR_MAX : w->blur;
	if ( brad > 0 ) {
		for ( i = 0; i < npc; i++ ) {
			if ( pc[i].a < 255 ) spread = 1;
		}
	}
	if ( spread ) {
		hcur = NULL;
		hspread(pc, npc, brad);
		buf[CP_B_AUX] = A_T2;
	}
	memset(cpar, 0, sizeof(cpar));
	cpar[CP_PD_PITCH] = w->pitch;
	cpar[CP_PD_OL] = (uint32_t)w->outer.left;
	cpar[CP_PD_OT] = (uint32_t)w->outer.top;
	cpar[CP_PD_W] = (uint32_t)( w->outer.right - w->outer.left );
	cpar[CP_PD_H] = (uint32_t)( w->outer.bottom - w->outer.top );
	memset(&win, 0, sizeof(win));
	win.wsrc = buf[CP_B_SRC];
	win.wpitch = w->pitch;
	win.wl = w->outer.left;
	win.wt = w->outer.top;
	for ( i = 0; i < npc; i++ ) {
		unsigned	tstr = pc[i].frame ? w->tint_str : 0;
		int		plain = ( pc[i].a >= 255 && pc[i].wstr == 0 && tstr == 0 );

		memset(par, 0, sizeof(par));
		par[CP_PP_PITCH] = w->pitch;
		par[CP_PP_OL] = (uint32_t)w->outer.left;
		par[CP_PP_OT] = (uint32_t)w->outer.top;
		par[CP_PP_OR] = (uint32_t)w->outer.right;
		par[CP_PP_OB] = (uint32_t)w->outer.bottom;
		par[CP_PP_A] = pc[i].a;
		par[CP_PP_RAD] = (uint32_t)rad;
		par[CP_PP_TINT] = w->tint;
		par[CP_PP_TSTR] = tstr;
		par[CP_PP_WASH] = pc[i].wcol;
		par[CP_PP_WSTR] = pc[i].wstr;
		par[CP_PP_UND] = ( spread && pc[i].a < 255 ) ? 1 : 0;
		par[CP_PP_UPITCH] = PITCH;
		par[CP_PP_SW] = PITCH / 4;
		par[CP_PP_SH] = SH;
		sp = sr_of(pc[i].r);
		n = cp_split_piece(&sp, plain, &so, rad, sr, sfs);
		for ( m = 0; m < n; m++ ) {
			hlay_rect(sfs[m], sr[m].l, sr[m].t, sr[m].r, sr[m].b, ( sfs[m] == CP_FS_COPY ) ? cpar : par, buf,
				  ( sfs[m] == CP_FS_COPY ) ? &win : NULL);
		}
	}
}

/* A job's frame settled, its draws cut to what shows and its supertiles classed */
static void hjob_plan( HJOB *j )
{
	int	i, s, nv = 0;
	uint32_t vkey[64][4];

	if ( j->fit ) {
		CP_SR	u = hd[j->d0].r;

		for ( i = 1; i < j->nd; i++ ) {
			CP_SR	*r = &hd[j->d0 + i].r;

			if ( r->l < u.l ) u.l = r->l;
			if ( r->t < u.t ) u.t = r->t;
			if ( r->r > u.r ) u.r = r->r;
			if ( r->b > u.b ) u.b = r->b;
		}
		j->fx = u.l & ~( ALIGN_PX - 1 );
		j->fy = u.t;
		j->fw = u.r - j->fx;
		j->fh = u.b - u.t;
		j->load = j->store = A_BACK + (uint32_t)j->fy * PITCH + (uint32_t)j->fx * 4;
		keep_add(j->fx, j->fy, j->fx + j->fw, j->fy + j->fh);
	}
	for ( i = 0; i < j->nd; i++ ) {
		HDRAW	*d = &hd[j->d0 + i];
		CP_DR	*r = &hdr[j->d0 + i];

		d->par[CP_P_ORGX] = (uint32_t)j->fx;
		d->par[CP_P_ORGY] = (uint32_t)j->fy;
		memset(r, 0, sizeof(*r));
		r->r = d->r;
		r->copy = ( d->fs == CP_FS_COPY );
		r->wok = d->wok;
		r->w = ( d->fs == CP_FS_COPY ) ? CP_CW_COPY : CP_CW_SHADE;
	}
	cp_grid(&j->g, j->fx, j->fy, j->fw, j->fh, ht_tile, ht_maxst);
	if ( j->g.nx * j->g.ny > HST_MAX ) die("supertiles");
	npool = cp_cull(&hdr[j->d0], j->nd, pool, npool, HPOOL, f_cull);
	if ( npool < 0 ) die("the pool");
	cp_classify(&j->g, &hdr[j->d0], j->nd, pool, f_tiles && f_wload, j->cls, j->base);

	/* the windows the tiles load from: so many to a job, the rest loaded as they would be */
	for ( s = 0; s < j->g.nx * j->g.ny; s++ ) {
		HDRAW	*d;
		int	v;

		if ( j->cls[s] != CP_ST_WIN ) continue;
		d = &hd[j->d0 + j->base[s]];
		for ( v = 0; v < nv; v++ ) {
			if ( vkey[v][0] == d->wsrc && vkey[v][1] == d->wpitch && vkey[v][2] == (uint32_t)d->wl
			  && vkey[v][3] == (uint32_t)d->wt ) break;
		}
		if ( v == nv && nv < nvar_max ) {
			vkey[nv][0] = d->wsrc;
			vkey[nv][1] = d->wpitch;
			vkey[nv][2] = (uint32_t)d->wl;
			vkey[nv][3] = (uint32_t)d->wt;
			nv++;
		}
		if ( v >= nv ) {
			j->cls[s] = CP_ST_LOAD;
			j->base[s] = -1;
		}
	}
	npool = cp_cut_bases(&j->g, &hdr[j->d0], j->nd, pool, npool, HPOOL, j->cls, j->base);
}

/* A job as the GPU runs it */
static void hjob_run( HJOB *j )
{
	int	s, i, k, x, y, pw = j->g.tile * j->g.sw, ph = j->g.tile * j->g.sh;
	CP_SR	st;

	for ( s = 0; s < j->g.nx * j->g.ny; s++ ) {
		if ( j->cls[s] != CP_ST_LOAD && j->cls[s] != CP_ST_WIN ) continue;
		cp_grid_at(&j->g, s, &st);
		st_rendered++;
		if ( j->cls[s] == CP_ST_WIN ) st_win++;
		for ( y = st.t; y < st.b; y++ ) {
			for ( x = st.l; x < st.r; x++ ) {
				uint32_t	*f = &frame[( y - j->fy ) * SW + ( x - j->fx )];

				if ( j->cls[s] == CP_ST_WIN ) {
					HDRAW	*d = &hd[j->d0 + j->base[s]];

					*f = mem_read(d->wsrc + (uint32_t)( y - d->wt ) * d->wpitch + (uint32_t)( x - d->wl ) * 4);
				} else if ( j->load != 0 ) {
					*f = mem_read(j->load + (uint32_t)( y - j->fy ) * j->lpitch + (uint32_t)( x - j->fx ) * 4);
				} else {
					*f = 0xCDCDCDCDU;
				}
			}
		}
	}
	for ( i = 0; i < j->nd; i++ ) {
		HDRAW	*d = &hd[j->d0 + i];
		CP_DR	*r = &hdr[j->d0 + i];
		long	area = (long)( d->r.r - d->r.l ) * ( d->r.b - d->r.t ), shown = 0;

		for ( k = 0; k < r->ns; k++ ) {
			CP_SR	*q = &pool[r->s0 + k];

			shown += (long)( q->r - q->l ) * ( q->b - q->t );
			for ( y = q->t; y < q->b; y++ ) {
				for ( x = q->l; x < q->r; x++ ) {
					uint32_t	*f = &frame[( y - j->fy ) * SW + ( x - j->fx )], o;
					int		c = j->cls[( ( y - j->fy ) / ph ) * j->g.nx + ( x - j->fx ) / pw];

					if ( c != CP_ST_LOAD && c != CP_ST_WIN ) {
						fault("a draw in a supertile not rendered", x, y);
						continue;
					}
					if ( run_pixel(d->fs, x, y, j->fx, j->fy, d->par, d->buf, *f, &o) ) *f = o;
				}
			}
		}
		px_drawn += shown;
		px_culled += area - shown;
		if ( d->fs == CP_FS_COPY ) px_copied += shown;
	}
	if ( j->store == 0 ) return;
	for ( s = 0; s < j->g.nx * j->g.ny; s++ ) {
		if ( j->cls[s] != CP_ST_LOAD && j->cls[s] != CP_ST_WIN ) continue;
		cp_grid_at(&j->g, s, &st);
		for ( y = st.t; y < st.b; y++ ) {
			for ( x = st.l; x < st.r; x++ ) {
				uint32_t	a = j->store + (uint32_t)( y - j->fy ) * j->spitch + (uint32_t)( x - j->fx ) * 4;

				if ( a >= A_BACK && a < A_BACK + sizeof(back_g) && cpu_mask[( a - A_BACK ) / 4] ) {
					fault("a store where the processor copies the ground", x, y);
				}
				mem_write(a, frame[( y - j->fy ) * SW + ( x - j->fx )], 0);
			}
		}
	}
}

/* A part of the screen the shaders' way: the ground and windows w[0..nw-1] */
static void gpu_part( HC_RECT box, const HC_WIN *w, int nw )
{
	int	i, s, x, y;
	UD	gpu = 0;
	CP_SR	st;

	nhd = nhj = npool = nkeep = keep_over = 0;
	hbox = box;
	memset(cpu_mask, 0, sizeof(cpu_mask));
	hcur = hjob_open(box.left, box.top, box.right - box.left, box.bottom - box.top,
			 A_GROUND + (uint32_t)box.top * PITCH + (uint32_t)box.left * 4, PITCH,
			 A_BACK + (uint32_t)box.top * PITCH + (uint32_t)box.left * 4, PITCH);
	for ( i = 0; i < nw; i++ ) hwindow(&w[i], i);

	/* every job planned, then the first job's free supertiles shared with the processor */
	for ( i = 0; i < nhj; i++ ) {
		if ( hj[i].fit && hj[i].nd == 0 ) continue;
		hjob_plan(&hj[i]);
	}
	for ( i = 1; i < nhj; i++ ) {
		HJOB	*j = &hj[i];

		if ( j->fit && j->nd == 0 ) continue;
		for ( s = 0; s < j->g.nx * j->g.ny; s++ ) {
			if ( j->cls[s] == CP_ST_FREE ) j->cls[s] = f_tiles ? CP_ST_SKIP : CP_ST_LOAD;
		}
		gpu += cp_cost(&j->g, j->cls, &hdr[j->d0], j->nd, pool, CP_CW_TILE);
	}
	if ( f_tiles ) {
		gpu += cp_cost(&hj[0].g, hj[0].cls, &hdr[hj[0].d0], hj[0].nd, pool, CP_CW_TILE);
		cp_share(&hj[0].g, hj[0].cls, keep, nkeep, f_cpu && !keep_over, gpu);
	} else {
		for ( s = 0; s < hj[0].g.nx * hj[0].g.ny; s++ ) {
			if ( hj[0].cls[s] == CP_ST_FREE ) hj[0].cls[s] = CP_ST_LOAD;
		}
	}
	for ( s = 0; s < hj[0].g.nx * hj[0].g.ny; s++ ) {
		if ( hj[0].cls[s] != CP_ST_CPU ) continue;
		cp_grid_at(&hj[0].g, s, &st);
		st_cpu++;
		for ( y = st.t; y < st.b; y++ ) {
			for ( x = st.l; x < st.r; x++ ) cpu_mask[y * SW + x] = 1;
		}
	}

	/* the jobs in turn, then the processor's share */
	for ( i = 0; i < nhj; i++ ) {
		if ( hj[i].fit && hj[i].nd == 0 ) continue;
		for ( s = 0; s < hj[i].g.nx * hj[i].g.ny; s++ ) {
			if ( hj[i].cls[s] == CP_ST_SKIP ) st_skip++;
		}
		hjob_run(&hj[i]);
	}
	for ( i = 0; i < SW * SH; i++ ) {
		if ( cpu_mask[i] ) back_g[i] = ground[i];
	}
}

/* A random window over pixels k, with a random look */
static void random_win( HC_WIN *w, int k )
{
	int	bar = rr(0, 1) ? rr(4, 14) : 0;
	int	ww = rr(12, SW - 8), hh = rr(bar + 8, SH - 8), l = rr(-8, SW - ww + 4), t = rr(-6, SH - hh + 4);

	/* the work area within the frame, as the window manager makes it (wm.c work_of) */

	memset(w, 0, sizeof(*w));
	if ( l < 0 ) l = 0;			/* a row of pixels is as long as the screen's */
	if ( l + ww > SW ) ww = SW - l;
	w->outer = rect(l, t, l + ww, t + hh);
	w->work = rect(l + 2, t + 2 + bar, l + ww - 3, t + hh - 3);
	w->band = w->outer;
	w->band.bottom = bar ? w->work.top : w->outer.top;
	w->pixels = (const uint8_t *)pix[k];
	w->pitch = PITCH;
	w->a_frame = rr(0, 2) ? 255 : (unsigned)rr(0, 255);
	w->a_work = rr(0, 3) ? 255 : (unsigned)rr(0, 255);
	w->round = rr(0, 2) ? 0 : rr(1, 14);
	w->blur = rr(0, 1) ? 0 : rr(1, HC_BLUR_MAX);
	w->wash = rnd() & 0xFFFFFF;
	w->wash_str = rr(0, 1) ? 0 : (unsigned)rr(0, 255);
	w->bar_wash = rnd() & 0xFFFFFF;
	w->bar_str = rr(0, 1) ? 0 : (unsigned)rr(0, 255);
	w->tint = rnd() & 0xFFFFFF;
	w->tint_str = rr(0, 2) ? 0 : (unsigned)rr(0, 255);
	if ( rr(0, 1) ) {
		w->shd_dx = rr(-8, 8);
		w->shd_dy = rr(-8, 8);
		w->shd_size = rr(0, 10);
		w->shd_dark = (unsigned)rr(1, 255);
	}
	w->shown = w->outer;
	w->fade = 255;
	if ( rr(0, 5) == 0 ) {
		int	sw = ww * rr(180, 255) / 256, sh = hh * rr(180, 255) / 256;

		w->shown.left = l + ( ww - sw ) / 2;
		w->shown.top = t + ( hh - sh ) / 2;
		w->shown.right = w->shown.left + sw;
		w->shown.bottom = w->shown.top + sh;
		w->fade = (unsigned)rr(20, 255);
	}
}

static void fill( uint32_t *p, int n )
{
	int	i;

	for ( i = 0; i < n; i++ ) p[i] = rnd();
}

int main( int argc, char **argv )
{
	static const int	tiles[3] = { 8, 16, 32 }, maxst[4] = { 256, 30, 12, 5 };
	int		nscene = ( argc > 1 ) ? atoi(argv[1]) : 200;
	int		s, i, k, nw, bad_scenes = 0;
	HC_WIN		w[NW];
	long		total_bad = 0, top_diff = 0;

	div255_check();
	glsl_type_singleton_init_or_ref();
	device_init();
	runs_make();

	region_set(A_BACK, back_g, sizeof(back_g), 0);
	region_set(A_GROUND, ground, sizeof(ground), 0);
	region_set(A_T1, t1, sizeof(t1), 1);
	region_set(A_T2, t2, sizeof(t2), 1);
	for ( k = 0; k <= HC_ROUND_MAX; k++ ) {
		const uint8_t	*c = ( k > 0 ) ? hc_corner_cov(k) : NULL;

		memset(cov[k], 0, sizeof(cov[k]));
		if ( c != NULL ) memcpy(cov[k], c, (size_t)( k * k ));
		region_set(cov_at(k), cov[k], sizeof(cov[k]), 0);
	}
	for ( k = 0; k < NW; k++ ) {
		region_set(A_WIN + (uint32_t)k * 0x1000000, pix[k], sizeof(pix[k]), 0);
	}

	/* the three that do one thing each */
	{
		uint32_t	par[CP_NPARAM], buf[CP_NBUF] = { A_WIN, A_BACK, A_COV }, o, want;
		int		x, y, nd = 0;

		fill(pix[0], SW * SH);
		memset(par, 0, sizeof(par));
		par[CP_PD_COLOUR] = 0x8040C020U;
		par[CP_PD_PITCH] = PITCH;
		par[CP_PD_OL] = 10;
		par[CP_PD_OT] = 7;
		par[CP_PD_W] = 50;
		par[CP_PD_H] = 40;
		for ( y = 7; y < 47; y++ ) {
			for ( x = 10; x < 60; x++ ) {
				uint32_t	dst = rnd();

				if ( !run_pixel(CP_FS_FILL, x, y, 0, 0, par, buf, dst, &o) || o != par[CP_PD_COLOUR] ) nd++;
				want = pix[0][( y - 7 ) * SW + ( x - 10 )];
				if ( !run_pixel(CP_FS_COPY, x, y, 0, 0, par, buf, dst, &o) || o != want ) nd++;
				if ( !run_pixel(CP_FS_INVERT, x, y, 0, 0, par, buf, dst, &o) || o != ( dst ^ 0x00FFFFFFU ) ) nd++;
			}
		}
		printf("test_cp_shaders: fill, copy and invert: %d pixels wrong\n", nd);
		if ( nd != 0 ) bad_scenes++;
	}

	for ( s = 0; s < nscene; s++ ) {
		HC_RECT	box = rect(0, 0, SW, SH);
		long	bad = 0, faults0 = faults, oob0 = oob, bw0 = bad_writes;

		seed = 0x9E3779B9U ^ (uint32_t)( s * 7919 + 1 );
		fill(ground, SW * SH);
		for ( k = 0; k < NW; k++ ) fill(pix[k], SW * SH);
		nw = rr(1, NW);
		for ( k = 0; k < nw; k++ ) random_win(&w[k], k);
		if ( rr(0, 2) == 0 ) {
			box.left = rr(0, 4) * 16;
			box.top = rr(0, SH / 2);
			box.right = rr(box.left + 1, SW);
			box.bottom = rr(box.top + 1, SH);
		}
		/* the ways the kernel may take, most scenes with all of them */
		k = rr(0, 3);
		f_cull = ( k != 1 ) || rr(0, 1);
		f_tiles = ( k != 2 ) || rr(0, 1);
		f_wload = f_tiles && ( ( k != 3 ) || rr(0, 1) );
		f_cpu = rr(0, 3) != 0;
		ht_tile = tiles[rr(0, 2)];
		ht_maxst = maxst[rr(0, 3)];
		jdraws_max = rr(0, 2) ? 128 : rr(1, 6);
		nvar_max = rr(0, 2) ? 16 : rr(0, 2);

		for ( i = 0; i < SW * SH; i++ ) back_c[i] = back_g[i] = 0x5A000000U ^ ( (uint32_t)i * 2654435761U );
		memset(t1, 0x77, sizeof(t1));
		memset(t2, 0x66, sizeof(t2));

		/* the processor's way */
		for ( i = box.top; i < box.bottom; i++ ) {
			memcpy(&back_c[i * SW + box.left], &ground[i * SW + box.left], (size_t)( box.right - box.left ) * 4);
		}
		for ( k = 0; k < nw; k++ ) hc_window((uint8_t *)back_c, PITCH, box, SH, &w[k]);

		/* the shaders' */
		gpu_part(box, w, nw);

		for ( i = 0; i < SW * SH; i++ ) {
			if ( ( ( back_c[i] ^ back_g[i] ) & 0xFF000000U ) != 0 ) top_diff++;
			if ( ( back_c[i] ^ back_g[i] ) & 0x00FFFFFFU ) {
				if ( bad < 4 ) {
					fprintf(stderr, "scene %d: at (%d,%d) shaders %08x, compose.c %08x\n",
						s, i % SW, i / SW, back_g[i], back_c[i]);
				}
				bad++;
			}
		}
		if ( bad != 0 || faults != faults0 || oob != oob0 || bad_writes != bw0 ) {
			bad_scenes++;
			total_bad += bad;
			fprintf(stderr, "scene %d: %ld pixels differ, %ld faults, %ld reads outside, %ld writes astray "
				"(%d windows, box %d,%d-%d,%d; cull %d tiles %d wload %d cpu %d, tile %d, "
				"supertiles %d, draws a job %d, windows a job %d)\n",
				s, bad, faults - faults0, oob - oob0, bad_writes - bw0, nw, box.left, box.top,
				box.right, box.bottom, f_cull, f_tiles, f_wload, f_cpu, ht_tile, ht_maxst,
				jdraws_max, nvar_max);
			for ( k = 0; k < nw; k++ ) {
				fprintf(stderr, "  window %d: outer %d,%d-%d,%d work %d,%d-%d,%d a %u/%u round %d blur %d "
					"wash %u bar %u tint %u shadow %d,%d size %d dark %u shown %d,%d-%d,%d fade %u\n",
					k, w[k].outer.left, w[k].outer.top, w[k].outer.right, w[k].outer.bottom,
					w[k].work.left, w[k].work.top, w[k].work.right, w[k].work.bottom,
					w[k].a_frame, w[k].a_work, w[k].round, w[k].blur, w[k].wash_str, w[k].bar_str,
					w[k].tint_str, w[k].shd_dx, w[k].shd_dy, w[k].shd_size, w[k].shd_dark,
					w[k].shown.left, w[k].shown.top, w[k].shown.right, w[k].shown.bottom, w[k].fade);
			}
		}
	}
	printf("test_cp_shaders: %d scenes, %d differ (%ld pixels), %ld draws (%ld pixels drawn, %ld of them copied, "
	       "%ld left out under copies), supertiles: %ld rendered (%ld from windows), %ld left out, "
	       "%ld the processor's; %ld faults, %ld reads outside their buffer, %ld writes astray, "
	       "%ld pixels whose top byte differs\n", nscene, bad_scenes, total_bad, draws, px_drawn, px_copied,
	       px_culled, st_rendered, st_win, st_skip, st_cpu, faults, oob, bad_writes, top_diff);
	return ( bad_scenes == 0 && oob == 0 && bad_writes == 0 && faults == 0 ) ? 0 : 1;
}
