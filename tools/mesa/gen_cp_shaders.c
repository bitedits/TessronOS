/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	gen_cp_shaders.c
 *	The shaders that composite windows on the V3D 7.1, translated on the
 *	host into QPU code (docs/tessronos/13-gpu.md 13.10).
 *
 *	Built by tools/mesa/shaders.sh against Mesa's NIR and V3D shader
 *	compiler, which never go into the kernel. Each shader is written
 *	here with the NIR builder, compiled for the Raspberry Pi 5's V3D
 *	(7.1, revision 7 or later), and written out as C arrays: the QPU
 *	code, what each word of its uniform stream holds, the shader state
 *	record with its addresses left 0, and the few control list packets
 *	that carry floating point numbers, which the kernel cannot work out
 *	because it is built without the floating point registers.
 *
 *	One vertex shader serves every draw: it takes the corners of a
 *	rectangle already in clip space and passes them on. The fragment
 *	shaders do the work of compose.c's stages, pixel by pixel and with
 *	the same integer arithmetic, so that the screen they build is the
 *	one the processor builds:
 *
 *	  piece		a part of a window mixed over what is behind it
 *	  shadow	what is behind darkened round a window
 *	  halve		what shows through, a level of its spread from the
 *			level below at twice the size
 *	  smooth3, smooth5
 *			the deepest level smoothed, three or five pixels
 *			across
 *	  double	a level from the level above at half the size
 *	  moving	a window opening or closing, scaled and faded
 *
 *	What is behind a pixel is read from the tile buffer (the render
 *	target is 8 bit RGBA, which holds the back buffer's bytes in the
 *	order they are in memory); everything else is read from memory by
 *	address through the TMU, with the address held inside the buffer
 *	it belongs to, so that the 2 x 2 quads' helper pixels never read
 *	outside it.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/glsl_types.h"
#include "broadcom/common/v3d_device_info.h"
#include "broadcom/common/v3d_limits.h"
#include "broadcom/common/v3d_util.h"
#include "broadcom/compiler/v3d_compiler.h"
#include "broadcom/qpu/qpu_disasm.h"

#define __gen_user_data		void
#define __gen_address_type	uint32_t
#define __gen_address_offset(a)	(*(a))
#define __gen_emit_reloc(d, a)	((void)0)
#include "broadcom/cle/v3d_packet_v71_pack.h"

/* The clip space maps [0, 2 * VP_HALF) pixels each way, whatever the frame */
#define VP_HALF		4096

/* What a word of a uniform stream holds, as the kernel fills it */
#define U_CONST		0	/* the value as it stands */
#define U_PARAM		1	/* the draw's parameter of this number */
#define U_SSBO		2	/* the GPU address of the draw's buffer of this number */

/* The draw's buffers */
#define B_SRC		0	/* a window's pixels, or the level a step of the spread reads */
#define B_AUX		1	/* what shows through, spread (T2) */
#define B_COV		2	/* the cover of a round corner, one byte a pixel */

/* ---------------------------------------------------------------- parameters */

/*
 * Every fragment shader takes the frame's place on the screen first; the
 * rest are its own. The numbers are written out for the kernel.
 */
enum {
	P_ORGX, P_ORGY,

	/* piece */
	PP_PITCH = 2, PP_OL, PP_OT, PP_OR, PP_OB, PP_A, PP_RAD, PP_TINT, PP_TSTR,
	PP_WASH, PP_WSTR, PP_UND, PP_UPITCH, PP_SW, PP_SH, PP_NPARAM,

	/* shadow */
	PS_OL = 2, PS_OT, PS_OR, PS_OB, PS_DX, PS_DY, PS_SIZE, PS_DARK, PS_RAD, PS_NPARAM,

	/*
	 * halve, smooth3, smooth5, double: a level of the spread from
	 * another, whose pixels that touch the box are VL..VR - 1 and
	 * VT..VB - 1; smoothed with the weights A, B, M
	 */
	PL_PITCH = 2, PL_VL, PL_VT, PL_VR, PL_VB, PL_A, PL_B, PL_M, PL_NPARAM,

	/* fill, copy, invert: the checks of one thing each */
	PD_COLOUR = 2, PD_PITCH, PD_OL, PD_OT, PD_W, PD_H, PD_NPARAM,

	/* moving */
	PM_PITCH = 2, PM_SL, PM_ST, PM_SR, PM_SB, PM_STEPX, PM_STEPY, PM_OL, PM_OT,
	PM_OR, PM_OB, PM_WL, PM_WT, PM_WR, PM_WB, PM_AW, PM_AF, PM_RAD, PM_TINT,
	PM_TSTR, PM_WASH, PM_WSTR, PM_BWASH, PM_BSTR, PM_BAND, PM_NPARAM
};

static const struct {
	const char	*name;
	int		value;
} param_names[] = {
	{ "CP_P_ORGX", P_ORGX }, { "CP_P_ORGY", P_ORGY },
	{ "CP_PP_PITCH", PP_PITCH }, { "CP_PP_OL", PP_OL }, { "CP_PP_OT", PP_OT },
	{ "CP_PP_OR", PP_OR }, { "CP_PP_OB", PP_OB }, { "CP_PP_A", PP_A },
	{ "CP_PP_RAD", PP_RAD }, { "CP_PP_TINT", PP_TINT }, { "CP_PP_TSTR", PP_TSTR },
	{ "CP_PP_WASH", PP_WASH }, { "CP_PP_WSTR", PP_WSTR }, { "CP_PP_UND", PP_UND },
	{ "CP_PP_UPITCH", PP_UPITCH }, { "CP_PP_SW", PP_SW }, { "CP_PP_SH", PP_SH },
	{ "CP_PS_OL", PS_OL }, { "CP_PS_OT", PS_OT }, { "CP_PS_OR", PS_OR },
	{ "CP_PS_OB", PS_OB }, { "CP_PS_DX", PS_DX }, { "CP_PS_DY", PS_DY },
	{ "CP_PS_SIZE", PS_SIZE }, { "CP_PS_DARK", PS_DARK }, { "CP_PS_RAD", PS_RAD },
	{ "CP_PL_PITCH", PL_PITCH }, { "CP_PL_VL", PL_VL }, { "CP_PL_VT", PL_VT },
	{ "CP_PL_VR", PL_VR }, { "CP_PL_VB", PL_VB }, { "CP_PL_A", PL_A },
	{ "CP_PL_B", PL_B }, { "CP_PL_M", PL_M },
	{ "CP_PM_PITCH", PM_PITCH }, { "CP_PM_SL", PM_SL }, { "CP_PM_ST", PM_ST },
	{ "CP_PM_SR", PM_SR }, { "CP_PM_SB", PM_SB }, { "CP_PM_STEPX", PM_STEPX },
	{ "CP_PM_STEPY", PM_STEPY }, { "CP_PM_OL", PM_OL }, { "CP_PM_OT", PM_OT },
	{ "CP_PM_OR", PM_OR }, { "CP_PM_OB", PM_OB }, { "CP_PM_WL", PM_WL },
	{ "CP_PM_WT", PM_WT }, { "CP_PM_WR", PM_WR }, { "CP_PM_WB", PM_WB },
	{ "CP_PM_AW", PM_AW }, { "CP_PM_AF", PM_AF }, { "CP_PM_RAD", PM_RAD },
	{ "CP_PM_TINT", PM_TINT }, { "CP_PM_TSTR", PM_TSTR }, { "CP_PM_WASH", PM_WASH },
	{ "CP_PM_WSTR", PM_WSTR }, { "CP_PM_BWASH", PM_BWASH }, { "CP_PM_BSTR", PM_BSTR },
	{ "CP_PM_BAND", PM_BAND },
	{ "CP_PD_COLOUR", PD_COLOUR }, { "CP_PD_PITCH", PD_PITCH }, { "CP_PD_OL", PD_OL },
	{ "CP_PD_OT", PD_OT }, { "CP_PD_W", PD_W }, { "CP_PD_H", PD_H },
};

/* ---------------------------------------------------------------- the device */

static struct v3d_device_info devinfo;
static const struct v3d_compiler *compiler;
static nir_shader_compiler_options nir_options;

static void die( const char *fmt, ... )
{
	va_list	ap;

	va_start(ap, fmt);
	fprintf(stderr, "gen_cp_shaders: ");
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
	exit(1);
}

/*
 * The Raspberry Pi 5's V3D as the board reports it (13-gpu.md 13.8.1):
 * 7.1, hub revision 7, 4 slices of 4 QPUs, 64KB of VPM.
 */
static void device_init( void )
{
	memset(&devinfo, 0, sizeof(devinfo));
	devinfo.ver = 71;
	devinfo.rev = 7;
	devinfo.compat_rev = 0;
	devinfo.vpm_size = 8 * 8192;
	devinfo.qpu_count = 16;
	devinfo.has_accumulators = false;
	devinfo.clipper_xy_granularity = 64.0f;
	devinfo.cle_readahead = 1024;
	devinfo.page_size = 4096;
	devinfo.max_framebuffer_size = V3D_MAX_FRAMEBUFFER_SIZE(71);
	devinfo.max_render_targets = V3D_MAX_RENDER_TARGETS(71);

	compiler = v3d_compiler_init(&devinfo, 0);
	if ( compiler == NULL ) die("no compiler");

	/* what the V3D compiler needs of the NIR it is given: the operations it has, the ones lowered */
	memset(&nir_options, 0, sizeof(nir_options));
	nir_options.compact_arrays = true;
	nir_options.lower_uadd_sat = true;
	nir_options.lower_usub_sat = true;
	nir_options.lower_iadd_sat = true;
	nir_options.lower_extract_byte = true;
	nir_options.lower_extract_word = true;
	nir_options.lower_insert_byte = true;
	nir_options.lower_insert_word = true;
	nir_options.lower_bitfield_insert = true;
	nir_options.lower_bitfield_extract = true;
	nir_options.lower_bitfield_extract16 = true;
	nir_options.lower_bitfield_extract8 = true;
	nir_options.lower_bitfield_reverse = true;
	nir_options.lower_bit_count = true;
	nir_options.lower_cs_local_id_to_index = true;
	nir_options.lower_ffract = true;
	nir_options.lower_fmod = true;
	nir_options.lower_pack_unorm_2x16 = true;
	nir_options.lower_pack_snorm_2x16 = true;
	nir_options.lower_pack_unorm_4x8 = true;
	nir_options.lower_pack_snorm_4x8 = true;
	nir_options.lower_unpack_unorm_4x8 = true;
	nir_options.lower_unpack_snorm_4x8 = true;
	nir_options.lower_pack_half_2x16 = true;
	nir_options.lower_unpack_half_2x16 = true;
	nir_options.lower_pack_32_2x16 = true;
	nir_options.lower_pack_32_2x16_split = true;
	nir_options.lower_unpack_32_2x16_split = true;
	nir_options.lower_fdiv = true;
	nir_options.lower_find_lsb = true;
	nir_options.lower_flrp32 = true;
	nir_options.lower_fpow = true;
	nir_options.lower_fsqrt = true;
	nir_options.lower_ifind_msb = true;
	nir_options.lower_isign = true;
	nir_options.lower_hadd = true;
	nir_options.lower_fisnormal = true;
	nir_options.lower_mul_high = true;
	nir_options.lower_wpos_pntc = true;
	nir_options.lower_to_scalar = true;
	nir_options.lower_interpolate_at = true;
	nir_options.lower_int64_options = nir_lower_bcsel64 | nir_lower_bit_count64 |
		nir_lower_conv64 | nir_lower_divmod64 | nir_lower_iabs64 | nir_lower_iadd64 |
		nir_lower_icmp64 | nir_lower_imul_2x32_64 | nir_lower_imul64 | nir_lower_ineg64 |
		nir_lower_logic64 | nir_lower_minmax64 | nir_lower_shift64 | nir_lower_ufind_msb64;
	nir_options.lower_fquantize2f16 = true;
	nir_options.lower_ufind_msb = true;
	nir_options.has_fsub = true;
	nir_options.has_isub = true;
	nir_options.has_imul24 = true;
	nir_options.has_umul24 = true;
	nir_options.has_uclz = true;
	nir_options.divergence_analysis_options = nir_divergence_multiple_workgroup_per_compute_subgroup;
	nir_options.max_unroll_iterations = 16;
	nir_options.max_samples = 4;
	nir_options.force_indirect_unrolling_sampler = true;
	nir_options.scalarize_ddx = true;
	nir_options.max_varying_expression_cost = 4;
	nir_options.lower_fsat = false;
}

/* ---------------------------------------------------------------- building blocks */

static nir_def *P( nir_builder *b, int i )
{
	return nir_load_uniform(b, 1, 32, nir_imm_int(b, 0), .base = i * 4, .range = 4,
				.dest_type = nir_type_uint32);
}

/* A word of a buffer, by its byte offset */
static nir_def *ld( nir_builder *b, int buf, nir_def *off )
{
	return nir_load_ssbo(b, 1, 32, nir_imm_int(b, buf), off,
			     .access = ACCESS_NON_WRITEABLE | ACCESS_CAN_REORDER,
			     .align_mul = 4, .align_offset = 0);
}

static nir_def *clampi( nir_builder *b, nir_def *v, nir_def *lo, nir_def *hi_incl )
{
	return nir_imin(b, nir_imax(b, v, lo), hi_incl);
}

static nir_def *ch( nir_builder *b, nir_def *w, int i )
{
	return nir_iand_imm(b, nir_ushr_imm(b, w, 8 * i), 0xFF);
}

static nir_def *pack4( nir_builder *b, nir_def *c0, nir_def *c1, nir_def *c2, nir_def *c3 )
{
	return nir_ior(b, nir_ior(b, c0, nir_ishl_imm(b, c1, 8)),
		       nir_ior(b, nir_ishl_imm(b, c2, 16), nir_ishl_imm(b, c3, 24)));
}

/* t / 255 rounded down, for t up to 65280 (checked in div255_check); the most it is used for is 255 * 255 + 127 */
static unsigned div255_c( unsigned t )
{
	return ( t + 1 + ( t >> 8 ) ) >> 8;
}

static nir_def *div255( nir_builder *b, nir_def *t )
{
	return nir_ushr_imm(b, nir_iadd(b, nir_iadd_imm(b, t, 1), nir_ushr_imm(b, t, 8)), 8);
}

static void div255_check( void )
{
	unsigned t;

	for ( t = 0; t <= 65280; t++ ) {
		if ( div255_c(t) != t / 255 ) die("t / 255 is wrong at %u", t);
	}
}

static nir_def *mul( nir_builder *b, nir_def *x, nir_def *y )
{
	return nir_umul24(b, x, y);	/* both below 2^24 wherever it is used */
}

/* over laid at a (0..255) over under, channel by channel and rounded; the top byte 0 */
static nir_def *over_of( nir_builder *b, nir_def *under, nir_def *over, nir_def *a )
{
	nir_def	*na = nir_isub(b, nir_imm_int(b, 255), a);
	nir_def	*c[3];
	int	i;

	for ( i = 0; i < 3; i++ ) {
		nir_def	*t = nir_iadd(b, mul(b, ch(b, over, i), a), mul(b, ch(b, under, i), na));

		c[i] = div255(b, nir_iadd_imm(b, t, 127));
	}
	return pack4(b, c[0], c[1], c[2], nir_imm_int(b, 0));
}

/* A colour taken toward black by a (0..255), rounded; the top byte 0 */
static nir_def *darker( nir_builder *b, nir_def *col, nir_def *a )
{
	nir_def	*k = nir_isub(b, nir_imm_int(b, 255), a);
	nir_def	*c[3];
	int	i;

	for ( i = 0; i < 3; i++ ) {
		c[i] = div255(b, nir_iadd_imm(b, mul(b, ch(b, col, i), k), 127));
	}
	return pack4(b, c[0], c[1], c[2], nir_imm_int(b, 0));
}

/* ---------------------------------------------------------------- fragment shader frame */

typedef struct {
	nir_builder	b;
	nir_variable	*color;		/* the render target, read for what is behind */
	nir_def		*x, *y;		/* the pixel on the screen */
} FS;

static void fs_begin( FS *f, const char *name, int nssbo )
{
	nir_variable	*pos;
	nir_def		*p;

	f->b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, &nir_options, "%s", name);
	f->b.shader->info.num_ssbos = nssbo;

	f->color = nir_variable_create(f->b.shader, nir_var_shader_out, glsl_vec4_type(), "color");
	f->color->data.location = FRAG_RESULT_DATA0;
	f->color->data.driver_location = 0;
	f->color->data.fb_fetch_output = true;

	pos = nir_variable_create(f->b.shader, nir_var_shader_in, glsl_vec4_type(), "pos");
	pos->data.location = VARYING_SLOT_POS;
	pos->data.driver_location = 0;

	p = nir_load_var(&f->b, pos);
	f->x = nir_iadd(&f->b, nir_f2i32(&f->b, nir_channel(&f->b, p, 0)), P(&f->b, P_ORGX));
	f->y = nir_iadd(&f->b, nir_f2i32(&f->b, nir_channel(&f->b, p, 1)), P(&f->b, P_ORGY));
}

/* What is behind the pixel, as a word of four bytes */
static nir_def *fs_behind( FS *f )
{
	nir_builder	*b = &f->b;
	nir_def		*v = nir_load_var(b, f->color);
	nir_def		*c[4];
	int		i;

	for ( i = 0; i < 4; i++ ) {
		c[i] = nir_f2u32(b, nir_fadd_imm(b, nir_fmul_imm(b, nir_channel(b, v, i), 255.0), 0.5));
		c[i] = nir_umin(b, c[i], nir_imm_int(b, 255));
	}
	return pack4(b, c[0], c[1], c[2], c[3]);
}

static void fs_out( FS *f, nir_def *w )
{
	nir_builder	*b = &f->b;
	nir_def		*c[4];
	int		i;

	for ( i = 0; i < 4; i++ ) {
		c[i] = nir_fmul_imm(b, nir_u2f32(b, ch(b, w, i)), 1.0 / 255.0);
	}
	nir_store_var(b, f->color, nir_vec(b, c, 4), 0xF);
}

/* ---------------------------------------------------------------- corners */

/*
 * The cover of the pixel (x, y) of a rectangle (l, t, r, b) whose corners
 * are of radius rad: 'inrow' whether the row is one of a corner's, and
 * the cover itself (255 outside the corners' columns). The table holds
 * rad x rad bytes from the corner in.
 */
static void cover_at( nir_builder *b, nir_def *x, nir_def *y, nir_def *l, nir_def *t,
		      nir_def *r, nir_def *bot, nir_def *rad, nir_def **inrow, nir_def **cov )
{
	nir_def	*ey = nir_imin(b, nir_isub(b, y, t), nir_isub(b, nir_iadd_imm(b, bot, -1), y));
	nir_def	*ex = nir_imin(b, nir_isub(b, x, l), nir_isub(b, nir_iadd_imm(b, r, -1), x));
	nir_def	*rm1 = nir_imax(b, nir_iadd_imm(b, rad, -1), nir_imm_int(b, 0));
	nir_def	*idx, *w, *byte;

	*inrow = nir_iand(b, nir_ilt(b, nir_imm_int(b, 0), rad), nir_ilt(b, ey, rad));
	idx = nir_iadd(b, mul(b, clampi(b, ey, nir_imm_int(b, 0), rm1), rad),
		       clampi(b, ex, nir_imm_int(b, 0), rm1));
	w = ld(b, B_COV, nir_iand_imm(b, idx, ~3));
	byte = nir_iand_imm(b, nir_ushr(b, w, nir_ishl_imm(b, nir_iand_imm(b, idx, 3), 3)), 0xFF);
	*cov = nir_bcsel(b, nir_ilt(b, ex, rad), byte, nir_imm_int(b, 255));
}

/* ---------------------------------------------------------------- the shaders */

/*
 * A part of a window mixed over what is behind it (compose.c lay_piece):
 * its pixel, made less solid in a round corner (and not laid where that
 * leaves nothing), tinted and washed, then laid as it is where it is
 * solid and mixed over what shows through -- spread when there is a
 * spread -- where it is not.
 */
static nir_shader *fs_piece( void )
{
	FS		f;
	nir_builder	*b;
	nir_def		*ol, *ot, *orr, *ob, *cx, *cy, *c, *a0, *a, *inrow, *cov, *ac;
	nir_def		*tstr, *wstr, *und, *uw, *base, *mixed;

	fs_begin(&f, "cp_piece", 3);
	b = &f.b;
	ol = P(b, PP_OL);  ot = P(b, PP_OT);  orr = P(b, PP_OR);  ob = P(b, PP_OB);

	cx = clampi(b, f.x, ol, nir_iadd_imm(b, orr, -1));
	cy = clampi(b, f.y, ot, nir_iadd_imm(b, ob, -1));
	c = ld(b, B_SRC, nir_iadd(b, mul(b, nir_isub(b, cy, ot), P(b, PP_PITCH)),
				  nir_ishl_imm(b, nir_isub(b, cx, ol), 2)));

	a0 = P(b, PP_A);
	cover_at(b, f.x, f.y, ol, ot, orr, ob, P(b, PP_RAD), &inrow, &cov);
	ac = div255(b, nir_iadd_imm(b, mul(b, a0, cov), 127));
	a = nir_bcsel(b, inrow, ac, a0);
	nir_terminate_if(b, nir_iand(b, inrow, nir_ieq_imm(b, a, 0)));

	tstr = P(b, PP_TSTR);
	c = nir_bcsel(b, nir_ine_imm(b, tstr, 0), over_of(b, c, P(b, PP_TINT), tstr), c);
	wstr = P(b, PP_WSTR);
	c = nir_bcsel(b, nir_ine_imm(b, wstr, 0), over_of(b, c, P(b, PP_WASH), wstr), c);

	und = nir_ine_imm(b, P(b, PP_UND), 0);
	uw = ld(b, B_AUX, nir_iadd(b, mul(b, clampi(b, f.y, nir_imm_int(b, 0), nir_iadd_imm(b, P(b, PP_SH), -1)),
					  P(b, PP_UPITCH)),
				  nir_ishl_imm(b, clampi(b, f.x, nir_imm_int(b, 0),
							 nir_iadd_imm(b, P(b, PP_SW), -1)), 2)));
	base = nir_bcsel(b, und, uw, fs_behind(&f));
	mixed = over_of(b, base, c, a);
	fs_out(&f, nir_bcsel(b, nir_uge_imm(b, a, 255), c, mixed));
	return b->shader;
}

/* An integer square root, rounded down, of v below 2^24 */
static nir_def *isqrt( nir_builder *b, nir_def *v )
{
	nir_def	*r = nir_f2u32(b, nir_fsqrt(b, nir_u2f32(b, v)));
	int	i;

	for ( i = 0; i < 2; i++ ) {
		nir_def	*r1 = nir_iadd_imm(b, r, 1);

		r = nir_bcsel(b, nir_ult(b, v, nir_imul(b, r, r)), nir_iadd_imm(b, r, -1), r);
		r = nir_bcsel(b, nir_uge(b, v, nir_imul(b, r1, r1)), r1, r);
	}
	return r;
}

/*
 * The shadow (compose.c cp_shadow and shd_at): the window's outline
 * thrown by the offset; all of the darkness inside it, then less and
 * less until none at 'size' away, worked out in sixteenths. Not where
 * the window stands but in the rows of its round corners, where it is
 * covered only in part.
 */
static nir_shader *fs_shadow( void )
{
	FS		f;
	nir_builder	*b;
	nir_def		*x, *y, *ol, *ot, *orr, *ob, *rad, *inrow_w, *inwin, *cr, *cseg;
	nir_def		*sl, *st, *sr, *sb, *l, *r, *t, *bt, *cx, *cy, *dx, *dy, *d16, *size16;
	nir_def		*dark, *a, *covrow, *cov, *ag;

	fs_begin(&f, "cp_shadow", 3);
	b = &f.b;
	x = f.x;  y = f.y;
	ol = P(b, PS_OL);  ot = P(b, PS_OT);  orr = P(b, PS_OR);  ob = P(b, PS_OB);
	rad = P(b, PS_RAD);

	inrow_w = nir_iand(b, nir_ige(b, y, ot), nir_ilt(b, y, ob));
	inwin = nir_iand(b, inrow_w, nir_iand(b, nir_ige(b, x, ol), nir_ilt(b, x, orr)));
	cover_at(b, x, y, ol, ot, orr, ob, rad, &covrow, &cov);
	cr = nir_iand(b, inrow_w, covrow);
	cseg = nir_iand(b, cr, nir_ior(b, nir_ilt(b, x, nir_iadd(b, ol, rad)),
				       nir_ige(b, x, nir_isub(b, orr, rad))));
	nir_terminate_if(b, nir_iand(b, inwin, nir_inot(b, cseg)));

	sl = nir_iadd(b, ol, P(b, PS_DX));  sr = nir_iadd(b, orr, P(b, PS_DX));
	st = nir_iadd(b, ot, P(b, PS_DY));  sb = nir_iadd(b, ob, P(b, PS_DY));
	l = nir_iadd(b, sl, rad);
	r = nir_isub(b, nir_iadd_imm(b, sr, -1), rad);
	t = nir_iadd(b, st, rad);
	bt = nir_isub(b, nir_iadd_imm(b, sb, -1), rad);
	cx = nir_bcsel(b, nir_ilt(b, x, l), l, nir_bcsel(b, nir_ilt(b, r, x), r, x));
	cy = nir_bcsel(b, nir_ilt(b, y, t), t, nir_bcsel(b, nir_ilt(b, bt, y), bt, y));
	dx = nir_iabs(b, nir_isub(b, x, cx));
	dy = nir_iabs(b, nir_isub(b, y, cy));
	d16 = nir_bcsel(b, nir_ieq_imm(b, dx, 0), nir_ishl_imm(b, dy, 4),
			nir_bcsel(b, nir_ieq_imm(b, dy, 0), nir_ishl_imm(b, dx, 4),
				  isqrt(b, nir_ishl_imm(b, nir_iadd(b, mul(b, dx, dx), mul(b, dy, dy)), 8))));
	d16 = nir_isub(b, d16, nir_ishl_imm(b, rad, 4));
	size16 = nir_ishl_imm(b, P(b, PS_SIZE), 4);
	dark = P(b, PS_DARK);
	ag = nir_udiv(b, mul(b, dark, nir_isub(b, size16, d16)), nir_imax(b, size16, nir_imm_int(b, 1)));
	a = nir_bcsel(b, nir_ige(b, nir_imm_int(b, 0), d16), dark,
		      nir_bcsel(b, nir_ige(b, d16, size16), nir_imm_int(b, 0), ag));
	/* where the window covers part of the pixel, the shadow shows through the rest */
	a = nir_bcsel(b, inwin, div255(b, mul(b, a, nir_isub(b, nir_imm_int(b, 255), cov))), a);
	nir_terminate_if(b, nir_ieq_imm(b, a, 0));
	fs_out(&f, darker(b, fs_behind(&f), a));
	return b->shader;
}

/*
 * The spread (compose.c cp_spread_plan): a level of what shows through
 * worked out from another, one pixel to each fragment, the level read
 * being B_SRC, its pixel (x, y) the word at y * PITCH + 4 x. A read is
 * held to the pixels of that level that touch the box.
 */
static nir_def *lv_x( nir_builder *b, nir_def *x )
{
	return nir_ishl_imm(b, clampi(b, x, P(b, PL_VL), nir_iadd_imm(b, P(b, PL_VR), -1)), 2);
}

static nir_def *lv_y( nir_builder *b, nir_def *y )
{
	return mul(b, clampi(b, y, P(b, PL_VT), nir_iadd_imm(b, P(b, PL_VB), -1)), P(b, PL_PITCH));
}

/*
 * Halving and doubling sum blue and red together in the two halves of
 * one word, and green alone: no sum of theirs passes 16 times 255.
 * Each is rounded once, by 2^s, as compose.c's unlanes does.
 */
static void rbg_add( nir_builder *b, nir_def *rb1, nir_def *g1, nir_def **rb, nir_def **g )
{
	*rb = ( *rb == NULL ) ? rb1 : nir_iadd(b, *rb, rb1);
	*g = ( *g == NULL ) ? g1 : nir_iadd(b, *g, g1);
}

static nir_def *rbg_round( nir_builder *b, nir_def *rb, nir_def *g, int s )
{
	rb = nir_iand_imm(b, nir_ushr_imm(b, nir_iadd_imm(b, rb, ( 1 << ( s - 1 ) ) * 0x00010001), s), 0x00FF00FF);
	g = nir_ushr_imm(b, nir_iadd_imm(b, g, 1 << ( s - 1 )), s);
	return nir_ior(b, rb, nir_ishl_imm(b, g, 8));
}

/* x three times when 'three', else as it is */
static nir_def *times3_if( nir_builder *b, nir_def *x, nir_def *three )
{
	return nir_bcsel(b, three, nir_iadd(b, x, nir_ishl_imm(b, x, 1)), x);
}

/*
 * Halving: the pixel (x, y) from the four by four of the level below
 * from (2x - 1, 2y - 1), weighed 1 3 3 1 each way, of 64
 */
static nir_shader *fs_halve( void )
{
	static const unsigned	wt[4] = { 1, 3, 3, 1 };
	FS			f;
	nir_builder		*b;
	nir_def			*xo[4], *yo[4], *rb = NULL, *g = NULL;
	int			i, j;

	fs_begin(&f, "cp_halve", 1);
	b = &f.b;
	for ( i = 0; i < 4; i++ ) {
		xo[i] = lv_x(b, nir_iadd_imm(b, nir_ishl_imm(b, f.x, 1), i - 1));
		yo[i] = lv_y(b, nir_iadd_imm(b, nir_ishl_imm(b, f.y, 1), i - 1));
	}
	for ( j = 0; j < 4; j++ ) {
		for ( i = 0; i < 4; i++ ) {
			nir_def		*v = ld(b, B_SRC, nir_iadd(b, yo[j], xo[i]));
			nir_def		*vrb = nir_iand_imm(b, v, 0x00FF00FF), *vg = ch(b, v, 1);
			unsigned	w = wt[i] * wt[j];

			if ( w != 1 ) {
				vrb = mul(b, vrb, nir_imm_int(b, (int)w));
				vg = mul(b, vg, nir_imm_int(b, (int)w));
			}
			rbg_add(b, vrb, vg, &rb, &g);
		}
	}
	fs_out(&f, rbg_round(b, rb, g, 6));
	return b->shader;
}

/*
 * Doubling: the pixel (x, y) from the two by two of the level above
 * nearest its centre, from (x / 2, y / 2) toward the side the pixel lies
 * on, the nearer weighed 3 and the further 1 each way, of 16
 */
static nir_shader *fs_double( void )
{
	FS		f;
	nir_builder	*b;
	nir_def		*xodd, *yodd, *xa, *xb, *ya, *yb, *rb = NULL, *g = NULL;
	nir_def		*hrb[2], *hg[2];
	int		j;

	fs_begin(&f, "cp_double", 1);
	b = &f.b;
	xodd = nir_ine_imm(b, nir_iand_imm(b, f.x, 1), 0);
	yodd = nir_ine_imm(b, nir_iand_imm(b, f.y, 1), 0);
	xa = nir_isub(b, nir_ishr_imm(b, f.x, 1), nir_b2i32(b, nir_inot(b, xodd)));
	ya = nir_isub(b, nir_ishr_imm(b, f.y, 1), nir_b2i32(b, nir_inot(b, yodd)));
	xb = lv_x(b, nir_iadd_imm(b, xa, 1));
	yb = lv_y(b, nir_iadd_imm(b, ya, 1));
	xa = lv_x(b, xa);
	ya = lv_y(b, ya);

	/* along each of the two rows: the left weighed 3 on an odd column, the right on an even */
	for ( j = 0; j < 2; j++ ) {
		nir_def	*row = ( j == 0 ) ? ya : yb;
		nir_def	*va = ld(b, B_SRC, nir_iadd(b, row, xa)), *vb = ld(b, B_SRC, nir_iadd(b, row, xb));
		nir_def	*arb = nir_iand_imm(b, va, 0x00FF00FF), *ag = ch(b, va, 1);
		nir_def	*brb = nir_iand_imm(b, vb, 0x00FF00FF), *bg = ch(b, vb, 1);

		hrb[j] = nir_iadd(b, times3_if(b, arb, xodd), times3_if(b, brb, nir_inot(b, xodd)));
		hg[j] = nir_iadd(b, times3_if(b, ag, xodd), times3_if(b, bg, nir_inot(b, xodd)));
	}
	/* and down: the upper row weighed 3 on an odd row, the lower on an even */
	rbg_add(b, times3_if(b, hrb[0], yodd), times3_if(b, hg[0], yodd), &rb, &g);
	rbg_add(b, times3_if(b, hrb[1], nir_inot(b, yodd)), times3_if(b, hg[1], nir_inot(b, yodd)), &rb, &g);
	fs_out(&f, rbg_round(b, rb, g, 4));
	return b->shader;
}

/*
 * Smoothing: the pixel (x, y) from the (2 taps + 1) square round it at
 * its own level, weighed A B M B A each way (taps 2) or B M B (taps 1),
 * of 64 each way; each channel summed alone, since a sum reaches 4096
 * times 255, and rounded once
 */
static nir_shader *fs_smooth( const char *name, int taps )
{
	FS		f;
	nir_builder	*b;
	nir_def		*wt[5], *xo[5], *s[3] = { NULL, NULL, NULL };
	int		i, j, k;

	fs_begin(&f, name, 1);
	b = &f.b;
	wt[0] = wt[4] = P(b, PL_A);
	wt[1] = wt[3] = P(b, PL_B);
	wt[2] = P(b, PL_M);
	for ( i = 2 - taps; i <= 2 + taps; i++ ) {
		xo[i] = lv_x(b, nir_iadd_imm(b, f.x, i - 2));
	}
	for ( j = 2 - taps; j <= 2 + taps; j++ ) {
		nir_def	*yo = lv_y(b, nir_iadd_imm(b, f.y, j - 2)), *h[3] = { NULL, NULL, NULL };

		for ( i = 2 - taps; i <= 2 + taps; i++ ) {
			nir_def	*v = ld(b, B_SRC, nir_iadd(b, yo, xo[i]));

			for ( k = 0; k < 3; k++ ) {
				nir_def	*t = mul(b, ch(b, v, k), wt[i]);

				h[k] = ( h[k] == NULL ) ? t : nir_iadd(b, h[k], t);
			}
		}
		for ( k = 0; k < 3; k++ ) {
			nir_def	*t = mul(b, h[k], wt[j]);

			s[k] = ( s[k] == NULL ) ? t : nir_iadd(b, s[k], t);
		}
	}
	for ( k = 0; k < 3; k++ ) {
		s[k] = nir_ushr_imm(b, nir_iadd_imm(b, s[k], 2048), 12);
	}
	fs_out(&f, pack4(b, s[0], s[1], s[2], nir_imm_int(b, 0)));
	return b->shader;
}

static nir_shader *fs_smooth3( void )
{
	return fs_smooth("cp_smooth3", 1);
}

static nir_shader *fs_smooth5( void )
{
	return fs_smooth("cp_smooth5", 2);
}

/*
 * A window opening or closing (compose.c cp_moving): each pixel of where
 * it is shown taken from the pixel of the window it falls on, the work
 * area by how solid it is and the frame by how solid the frame is, the
 * frame tinted and washed, round corners on where it is shown.
 */
static nir_shader *fs_moving( void )
{
	FS		f;
	nir_builder	*b;
	nir_def		*x, *y, *sl, *st, *wy, *wx, *ay, *ax, *c, *workrow, *bandrow, *inwork;
	nir_def		*a, *inrow, *cov, *tstr, *bstr, *wstr, *tinted, *washed, *frame;

	fs_begin(&f, "cp_moving", 3);
	b = &f.b;
	x = f.x;  y = f.y;
	sl = P(b, PM_SL);  st = P(b, PM_ST);
	wy = nir_ushr_imm(b, nir_imul(b, nir_isub(b, y, st), P(b, PM_STEPY)), 16);
	wx = nir_ushr_imm(b, nir_imul(b, nir_isub(b, x, sl), P(b, PM_STEPX)), 16);
	wy = clampi(b, wy, nir_imm_int(b, 0), nir_iadd_imm(b, nir_isub(b, P(b, PM_OB), P(b, PM_OT)), -1));
	wx = clampi(b, wx, nir_imm_int(b, 0), nir_iadd_imm(b, nir_isub(b, P(b, PM_OR), P(b, PM_OL)), -1));
	ay = nir_iadd(b, P(b, PM_OT), wy);
	ax = nir_iadd(b, P(b, PM_OL), wx);
	c = ld(b, B_SRC, nir_iadd(b, mul(b, wy, P(b, PM_PITCH)), nir_ishl_imm(b, wx, 2)));

	workrow = nir_iand(b, nir_ige(b, ay, P(b, PM_WT)), nir_ilt(b, ay, P(b, PM_WB)));
	bandrow = nir_iand(b, nir_ilt(b, ay, P(b, PM_WT)), nir_ine_imm(b, P(b, PM_BAND), 0));
	inwork = nir_iand(b, workrow, nir_iand(b, nir_ige(b, ax, P(b, PM_WL)), nir_ilt(b, ax, P(b, PM_WR))));
	a = nir_bcsel(b, inwork, P(b, PM_AW), P(b, PM_AF));
	cover_at(b, x, y, sl, st, P(b, PM_SR), P(b, PM_SB), P(b, PM_RAD), &inrow, &cov);
	a = nir_bcsel(b, inrow, div255(b, nir_iadd_imm(b, mul(b, a, cov), 127)), a);
	nir_terminate_if(b, nir_ieq_imm(b, a, 0));

	tstr = P(b, PM_TSTR);
	tinted = nir_bcsel(b, nir_ine_imm(b, tstr, 0), over_of(b, c, P(b, PM_TINT), tstr), c);
	bstr = P(b, PM_BSTR);
	wstr = P(b, PM_WSTR);
	washed = nir_bcsel(b, nir_iand(b, bandrow, nir_ine_imm(b, bstr, 0)),
			   over_of(b, tinted, P(b, PM_BWASH), bstr),
			   nir_bcsel(b, nir_ine_imm(b, wstr, 0), over_of(b, tinted, P(b, PM_WASH), wstr),
				     tinted));
	frame = washed;
	c = nir_bcsel(b, inwork, c, frame);
	fs_out(&f, nir_bcsel(b, nir_uge_imm(b, a, 255), c, over_of(b, fs_behind(&f), c, a)));
	return b->shader;
}

/*
 * Three shaders that do one thing each, for the check on the board to
 * tell which part of the pipeline goes wrong when a scene does: a
 * colour from a parameter (the draws reach the pixels), a pixel read
 * from memory (the TMU), what is behind turned over (the tile buffer
 * read back).
 */
static nir_shader *fs_fill( void )
{
	FS	f;

	fs_begin(&f, "cp_fill", 0);
	fs_out(&f, P(&f.b, PD_COLOUR));
	return f.b.shader;
}

static nir_shader *fs_copy( void )
{
	FS		f;
	nir_builder	*b;
	nir_def		*cx, *cy;

	fs_begin(&f, "cp_copy", 1);
	b = &f.b;
	cx = clampi(b, nir_isub(b, f.x, P(b, PD_OL)), nir_imm_int(b, 0), nir_iadd_imm(b, P(b, PD_W), -1));
	cy = clampi(b, nir_isub(b, f.y, P(b, PD_OT)), nir_imm_int(b, 0), nir_iadd_imm(b, P(b, PD_H), -1));
	fs_out(&f, ld(b, B_SRC, nir_iadd(b, mul(b, cy, P(b, PD_PITCH)), nir_ishl_imm(b, cx, 2))));
	return b->shader;
}

static nir_shader *fs_invert( void )
{
	FS	f;

	fs_begin(&f, "cp_invert", 0);
	fs_out(&f, nir_ixor(&f.b, fs_behind(&f), nir_imm_int(&f.b, 0x00FFFFFF)));
	return f.b.shader;
}

/* The corners of a rectangle, already in clip space, passed on */
static nir_shader *vs_rect( void )
{
	nir_builder	b = nir_builder_init_simple_shader(MESA_SHADER_VERTEX, &nir_options, "cp_rect");
	nir_variable	*in, *out;
	nir_def		*p;

	in = nir_variable_create(b.shader, nir_var_shader_in, glsl_vec_type(2), "corner");
	in->data.location = VERT_ATTRIB_GENERIC0;
	in->data.driver_location = 0;
	out = nir_variable_create(b.shader, nir_var_shader_out, glsl_vec4_type(), "gl_Position");
	out->data.location = VARYING_SLOT_POS;
	p = nir_load_var(&b, in);
	nir_store_var(&b, out, nir_vec4(&b, nir_channel(&b, p, 0), nir_channel(&b, p, 1),
					 nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f)), 0xF);
	return b.shader;
}

/* ---------------------------------------------------------------- compiling */

static unsigned type_size( const struct glsl_type *type, bool bindless )
{
	return glsl_count_attribute_slots(type, false);
}

/*
 * The shader in the form the V3D compiler takes: variables in SSA form,
 * the fragment shader's inputs and outputs as intrinsics, constants
 * scalar, copies gone, and the information gathered again.
 */
static void prepare( nir_shader *s )
{
	NIR_PASS(_, s, nir_lower_vars_to_ssa);
	NIR_PASS(_, s, nir_lower_global_vars_to_local);
	NIR_PASS(_, s, nir_lower_vars_to_ssa);
	nir_shader_gather_info(s, nir_shader_get_entrypoint(s));
	if ( s->info.stage == MESA_SHADER_FRAGMENT ) {
		NIR_PASS(_, s, nir_lower_io, nir_var_shader_in | nir_var_shader_out, type_size,
			 (nir_lower_io_options)0);
	}
	NIR_PASS(_, s, nir_lower_load_const_to_scalar);
	v3d_optimize_nir(NULL, s);
	NIR_PASS(_, s, nir_lower_var_copies);
	v3d_optimize_nir(NULL, s);
	NIR_PASS(_, s, nir_remove_dead_variables, nir_var_function_temp, NULL);
	nir_sweep(s);
	nir_shader_gather_info(s, nir_shader_get_entrypoint(s));
	nir_validate_shader(s, "prepared");
}

typedef struct {
	const char		*name;
	uint64_t		*code;
	uint32_t		size;
	struct v3d_prog_data	*pd;
} PROG;

static void debug_out( const char *msg, void *data )
{
	fprintf(stderr, "  compiler: %s\n", msg);
}

static void compile( PROG *p, const char *name, nir_shader *s, struct v3d_key *key )
{
	static int	id = 0;

	prepare(s);
	p->name = name;
	p->pd = NULL;
	p->code = v3d_compile(compiler, key, &p->pd, s, debug_out, NULL, ++id, 0, &p->size);
	if ( p->code == NULL || p->size == 0 ) die("%s did not compile", name);
	if ( p->pd->spill_size != 0 ) die("%s spills registers", name);
	fprintf(stderr, "%s: %u instructions, %u threads%s, %u uniforms\n", name,
		p->size / 8, p->pd->threads, p->pd->single_seg ? ", single segment" : "",
		p->pd->uniforms.count);
}

static PROG	vs, cs;
#define NFS	10
static PROG	fs[NFS];
static const char *fs_names[NFS] = { "piece", "shadow", "halve", "smooth3", "smooth5", "double", "moving",
				     "fill", "copy", "invert" };

static void compile_all( void )
{
	struct v3d_vs_key	vk;
	struct v3d_fs_key	fk;
	nir_shader		*(*make[NFS])( void ) = { fs_piece, fs_shadow, fs_halve, fs_smooth3, fs_smooth5,
							  fs_double, fs_moving, fs_fill, fs_copy, fs_invert };
	int			i;

	memset(&vk, 0, sizeof(vk));
	vk.base.is_last_geometry_stage = true;
	compile(&vs, "vs", vs_rect(), &vk.base);

	memset(&vk, 0, sizeof(vk));
	vk.base.is_last_geometry_stage = true;
	vk.is_coord = true;
	for ( i = 0; i < 4; i++ ) {
		vk.used_outputs[vk.num_used_outputs++] = v3d_slot_from_slot_and_component(VARYING_SLOT_POS, i);
	}
	compile(&cs, "cs", vs_rect(), &vk.base);

	for ( i = 0; i < NFS; i++ ) {
		memset(&fk, 0, sizeof(fk));
		fk.cbufs = 1;
		fk.logicop_func = PIPE_LOGICOP_COPY;
		fk.color_fmt[0].format = PIPE_FORMAT_R8G8B8A8_UNORM;
		fk.color_fmt[0].swizzle[0] = PIPE_SWIZZLE_X;
		fk.color_fmt[0].swizzle[1] = PIPE_SWIZZLE_Y;
		fk.color_fmt[0].swizzle[2] = PIPE_SWIZZLE_Z;
		fk.color_fmt[0].swizzle[3] = PIPE_SWIZZLE_W;
		compile(&fs[i], fs_names[i], make[i](), &fk.base);
		{
			struct v3d_fs_prog_data	*pd = (struct v3d_fs_prog_data *)fs[i].pd;

			fprintf(stderr, "  writes z %d (from FEP %d), early z off %d, centre w %d, inputs %d, "
				"scoreboard on the first switch %d, writes memory %d\n", pd->writes_z,
				pd->writes_z_from_fep, pd->disable_ez, pd->uses_center_w, pd->num_inputs,
				pd->lock_scoreboard_on_first_thrsw, pd->base.tmu_dirty_rcl);
		}
	}
}

/* ---------------------------------------------------------------- writing out */

static FILE	*out;

static uint32_t fbits( float f )
{
	uint32_t	u;

	memcpy(&u, &f, 4);
	return u;
}

static void put_bytes( const char *name, const uint8_t *p, int n )
{
	int	i;

	fprintf(out, "LOCAL CONST UB %s[%d] __attribute__((aligned(4))) = {", name, n);
	for ( i = 0; i < n; i++ ) {
		fprintf(out, "%s0x%02x,", ( i % 12 ) == 0 ? "\n\t" : " ", p[i]);
	}
	fprintf(out, "\n};\n\n");
}

static void put_code( PROG *p )
{
	uint32_t	i, n = p->size / 8;

	fprintf(out, "/* %s: %u instructions */\n", p->name, n);
	fprintf(out, "LOCAL CONST UD cp_code_%s[%u] = {", p->name, n);
	for ( i = 0; i < n; i++ ) {
		fprintf(out, "%s0x%016llxULL,", ( i % 3 ) == 0 ? "\n\t" : " ", (unsigned long long)p->code[i]);
	}
	fprintf(out, "\n};\n\n");
}

/* A uniform stream as kinds and values for the kernel to fill */
static void put_uniforms( PROG *p )
{
	struct v3d_uniform_list	*u = &p->pd->uniforms;
	uint32_t		i;

	fprintf(out, "LOCAL CONST UW cp_unif_%s[%u][2] = {\n", p->name, u->count ? u->count : 1);
	for ( i = 0; i < u->count; i++ ) {
		uint32_t	kind, val = u->data[i];

		switch ( u->contents[i] ) {
		case QUNIFORM_CONSTANT:		kind = U_CONST;	break;
		case QUNIFORM_UNIFORM:		kind = U_PARAM;	break;
		case QUNIFORM_SSBO_OFFSET:	kind = U_SSBO;	break;
		case QUNIFORM_VIEWPORT_X_SCALE:
		case QUNIFORM_VIEWPORT_Y_SCALE:
			kind = U_CONST;
			val = fbits((float)VP_HALF * devinfo.clipper_xy_granularity);
			break;
		case QUNIFORM_VIEWPORT_Z_OFFSET:
		case QUNIFORM_VIEWPORT_Z_SCALE:
			kind = U_CONST;
			val = fbits(0.5f);
			break;
		default:
			die("%s: a uniform of kind %d the kernel cannot fill", p->name, u->contents[i]);
		}
		if ( kind == U_PARAM && val >= 32 ) die("%s: parameter %u", p->name, val);
		if ( kind == U_SSBO && val > B_COV ) die("%s: buffer %u", p->name, val);
		fprintf(out, "\t{ %u, 0x%08x },\n", kind, val);
	}
	if ( u->count == 0 ) fprintf(out, "\t{ 0, 0 },\n");
	fprintf(out, "};\n\n");
}

static void dump( PROG *p, FILE *f )
{
	uint32_t	i;

	fprintf(f, "---- %s\n", p->name);
	for ( i = 0; i < p->size / 8; i++ ) {
		fprintf(f, "%4u: %s\n", i, v3d_qpu_disasm(&devinfo, p->code[i]));
	}
	for ( i = 0; i < p->pd->uniforms.count; i++ ) {
		fprintf(f, "  unif %u: kind %d data %08x\n", i, p->pd->uniforms.contents[i], p->pd->uniforms.data[i]);
	}
}

/* ---------------------------------------------------------------- the shader state record */

typedef struct {
	uint32_t	fs_code, fs_unif, vs_code, vs_unif, cs_code, cs_unif;
} ADDRS;

static struct vpm_config	vpm_bin, vpm_render;

#define FILL_RECORD(rec, f, a) do { \
	struct v3d_vs_prog_data *_c = (struct v3d_vs_prog_data *)cs.pd; \
	struct v3d_vs_prog_data *_v = (struct v3d_vs_prog_data *)vs.pd; \
	struct v3d_fs_prog_data *_f = (struct v3d_fs_prog_data *)(f)->pd; \
	(rec).enable_clipping = true; \
	(rec).fragment_shader_does_z_writes = _f->writes_z; \
	(rec).turn_off_early_z_test = _f->disable_ez; \
	(rec).fragment_shader_uses_real_pixel_centre_w_in_addition_to_centroid_w2 = _f->uses_center_w; \
	(rec).any_shader_reads_hardware_written_primitive_id = _f->uses_pid; \
	(rec).insert_primitive_id_as_first_varying_to_fragment_shader = _f->uses_pid; \
	(rec).do_scoreboard_wait_on_first_thread_switch = _f->lock_scoreboard_on_first_thrsw; \
	(rec).disable_implicit_point_line_varyings = !_f->uses_implicit_point_line_varyings; \
	(rec).number_of_varyings_in_fragment_shader = _f->num_inputs; \
	(rec).coordinate_shader_code_address = (a)->cs_code; \
	(rec).vertex_shader_code_address = (a)->vs_code; \
	(rec).fragment_shader_code_address = (a)->fs_code; \
	(rec).coordinate_shader_input_vpm_segment_size = _c->vpm_input_size; \
	(rec).vertex_shader_input_vpm_segment_size = _v->vpm_input_size; \
	(rec).coordinate_shader_output_vpm_segment_size = _c->vpm_output_size; \
	(rec).vertex_shader_output_vpm_segment_size = _v->vpm_output_size; \
	(rec).coordinate_shader_uniforms_address = (a)->cs_unif; \
	(rec).vertex_shader_uniforms_address = (a)->vs_unif; \
	(rec).fragment_shader_uniforms_address = (a)->fs_unif; \
	(rec).min_coord_shader_input_segments_required_in_play = vpm_bin.As; \
	(rec).min_vertex_shader_input_segments_required_in_play = vpm_render.As; \
	(rec).min_coord_shader_output_segments_required_in_play_in_addition_to_vcm_cache_size = vpm_bin.Ve; \
	(rec).min_vertex_shader_output_segments_required_in_play_in_addition_to_vcm_cache_size = vpm_render.Ve; \
	(rec).coordinate_shader_4_way_threadable = _c->base.threads == 4; \
	(rec).vertex_shader_4_way_threadable = _v->base.threads == 4; \
	(rec).fragment_shader_4_way_threadable = _f->base.threads == 4; \
	(rec).coordinate_shader_start_in_final_thread_section = _c->base.single_seg; \
	(rec).vertex_shader_start_in_final_thread_section = _v->base.single_seg; \
	(rec).fragment_shader_start_in_final_thread_section = _f->base.single_seg; \
	(rec).vertex_id_read_by_coordinate_shader = _c->uses_vid; \
	(rec).instance_id_read_by_coordinate_shader = _c->uses_iid; \
	(rec).vertex_id_read_by_vertex_shader = _v->uses_vid; \
	(rec).instance_id_read_by_vertex_shader = _v->uses_iid; \
} while ( 0 )

static void pack_record( PROG *f, const ADDRS *a, bool draw_index, uint8_t *cl )
{
	if ( draw_index ) {
		struct V3D71_GL_SHADER_STATE_RECORD_DRAW_INDEX	rec = { V3D71_GL_SHADER_STATE_RECORD_DRAW_INDEX_header };

		FILL_RECORD(rec, f, a);
		V3D71_GL_SHADER_STATE_RECORD_DRAW_INDEX_pack(NULL, cl, &rec);
	} else {
		struct V3D71_GL_SHADER_STATE_RECORD		rec = { V3D71_GL_SHADER_STATE_RECORD_header };

		FILL_RECORD(rec, f, a);
		V3D71_GL_SHADER_STATE_RECORD_pack(NULL, cl, &rec);
	}
}

/*
 * The word of a packed record that one address lands in, found by
 * packing with only that address set; it has to land whole in one
 * aligned word, so that the kernel can add an address to the word.
 */
static int address_word( PROG *f, bool draw_index, int which, int len )
{
	uint8_t		r0[64], r1[64];
	ADDRS		a0, a1;
	uint32_t	*w0 = (uint32_t *)r0, *w1 = (uint32_t *)r1;
	const uint32_t	probe = 0x12345600;
	int		i, found = -1;

	memset(&a0, 0, sizeof(a0));
	a1 = a0;
	((uint32_t *)&a1)[which] = probe;
	memset(r0, 0, sizeof(r0));
	memset(r1, 0, sizeof(r1));
	pack_record(f, &a0, draw_index, r0);
	pack_record(f, &a1, draw_index, r1);
	for ( i = 0; i < len / 4; i++ ) {
		if ( w0[i] == w1[i] ) continue;
		if ( found >= 0 || w1[i] - w0[i] != probe ) die("address %d of the record is not a whole word", which);
		found = i;
	}
	if ( found < 0 ) die("address %d not found in the record", which);
	return found * 4;
}

/* ---------------------------------------------------------------- the binner's state */

#define EMIT(buf, n, pkt, ...) do { \
		struct V3D71_##pkt _v = { V3D71_##pkt##_header, __VA_ARGS__ }; \
		V3D71_##pkt##_pack(NULL, (buf) + (n), &_v); \
		(n) += V3D71_##pkt##_length; \
	} while ( 0 )

/*
 * What every draw of every job shares, after START_TILE_BINNING: the
 * viewport (a fixed one, larger than any screen), no culling, no depth
 * test, no blending (the shaders mix), every channel written.
 */
static int state_prefix( uint8_t *p )
{
	int	n = 0;

	EMIT(p, n, CLIPPER_XY_SCALING,
	     .viewport_half_width_in_1_64th_of_pixel = (float)VP_HALF * 64.0f,
	     .viewport_half_height_in_1_64th_of_pixel = (float)VP_HALF * 64.0f);
	EMIT(p, n, CLIPPER_Z_SCALE_AND_OFFSET,
	     .viewport_z_offset_zc_to_zs = 0.5f,
	     .viewport_z_scale_zc_to_zs = 0.5f);
	EMIT(p, n, CLIPPER_Z_MIN_MAX_CLIPPING_PLANES,
	     .minimum_zw = 0.0f,
	     .maximum_zw = 1.0f);
	EMIT(p, n, VIEWPORT_OFFSET,
	     .fine_x = (float)VP_HALF,
	     .fine_y = (float)VP_HALF,
	     .coarse_x = 0,
	     .coarse_y = 0);
	EMIT(p, n, CFG_BITS,
	     .enable_forward_facing_primitive = true,
	     .enable_reverse_facing_primitive = true,
	     .clockwise_primitives = false,
	     .enable_depth_offset = false,
	     .rasterizer_oversample_mode = 0,
	     .blend_enable = false,
	     .z_updates_enable = false,
	     .depth_test_function = V3D_COMPARE_FUNC_ALWAYS,
	     .stencil_enable = false,
	     .line_rasterization = V3D_LINE_RASTERIZATION_DIAMOND_EXIT,
	     .z_clipping_mode = V3D_Z_CLIP_MODE_MIN_ONE_TO_ONE,
	     .z_clamp_mode = false);
	EMIT(p, n, BLEND_ENABLES, .mask = 0);
	EMIT(p, n, COLOR_WRITE_MASKS, .mask = 0);
	EMIT(p, n, ZERO_ALL_FLAT_SHADE_FLAGS);
	EMIT(p, n, ZERO_ALL_NON_PERSPECTIVE_FLAGS);
	EMIT(p, n, ZERO_ALL_CENTROID_FLAGS);
	EMIT(p, n, POINT_SIZE, .point_size = 1.0f);
	EMIT(p, n, LINE_WIDTH, .line_width = 1.0f);
	EMIT(p, n, SAMPLE_STATE, .coverage = 1.0f, .mask = 0xF);
	return n;
}

/* ---------------------------------------------------------------- main */

static const char notice[] =
"/*\n"
" * device/gpu/v3d/mesa/v3d_cp_shaders.h\n"
" *\n"
" * The shaders that composite windows on the V3D 7.1 (docs/tessronos/13-gpu.md\n"
" * 13.10), generated by \"tools/mesa/shaders.sh\" (tools/mesa/gen_cp_shaders.c)\n"
" * with the NIR and V3D shader compiler of Mesa %s. Do not edit; generate it\n"
" * again. The QPU code and the packed records below are the output of that\n"
" * compiler and of its packet packing.\n"
" *\n"
" * Copyright (C) 2014-2017 Broadcom\n"
" * Copyright (C) 2014-2016 Intel Corporation\n"
" * and the other authors of Mesa's src/compiler/nir, src/broadcom and\n"
" * src/util, as each of their files says\n"
" *\n"
" * Permission is hereby granted, free of charge, to any person obtaining a\n"
" * copy of this software and associated documentation files (the \"Software\"),\n"
" * to deal in the Software without restriction, including without limitation\n"
" * the rights to use, copy, modify, merge, publish, distribute, sublicense,\n"
" * and/or sell copies of the Software, and to permit persons to whom the\n"
" * Software is furnished to do so, subject to the following conditions:\n"
" *\n"
" * The above copyright notice and this permission notice (including the next\n"
" * paragraph) shall be included in all copies or substantial portions of the\n"
" * Software.\n"
" *\n"
" * THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
" * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
" * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL\n"
" * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
" * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING\n"
" * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS\n"
" * IN THE SOFTWARE.\n"
" */\n\n";

int main( int argc, char **argv )
{
	ADDRS		zero;
	uint8_t		rec[64], buf[256];
	struct V3D71_GL_SHADER_STATE_ATTRIBUTE_RECORD at = { V3D71_GL_SHADER_STATE_ATTRIBUTE_RECORD_header };
	struct v3d_vs_prog_data *cpd, *vpd;
	FILE		*dis;
	int		i, n, w;

	if ( argc < 3 ) die("usage: gen_cp_shaders <out.h> <mesa version>");
	div255_check();
	glsl_type_singleton_init_or_ref();
	device_init();
	compile_all();

	cpd = (struct v3d_vs_prog_data *)cs.pd;
	vpd = (struct v3d_vs_prog_data *)vs.pd;
	if ( !v3d_compute_vpm_config(&devinfo, cpd, vpd, NULL, NULL, &vpm_bin, &vpm_render) ) {
		die("no VPM configuration fits");
	}

	dis = fopen("v3d_cp_shaders.dis", "w");
	if ( dis != NULL ) {
		dump(&cs, dis);
		dump(&vs, dis);
		for ( i = 0; i < NFS; i++ ) dump(&fs[i], dis);
		fclose(dis);
	}

	out = fopen(argv[1], "w");
	if ( out == NULL ) die("cannot write %s", argv[1]);
	fprintf(out, notice, argv[2]);
	fprintf(out, "#ifndef _V3D_CP_SHADERS_H_\n#define _V3D_CP_SHADERS_H_\n\n");

	fprintf(out, "/* The V3D the code is for: 7.1, hub revision %d or later */\n", devinfo.rev);
	fprintf(out, "#define CP_V3D_VER\t\t%d\n#define CP_V3D_REV_MIN\t\t%d\n", devinfo.ver, devinfo.rev);
	fprintf(out, "/* The shader state record with draw index and base vertex from this revision */\n");
	fprintf(out, "#define CP_V3D_REV_DRAW_INDEX\t10\n\n");
	fprintf(out, "/* Clip space covers [0, 2 * CP_VP_HALF) pixels each way */\n");
	fprintf(out, "#define CP_VP_HALF\t\t%d\n\n", VP_HALF);
	fprintf(out, "/* What a word of a uniform stream holds */\n");
	fprintf(out, "#define CP_U_CONST\t\t%d\n#define CP_U_PARAM\t\t%d\n#define CP_U_SSBO\t\t%d\n\n",
		U_CONST, U_PARAM, U_SSBO);
	fprintf(out, "/* The draw's buffers */\n");
	fprintf(out, "#define CP_B_SRC\t\t%d\n#define CP_B_AUX\t\t%d\n#define CP_B_COV\t\t%d\n#define CP_NBUF\t\t\t3\n\n",
		B_SRC, B_AUX, B_COV);
	fprintf(out, "/* The draw's parameters */\n");
	for ( i = 0; i < (int)( sizeof(param_names) / sizeof(param_names[0]) ); i++ ) {
		fprintf(out, "#define %-15s\t%d\n", param_names[i].name, param_names[i].value);
	}
	fprintf(out, "#define CP_NPARAM\t\t32\n\n");

	fprintf(out, "/* The fragment shaders, in the order of cp_fs[] */\n");
	for ( i = 0; i < NFS; i++ ) {
		char	up[16];
		int	k;

		for ( k = 0; fs_names[i][k] != 0 && k < 15; k++ ) {
			up[k] = ( fs_names[i][k] >= 'a' && fs_names[i][k] <= 'z' ) ? fs_names[i][k] - 'a' + 'A'
										  : fs_names[i][k];
		}
		up[k] = 0;
		fprintf(out, "#define CP_FS_%s\t\t%d\n", up, i);
	}
	fprintf(out, "#define CP_NFS\t\t\t%d\n", NFS);
	n = 0;
	for ( i = 0; i < NFS; i++ ) {
		if ( fs[i].pd->tmu_dirty_rcl ) n |= 1 << i;
	}
	fprintf(out, "/* Those that write memory through the TMU: a job with one needs the GPU's caches cleaned after it */\n");
	fprintf(out, "#define CP_FS_TMU_WRITES\t0x%x\n\n", n);

	put_code(&cs);
	put_code(&vs);
	for ( i = 0; i < NFS; i++ ) put_code(&fs[i]);
	put_uniforms(&cs);
	put_uniforms(&vs);
	for ( i = 0; i < NFS; i++ ) put_uniforms(&fs[i]);

	/* the records, addresses 0 */
	memset(&zero, 0, sizeof(zero));
	for ( i = 0; i < NFS; i++ ) {
		char	name[64];

		memset(rec, 0, sizeof(rec));
		pack_record(&fs[i], &zero, false, rec);
		snprintf(name, sizeof(name), "cp_rec_%s", fs_names[i]);
		put_bytes(name, rec, V3D71_GL_SHADER_STATE_RECORD_length);
		memset(rec, 0, sizeof(rec));
		pack_record(&fs[i], &zero, true, rec);
		snprintf(name, sizeof(name), "cp_recdi_%s", fs_names[i]);
		put_bytes(name, rec, V3D71_GL_SHADER_STATE_RECORD_DRAW_INDEX_length);
	}
	fprintf(out, "#define CP_REC_LEN\t\t%d\n", V3D71_GL_SHADER_STATE_RECORD_length);
	if ( V3D71_GL_SHADER_STATE_RECORD_DRAW_INDEX_length != V3D71_GL_SHADER_STATE_RECORD_length ) {
		die("the two records differ in length");
	}
	{
		static const char *an[6] = { "FS_CODE", "FS_UNIF", "VS_CODE", "VS_UNIF", "CS_CODE", "CS_UNIF" };
		int	k;

		for ( k = 0; k < 6; k++ ) {
			fprintf(out, "#define CP_REC_%s\t\t%d\n", an[k],
				address_word(&fs[0], false, k, V3D71_GL_SHADER_STATE_RECORD_length));
			fprintf(out, "#define CP_RECDI_%s\t%d\n", an[k],
				address_word(&fs[0], true, k, V3D71_GL_SHADER_STATE_RECORD_length));
		}
	}
	fprintf(out, "\n");

	/* the one attribute: the corners, two floats each */
	at.address = 0;
	at.vec_size = 2;
	at.type = ATTRIBUTE_FLOAT;
	at.stride = 8;
	at.number_of_values_read_by_coordinate_shader = cpd->vattr_sizes[0] ? cpd->vattr_sizes[0] : 1;
	at.number_of_values_read_by_vertex_shader = vpd->vattr_sizes[0] ? vpd->vattr_sizes[0] : 1;
	at.maximum_index = 0xFFFFFF;
	memset(buf, 0, sizeof(buf));
	V3D71_GL_SHADER_STATE_ATTRIBUTE_RECORD_pack(NULL, buf, &at);
	put_bytes("cp_attr", buf, V3D71_GL_SHADER_STATE_ATTRIBUTE_RECORD_length);
	{
		uint8_t	b2[64];
		int	k, found = -1;

		at.address = 0x12345600;
		memset(b2, 0, sizeof(b2));
		V3D71_GL_SHADER_STATE_ATTRIBUTE_RECORD_pack(NULL, b2, &at);
		for ( k = 0; k < V3D71_GL_SHADER_STATE_ATTRIBUTE_RECORD_length / 4; k++ ) {
			uint32_t	x0 = ((uint32_t *)buf)[k], x1 = ((uint32_t *)b2)[k];

			if ( x0 == x1 ) continue;
			if ( found >= 0 || x1 - x0 != 0x12345600 ) die("the attribute's address is not a whole word");
			found = k;
		}
		fprintf(out, "#define CP_ATTR_LEN\t\t%d\n#define CP_ATTR_ADDR\t\t%d\n\n",
			V3D71_GL_SHADER_STATE_ATTRIBUTE_RECORD_length, found * 4);
	}

	/* before each draw, the vertex cache */
	n = 0;
	EMIT(buf, n, VCM_CACHE_SIZE,
	     .number_of_16_vertex_batches_for_binning = vpm_bin.Vc,
	     .number_of_16_vertex_batches_for_rendering = vpm_render.Vc);
	put_bytes("cp_vcm", buf, n);

	/* after START_TILE_BINNING */
	n = state_prefix(buf);
	put_bytes("cp_state", buf, n);

	/* the render target's row stride: 64 pixel tiles, one word a pixel */
	w = v3d_compute_rt_row_row_stride_128_bits(64, v3d_internal_bpp_words(V3D_INTERNAL_BPP_32));
	fprintf(out, "/* The render target's stride in 128 bits covering two rows, 64 pixel tiles of 32 bits */\n");
	fprintf(out, "#define CP_RT_STRIDE\t\t%d\n\n", w);

	fprintf(out, "#endif /* _V3D_CP_SHADERS_H_ */\n");
	fclose(out);
	return 0;
}
