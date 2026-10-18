/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	hevc.c
 *	The HEVC decoder of the BCM2712 (Raspberry Pi 5) as the device
 *	object "動画の復号器" (design 6.7, include/ts/hevc.h).
 *
 *	The decoder works a picture in two passes:
 *
 *	  pass 1  reads the slices' bytes (the CABAC entropy decoding) and
 *	          writes what it found into two memories of the decoder's
 *	          own: the prediction units (PU) and the coefficients. It is
 *	          driven by a list of (register, value) pairs in memory, the
 *	          command list, which carries everything the slices say: the
 *	          parameters, the context variables' first values, the
 *	          scaling factors, a message per slice with its references
 *	          and weights, and where each tile or row of the wavefronts
 *	          starts and ends.
 *	  pass 2  reconstructs the picture from the PU and the coefficients
 *	          and the references, with the loop filters, into its slot;
 *	          it is set up by writing its registers directly.
 *
 *	Each pass ends with an interrupt of the decoder's own interrupt
 *	controller (ACTIVE1 for pass 1, ACTIVE2 for pass 2). Pass 1 may run
 *	out of room for the PU or the coefficients: it says which, the
 *	memory is made twice as large and pass 1 is run again.
 *
 *	A picture is decoded inside the write of its job, one at a time.
 *	Every memory the decoder reaches is physically contiguous and below
 *	4GB, in pages of the kernel's (the decoder reaches memory by its
 *	physical address in units of 64 bytes); the processor's cache is
 *	cleaned before the decoder reads and invalidated before the
 *	processor reads what the decoder wrote. The decoder's IOMMU (iommu2)
 *	is left in bypass.
 *
 *	The pictures are kept as the decoder writes them: 4:2:0 in columns
 *	128 bytes wide, the luma in one memory (each column the picture's
 *	height of rows) and the chroma, U and V by turns, in another (each
 *	column half as high). With 8 bits a row of a column is 128 samples
 *	of a byte. With 10 bits it is 32 little-endian words of three
 *	samples each, at bits 0, 10 and 20 (the top two bits unused), so a
 *	column is 96 samples wide; the chroma's samples run U, V, U in one
 *	word and V, U, V in the next. A read of a slot's picture gives it in
 *	rows, as NV12 for 8 bits and P010 for 10.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <tm/tmonitor.h>
#include <sys/sysdef.h>
#include "sysdepend/sysdepend.h"
#include "sysdepend/rpi/rpi_fw.h"
#include "sysman/pfalloc.h"
#include "obj/obj.h"
#include <ts/ob.h>
#include <ts/hevc.h>

/* ---------------------------------------------------------------- the machine */

#define HV_PA		0x1000800000ULL		/* the decoder's registers (64KB) */
#define HV_IC_PA	0x1000840000ULL		/* its interrupt controller */
#define HV_IOMMU_PA	0x1000005100ULL		/* the IOMMU in front of it */
#define HV_INTNO	(32 + 98)		/* SPI 98, level */
#define HV_CLOCK	11			/* the firmware's clock of the decoder */

#define REG(r)		( DEV_BASE(HV_PA) + (r) )
#define IC_CTRL		DEV_BASE(HV_IC_PA)
#define IOMMU_CTRL	DEV_BASE(HV_IOMMU_PA)

/* Pass 1: what the command list writes, and the few registers set directly */
#define R_SPS0		0x00
#define R_SPS1		0x04
#define R_PPS		0x08
#define R_SLICE		0x0C
#define R_TILESTART	0x10
#define R_TILEEND	0x14
#define R_SLICESTART	0x18
#define R_MODE		0x1C
#define R_QP		0x30
#define R_CONTROL	0x34
#define R_STATUS	0x38
#define R_VERSION	0x3C
#define R_BFBASE	0x40
#define R_BFNUM		0x44
#define R_BFCONTROL	0x48
#define R_PUWBASE	0x50
#define R_PUWSTRIDE	0x54
#define R_COEFFWBASE	0x58
#define R_COEFFWSTRIDE	0x5C
#define R_SLICECMDS	0x60
#define R_BEGINTILEEND	0x64
#define R_TRANSFER	0x68
#define R_CFBASE	0x6C
#define R_CFNUM		0x70
#define R_CFSTATUS	0x74
#define A_PROB		0x1000			/* the context variables' first values */
#define A_SCALING	0x2000			/* the scaling factors */
#define A_SLICEMSG	0x4000			/* the slice's messages, a word each */

/* Pass 2 */
#define R_PURBASE	0x8000
#define R_PURSTRIDE	0x8004
#define R_COEFFRBASE	0x8008
#define R_COEFFRSTRIDE	0x800C
#define R_NUMROWS	0x8010			/* written last: pass 2 starts */
#define R_CONFIG2	0x8014
#define R_OUTYBASE	0x8018
#define R_OUTYSTRIDE	0x801C
#define R_OUTCBASE	0x8020
#define R_OUTCSTRIDE	0x8024
#define R_FRAMESIZE	0x802C
#define R_MVBASE	0x8030
#define R_MVSTRIDE	0x8034
#define R_COLBASE	0x8038
#define R_COLSTRIDE	0x803C
#define R_CURRPOC	0x8040
#define R_REFS		0x9000			/* 16 references: luma, its stride, chroma, its stride */

/* The interrupt controller */
#define IC_ACTIVE1_INT	0x00000001U
#define IC_ACTIVE1_EN	0x00000004U
#define IC_ACTIVE2_INT	0x00000010U
#define IC_ACTIVE2_EN	0x00000040U
#define IC_ALL_INTS	0x22000011U
#define IC_ZERO		0x000FF800U		/* written as zeros */

/* The IOMMU */
#define MMU_ENABLE	0x00000001U
#define MMU_BYPASS	0x00000100U

/* What the transfer register does with the context variables */
#define CTX_SAVE	( ( 20U << 12 ) | ( 20U << 6 ) )
#define CTX_RESTORE	( ( 20U << 12 ) | 20U )

/* STATUS after pass 1: which memory was too small */
#define ST_COEFF_FULL	0x08
#define ST_PU_FULL	0x10

/* The firmware's tags */
#define TAG_SET_CLOCK_STATE	0x00038001U
#define TAG_GET_CLOCK_RATE	0x00030002U
#define TAG_SET_CLOCK_RATE	0x00038002U
#define TAG_GET_MAX_CLOCK_RATE	0x00030004U
#define TAG_DONE		0x80000000U

#define PASS_TMO	1000			/* ms a pass may take */
#define SCALING_BYTES	4064
#define CMD_ORDER	8			/* the command list: 1MB, 128K pairs */
#define BITS_ORDER	10			/* the slices' bytes: 4MB */

/* ---------------------------------------------------------------- memory */

/* What the processor wrote, out to memory and out of its cache */
LOCAL void cache_flush( CONST void *p, UD len )
{
	DCacheFlushRange(p, len);
}

typedef struct {
	PFRAME	*pf;
	UINT	order;
	UD	pa;
	UB	*va;
	UD	size;
} MEM;

LOCAL ER mem_get( MEM *m, UD size )
{
	UINT	order = 0;

	while ( ( PAGE_SIZE << order ) < size ) {
		if ( ++order > PF_MAX_ORDER ) return E_LIMIT;
	}
	m->pf = knl_alloc_pages(order, ZONE_DMA32, KAF_ZERO | KAF_DMA32);
	if ( m->pf == NULL ) return E_NOMEM;
	m->order = order;
	m->pa = knl_pf_to_pa(m->pf);
	m->va = (UB *)knl_pf_to_va(m->pf);
	m->size = PAGE_SIZE << order;
	cache_flush(m->va, m->size);	/* no dirty line of the zeroing left to land on what the decoder writes */
	return E_OK;
}

LOCAL void mem_put( MEM *m )
{
	if ( m->pf != NULL ) knl_free_pages(m->pf, m->order);
	knl_memset(m, 0, sizeof(*m));
}

/* An address and a length as the decoder takes them: in units of 64 bytes */
#define A64(pa)		( (UW)( (pa) >> 6 ) )
#define L64(n)		( (UW)( ( (n) + 63 ) >> 6 ) )

/* ---------------------------------------------------------------- the decoder's state */

typedef struct {
	MEM	luma, chroma;		/* the picture */
	MEM	mv;			/* its motion vectors, for a later picture's collocated ones */
} SLOT;

LOCAL struct {
	BOOL		present;
	ID		mtx;		/* one job at a time */
	ID		flg;		/* bit 0: pass 1 ended, bit 1: pass 2 ended */
	T_HEVCCONF	conf;
	SLOT		slot[HEVC_MAX_SLOTS];
	UW		ystride, cstride;	/* bytes between columns */
	UW		colw;			/* samples a column is wide: 128 for 8 bits, 96 for 10 */
	UW		mvstride;
	MEM		cmd, bits, pu, coeff;
	T_HEVCINFO	info;
	TS_UUID		uuid;
} hv;

/* ---------------------------------------------------------------- the interrupt */

LOCAL void hevc_inthdr( UINT intno )
{
	UW	v = in_w(IC_CTRL);

	if ( ( v & IC_ALL_INTS ) != 0 ) {
		out_w(IC_CTRL, v & ~IC_ZERO);		/* the latched ones cleared, the enables kept */
		if ( v & IC_ACTIVE2_INT ) tk_set_flg(hv.flg, 2);
		if ( v & IC_ACTIVE1_INT ) tk_set_flg(hv.flg, 1);
	}
	EndOfInt(intno);
}

/* Waits for a pass to end: E_OK, or E_TMOUT */
LOCAL ER pass_wait( UINT bit )
{
	UINT	got;

	return tk_wai_flg(hv.flg, bit, TWF_ORW | TWF_BITCLR, &got, PASS_TMO);
}

/* ---------------------------------------------------------------- the firmware */

LOCAL UW	fw_msg[16] __attribute__((aligned(64)));

LOCAL ER fw_clock( UW tag, UW v1, UW v2, UW *ans )
{
	ER	er;

	knl_memset(fw_msg, 0, sizeof(fw_msg));
	fw_msg[0] = 9 * 4;
	fw_msg[2] = tag;
	fw_msg[3] = 12;
	fw_msg[4] = 12;
	fw_msg[5] = HV_CLOCK;
	fw_msg[6] = v1;
	fw_msg[7] = v2;
	er = knl_mbox_property(fw_msg);
	if ( er < E_OK ) return er;
	if ( ( fw_msg[4] & TAG_DONE ) == 0 ) return E_IO;
	if ( ans != NULL ) *ans = fw_msg[6];
	return E_OK;
}

LOCAL void clock_up( void )
{
	UW	max = 0, now = 0;

	(void)fw_clock(TAG_SET_CLOCK_STATE, 1, 0, NULL);
	if ( fw_clock(TAG_GET_MAX_CLOCK_RATE, 0, 0, &max) >= E_OK && max > 0 ) {
		(void)fw_clock(TAG_SET_CLOCK_RATE, max, 0, NULL);
	}
	(void)fw_clock(TAG_GET_CLOCK_RATE, 0, 0, &now);
	hv.info.clock_hz = now;
}

/* ---------------------------------------------------------------- the command list */

typedef struct {
	UW	*p;			/* pairs: register, value */
	UW	n, max;			/* pairs */
	BOOL	full;
} CMDS;

LOCAL void put( CMDS *c, UW reg, UW val )
{
	if ( c->n >= c->max ) {
		c->full = TRUE;
		return;
	}
	c->p[c->n * 2] = reg;
	c->p[c->n * 2 + 1] = val;
	c->n++;
}

/* The values the context variables start with, in the decoder's order, for each initType */
LOCAL CONST UB ctx_init[3][154] = {
	{
		153, 200, 139, 141, 157, 154, 154, 154, 154, 154, 184, 154, 154,
		154, 184, 63,  154, 154, 154, 154, 154, 154, 154, 154, 154, 154,
		154, 154, 154, 153, 138, 138, 111, 141, 94,  138, 182, 154, 154,
		154, 140, 92,  137, 138, 140, 152, 138, 139, 153, 74,  149, 92,
		139, 107, 122, 152, 140, 179, 166, 182, 140, 227, 122, 197, 110,
		110, 124, 125, 140, 153, 125, 127, 140, 109, 111, 143, 127, 111,
		79,  108, 123, 63,  110, 110, 124, 125, 140, 153, 125, 127, 140,
		109, 111, 143, 127, 111, 79,  108, 123, 63,  91,  171, 134, 141,
		138, 153, 136, 167, 152, 152, 139, 139, 111, 111, 125, 110, 110,
		94,  124, 108, 124, 107, 125, 141, 179, 153, 125, 107, 125, 141,
		179, 153, 125, 107, 125, 141, 179, 153, 125, 140, 139, 182, 182,
		152, 136, 152, 136, 153, 136, 139, 111, 136, 139, 111,
	},
	{
		153, 185, 107, 139, 126, 197, 185, 201, 154, 149, 154, 139, 154,
		154, 154, 152, 110, 122, 95,  79,  63,  31,  31,  153, 153, 168,
		140, 198, 79,  124, 138, 94,  153, 111, 149, 107, 167, 154, 154,
		154, 154, 196, 196, 167, 154, 152, 167, 182, 182, 134, 149, 136,
		153, 121, 136, 137, 169, 194, 166, 167, 154, 167, 137, 182, 125,
		110, 94,  110, 95,  79,  125, 111, 110, 78,  110, 111, 111, 95,
		94,  108, 123, 108, 125, 110, 94,  110, 95,  79,  125, 111, 110,
		78,  110, 111, 111, 95,  94,  108, 123, 108, 121, 140, 61,  154,
		107, 167, 91,  122, 107, 167, 139, 139, 155, 154, 139, 153, 139,
		123, 123, 63,  153, 166, 183, 140, 136, 153, 154, 166, 183, 140,
		136, 153, 154, 166, 183, 140, 136, 153, 154, 170, 153, 123, 123,
		107, 121, 107, 121, 167, 151, 183, 140, 151, 183, 140,
	},
	{
		153, 160, 107, 139, 126, 197, 185, 201, 154, 134, 154, 139, 154,
		154, 183, 152, 154, 137, 95,  79,  63,  31,  31,  153, 153, 168,
		169, 198, 79,  224, 167, 122, 153, 111, 149, 92,  167, 154, 154,
		154, 154, 196, 167, 167, 154, 152, 167, 182, 182, 134, 149, 136,
		153, 121, 136, 122, 169, 208, 166, 167, 154, 152, 167, 182, 125,
		110, 124, 110, 95,  94,  125, 111, 111, 79,  125, 126, 111, 111,
		79,  108, 123, 93,  125, 110, 124, 110, 95,  94,  125, 111, 111,
		79,  125, 126, 111, 111, 79,  108, 123, 93,  121, 140, 61,  154,
		107, 167, 91,  107, 107, 167, 139, 139, 170, 154, 139, 153, 139,
		123, 123, 63,  124, 166, 183, 140, 136, 153, 154, 166, 183, 140,
		136, 153, 154, 166, 183, 140, 136, 153, 154, 170, 153, 138, 138,
		122, 121, 122, 121, 167, 151, 183, 140, 151, 183, 140,
	},
};

/* Where each list's factors go in the decoder's table: [size][matrix] */
LOCAL CONST UH scale_at[4][6] = {
	{ 0x0000, 0x0010, 0x0020, 0x0030, 0x0040, 0x0050 },
	{ 0x0060, 0x00A0, 0x00E0, 0x0120, 0x0160, 0x01A0 },
	{ 0x01E0, 0x02E0, 0x03E0, 0x04E0, 0x05E0, 0x06E0 },
	{ 0x07E0, 0x0BE0, 0, 0, 0, 0 },
};

/* One 8×8 list made n times as large (n 2: 16×16, 4: 32×32), its DC value at the top left */
LOCAL void scale_up( UB *dst, CONST UB *src, UINT n, UB dc )
{
	UINT	y, x, side = 8 * n;

	for ( y = 0; y < side; y++ ) {
		for ( x = 0; x < side; x++ ) {
			dst[y * side + x] = src[( y / n ) * 8 + x / n];
		}
	}
	dst[0] = dc;
}

/* ---------------------------------------------------------------- one picture */

typedef struct {
	CONST T_HEVCJOB		*job;
	CONST T_HEVCSLICE	*sl;		/* the slice being written */
	UW			log2_ctb, ctb;	/* CTB size */
	UW			cw, ch;		/* the picture in CTBs */
	UW			tiles_x, tiles_y;
	UH			col[21], row[23];	/* tiles' first CTB column and row; the last the picture's */
	UH			*ts_of;		/* raster scan address -> tile scan address */
	UH			*rs_of;		/* and back */
	UW			nslice;
	BOOL			temporal_mvp;	/* of the first slice: the picture's */
	INT			col_dpb;	/* the collocated picture, -1 none */
	/* the slice in hand */
	UW			start_ts, sx, sy, px, py;	/* its first CTB, and the previous slice's last */
	UW			qp, n_l0, n_l1, merge;
	BOOL			dependent;
	UW			slicestart;	/* of the last independent slice */
	/* the entry point last set */
	UW			etx, ety, ex, ey, eqp, econst;
	UB			scale[SCALING_BYTES];
	UH			msg[2 * HEVC_MAX_DPB * 8 + 3];
	UW			nmsg;
} PIC;

LOCAL UW tile_of( CONST UH *bd, UW n, UW ctb )
{
	UW	i = 1;

	while ( i < n && ctb >= bd[i] ) i++;
	return i - 1;
}

/* The tile structure and the scan conversion of the picture */
LOCAL ER pic_layout( PIC *p )
{
	CONST T_HEVCSPS	*s = &p->job->sps;
	CONST T_HEVCPPS	*q = &p->job->pps;
	UW		i, ts = 0, ty, tx, y, x;

	p->log2_ctb = (UW)s->log2_min_cb + s->log2_diff_max_min_cb;
	p->ctb = 1U << p->log2_ctb;
	p->cw = ( s->width + p->ctb - 1 ) >> p->log2_ctb;
	p->ch = ( s->height + p->ctb - 1 ) >> p->log2_ctb;
	if ( q->flags & HEVC_PPS_TILES ) {
		p->tiles_x = q->num_tile_columns_minus1 + 1U;
		p->tiles_y = q->num_tile_rows_minus1 + 1U;
		if ( p->tiles_x > 20 || p->tiles_y > 22 ) return E_PAR;
	} else {
		p->tiles_x = p->tiles_y = 1;
	}
	p->col[0] = 0;
	for ( i = 1; i < p->tiles_x; i++ ) p->col[i] = (UH)( p->col[i - 1] + q->column_width_minus1[i - 1] + 1 );
	p->col[p->tiles_x] = (UH)p->cw;
	p->row[0] = 0;
	for ( i = 1; i < p->tiles_y; i++ ) p->row[i] = (UH)( p->row[i - 1] + q->row_height_minus1[i - 1] + 1 );
	p->row[p->tiles_y] = (UH)p->ch;
	for ( i = 0; i < p->tiles_x; i++ ) {
		if ( p->col[i] >= p->col[i + 1] ) return E_PAR;
	}
	for ( i = 0; i < p->tiles_y; i++ ) {
		if ( p->row[i] >= p->row[i + 1] ) return E_PAR;
	}

	p->ts_of = (UH *)Kmalloc(p->cw * p->ch * sizeof(UH) * 2);
	if ( p->ts_of == NULL ) return E_NOMEM;
	p->rs_of = p->ts_of + p->cw * p->ch;
	for ( ty = 0; ty < p->tiles_y; ty++ ) {
		for ( tx = 0; tx < p->tiles_x; tx++ ) {
			for ( y = p->row[ty]; y < p->row[ty + 1]; y++ ) {
				for ( x = p->col[tx]; x < p->col[tx + 1]; x++ ) {
					UW rs = y * p->cw + x;

					p->ts_of[rs] = (UH)ts;
					p->rs_of[ts] = (UH)rs;
					ts++;
				}
			}
		}
	}
	return E_OK;
}

/* The context variables' first values for the slice, then saved for the entry points after */
LOCAL void put_ctx_init( CMDS *c, PIC *p )
{
	CONST T_HEVCSLICE	*sl = p->sl;
	UINT			t, i;
	INT			q = (INT)p->qp;
	UB			v[156];

	t = ( ( sl->flags & HEVC_SL_CABAC_INIT ) && sl->type != HEVC_SLICE_I ) ? sl->type + 1U : 2U - sl->type;
	if ( q < 0 ) q = 0;
	if ( q > 51 ) q = 51;
	for ( i = 0; i < 154; i++ ) {
		INT	iv = ctx_init[t][i];
		INT	slope = ( iv >> 4 ) * 5 - 45;
		INT	offset = ( ( iv & 15 ) << 3 ) - 16;
		INT	state = 2 * ( ( ( slope * q ) >> 4 ) + offset ) - 127;

		if ( state < 0 ) state = -state - 1;	/* the state with its MPS bit at the bottom */
		if ( state > 124 ) state = 124 + ( state & 1 );
		v[i] = (UB)state;
	}
	v[154] = v[155] = 0;
	for ( i = 0; i < 156; i += 4 ) {
		put(c, A_PROB + i, (UW)v[i] | ( (UW)v[i + 1] << 8 ) | ( (UW)v[i + 2] << 16 ) | ( (UW)v[i + 3] << 24 ));
	}
	put(c, R_TRANSFER, CTX_SAVE);
}

/* Whether no reference of a list comes after the picture */
LOCAL BOOL all_before( CONST PIC *p, CONST UB *refs, UW n )
{
	UW	i;

	for ( i = 0; i < n; i++ ) {
		if ( p->job->dpb[refs[i]].poc > p->sl->poc ) return FALSE;
	}
	return TRUE;
}

/* The slice's messages: its kind, its references with their weights, its filters */
LOCAL void make_msgs( PIC *p )
{
	CONST T_HEVCSLICE	*sl = p->sl;
	CONST T_HEVCPPS		*q = &p->job->pps;
	UW			kind, l, i;
	BOOL			from_l0, wp;

	p->nmsg = 0;
	kind = ( sl->type == HEVC_SLICE_I ) ? 1 : ( sl->type == HEVC_SLICE_P ) ? 2 : 3;
	kind |= ( p->n_l0 << 2 ) | ( p->n_l1 << 6 ) | ( p->merge << 11 );
	from_l0 = !p->temporal_mvp || sl->type != HEVC_SLICE_B || ( sl->flags & HEVC_SL_COLLOCATED_FROM_L0 );
	kind |= (UW)from_l0 << 14;
	if ( sl->type == HEVC_SLICE_I ) {
		p->msg[p->nmsg++] = (UH)kind;
	} else {
		if ( all_before(p, sl->ref_l0, p->n_l0) && all_before(p, sl->ref_l1, p->n_l1) ) kind |= 1U << 10;
		p->msg[p->nmsg++] = (UH)kind;
		if ( p->temporal_mvp ) {
			p->col_dpb = ( from_l0 ? sl->ref_l0 : sl->ref_l1 )[sl->collocated_ref_idx];
		}
		wp = ( sl->type == HEVC_SLICE_P ) ? ( q->flags & HEVC_PPS_WEIGHTED_PRED ) != 0
						  : ( q->flags & HEVC_PPS_WEIGHTED_BIPRED ) != 0;
		for ( l = 0; l < 2; l++ ) {
			CONST UB	*refs = l ? sl->ref_l1 : sl->ref_l0;
			UW		n = l ? p->n_l1 : p->n_l0;

			for ( i = 0; i < n; i++ ) {
				UW	d = refs[i];
				UW	ld = sl->luma_log2_weight_denom;
				UW	cd = ld + (UW)sl->delta_chroma_log2_weight_denom;

				p->msg[p->nmsg++] = (UH)( d | ( ( p->job->dpb[d].flags & HEVC_DPB_LONG_TERM ) ? 0x10 : 0 )
							  | ( wp ? 0x60 : 0 ) );
				p->msg[p->nmsg++] = (UH)( p->job->dpb[d].poc & 0xFFFF );
				if ( wp ) {
					p->msg[p->nmsg++] = (UH)( ld | ( ( ( sl->delta_luma_weight[l][i] + ( 1 << ld ) ) & 0x1FF ) << 3 ) );
					p->msg[p->nmsg++] = (UH)( sl->luma_offset[l][i] & 0xFF );
					p->msg[p->nmsg++] = (UH)( cd | ( ( ( sl->delta_chroma_weight[l][i][0] + ( 1 << cd ) ) & 0x1FF ) << 3 ) );
					p->msg[p->nmsg++] = (UH)( sl->chroma_offset[l][i][0] & 0xFF );
					p->msg[p->nmsg++] = (UH)( cd | ( ( ( sl->delta_chroma_weight[l][i][1] + ( 1 << cd ) ) & 0x1FF ) << 3 ) );
					p->msg[p->nmsg++] = (UH)( sl->chroma_offset[l][i][1] & 0xFF );
				}
			}
		}
	}
	p->msg[p->nmsg++] = (UH)( ( sl->beta_offset_div2 & 15 ) | ( ( sl->tc_offset_div2 & 15 ) << 4 )
				  | ( ( sl->flags & HEVC_SL_DEBLOCKING_OFF ) ? 0x100 : 0 )
				  | ( ( sl->flags & HEVC_SL_LOOP_FILTER_SLICES ) ? 0x200 : 0 )
				  | ( ( q->flags & HEVC_PPS_LOOP_FILTER_TILES ) ? 0x400 : 0 ) );
	p->msg[p->nmsg++] = (UH)( ( ( sl->cr_qp_offset & 31 ) << 5 ) | ( sl->cb_qp_offset & 31 ) );
}

/* What every entry point of the slice shares in the slice register */
LOCAL UW slice_const( CONST PIC *p )
{
	CONST T_HEVCSLICE	*sl = p->sl;
	UW			v = p->merge | ( p->n_l0 << 4 ) | ( p->n_l1 << 8 ) | ( (UW)sl->type << 12 );

	if ( sl->flags & HEVC_SL_SAO_LUMA ) v |= 1U << 14;
	if ( sl->flags & HEVC_SL_SAO_CHROMA ) v |= 1U << 15;
	if ( sl->type == HEVC_SLICE_B && ( sl->flags & HEVC_SL_MVD_L1_ZERO ) ) v |= 1U << 16;
	return v;
}

/* The slice's bytes after its header: where they are, how many, and the stream told to stop and start */
LOCAL void put_bits( CMDS *c, PIC *p )
{
	UD	pa = hv.bits.pa + p->sl->data_offset + p->sl->header_bytes;
	UW	len = p->sl->data_size - p->sl->header_bytes;
	UW	off = (UW)( pa & 63 );

	put(c, R_BFBASE, A64(pa));
	put(c, R_BFNUM, len);
	put(c, R_BFCONTROL, off | 0x80);		/* stop */
	put(c, R_BFCONTROL, off | 0x40);		/* start, the emulation prevention bytes taken out */
}

/* The sequence's and picture's registers, the scaling factors, where the slice starts */
LOCAL void put_params( CMDS *c, PIC *p )
{
	CONST T_HEVCSPS	*s = &p->job->sps;
	CONST T_HEVCPPS	*q = &p->job->pps;
	CONST T_HEVCSLICE *sl = p->sl;
	UW		i;

	put(c, R_SPS0, (UW)s->log2_min_cb | ( p->log2_ctb << 4 ) | ( (UW)s->log2_min_tb << 8 )
		       | ( ( (UW)s->log2_min_tb + s->log2_diff_max_min_tb ) << 12 )
		       | ( (UW)s->bit_depth_luma << 16 ) | ( (UW)s->bit_depth_chroma << 20 )
		       | ( (UW)s->max_th_depth_intra << 24 ) | ( (UW)s->max_th_depth_inter << 28 ));
	put(c, R_SPS1, (UW)s->pcm_bit_depth_luma | ( (UW)s->pcm_bit_depth_chroma << 4 )
		       | ( (UW)s->log2_min_pcm_cb << 8 ) | ( ( (UW)s->log2_min_pcm_cb + s->log2_diff_max_min_pcm_cb ) << 12 )
		       | ( ( ( s->flags & HEVC_SPS_SEPARATE_COLOUR_PLANE ) ? 0U : s->chroma_format_idc ) << 16 )
		       | ( ( s->flags & HEVC_SPS_AMP ) ? 1U << 18 : 0 )
		       | ( ( s->flags & HEVC_SPS_PCM ) ? 1U << 19 : 0 )
		       | ( ( s->flags & HEVC_SPS_SCALING_LIST ) ? 1U << 20 : 0 )
		       | ( ( s->flags & HEVC_SPS_STRONG_INTRA_SMOOTHING ) ? 1U << 21 : 0 ));
	put(c, R_PPS, ( p->log2_ctb - q->diff_cu_qp_delta_depth )
		      | ( ( q->flags & HEVC_PPS_CU_QP_DELTA ) ? 1U << 4 : 0 )
		      | ( ( q->flags & HEVC_PPS_TRANSQUANT_BYPASS ) ? 1U << 5 : 0 )
		      | ( ( q->flags & HEVC_PPS_TRANSFORM_SKIP ) ? 1U << 6 : 0 )
		      | ( ( q->flags & HEVC_PPS_SIGN_DATA_HIDING ) ? 1U << 7 : 0 )
		      | ( ( (UW)( q->cb_qp_offset + sl->cb_qp_offset ) & 0xFF ) << 8 )
		      | ( ( (UW)( q->cr_qp_offset + sl->cr_qp_offset ) & 0xFF ) << 16 )
		      | ( ( q->flags & HEVC_PPS_CONSTRAINED_INTRA ) ? 1U << 24 : 0 ));
	if ( p->start_ts == 0 && ( s->flags & HEVC_SPS_SCALING_LIST ) ) {
		for ( i = 0; i < SCALING_BYTES; i += 4 ) {
			put(c, A_SCALING + i, (UW)p->scale[i] | ( (UW)p->scale[i + 1] << 8 )
					      | ( (UW)p->scale[i + 2] << 16 ) | ( (UW)p->scale[i + 3] << 24 ));
		}
	}
	if ( !p->dependent ) p->slicestart = ( sl->segment_addr % p->cw ) | ( ( sl->segment_addr / p->cw ) << 16 );
	put(c, R_SLICESTART, p->slicestart);
}

LOCAL void put_msgs( CMDS *c, PIC *p, UW slice_no )
{
	UW	i;

	put(c, R_SLICECMDS, p->nmsg | ( slice_no << 8 ));
	for ( i = 0; i < p->nmsg; i++ ) put(c, A_SLICEMSG + 4 * i, p->msg[i]);
}

#define MODE_TILE	0xFFFF
#define MODE_WPP	1

/* Where the decoder starts reading again: a slice, a tile or a row of the wavefronts */
LOCAL void put_entry( CMDS *c, PIC *p, BOOL begin, BOOL qp, UW mode, UW tx, UW ty, UW x, UW y, UW sqp, UW sconst )
{
	UW	lx = p->col[tx + 1] - 1U;
	UW	ly = ( mode == MODE_WPP ) ? y : p->row[ty + 1] - 1U;
	UW	wl = p->job->sps.width & ( p->ctb - 1 ), hl = p->job->sps.height & ( p->ctb - 1 );
	UW	w = ( lx + 1 < p->cw || wl == 0 ) ? p->ctb : wl;
	UW	h = ( ly + 1 < p->ch || hl == 0 ) ? p->ctb : hl;

	put(c, R_TILESTART, p->col[tx] | ( (UW)p->row[ty] << 16 ));
	put(c, R_TILEEND, lx | ( ly << 16 ));
	if ( begin ) put(c, R_BEGINTILEEND, lx | ( ly << 16 ));
	put(c, R_SLICE, sconst | ( w << 17 ) | ( h << 24 ));
	if ( qp ) put(c, R_QP, 6U * ( p->job->sps.bit_depth_luma - 8U ) + sqp);
	put(c, R_MODE, mode | ( ( lx == p->cw - 1 ) ? 1U << 17 : 0 ) | ( ( ly == p->ch - 1 ) ? 1U << 18 : 0 ));
	put(c, R_CONTROL, x | ( y << 16 ));
	p->etx = tx;
	p->ety = ty;
	p->ex = x;
	p->ey = y;
	p->eqp = sqp;
	p->econst = sconst;
}

/* The end of a CTB the decoder is to stop at: a slice's (1) or a tile's or row's (2) */
#define END_SLICE	1
#define END_PART	2

LOCAL void put_end( CMDS *c, UW how, UW x, UW y )
{
	put(c, R_STATUS, how | ( x << 5 ) | ( y << 18 ));
}

/* Wavefronts: the row's contexts kept after its second CTB for the row below */
LOCAL void wpp_hold( CMDS *c, PIC *p, UW y )
{
	put(c, R_STATUS, ( y << 18 ) | 0x25);
	put(c, R_TRANSFER, CTX_SAVE);
	put(c, R_MODE, ( y == p->ch - 1 ) ? 0x70000 : 0x30000);
	put(c, R_CONTROL, ( y << 16 ) + 2);
}

/* Wavefronts: the rows of the slice in hand up to 'last' started one after another */
LOCAL void wpp_rows( CMDS *c, PIC *p, UW last )
{
	while ( p->ey < last ) {
		if ( p->cw > 2 ) wpp_hold(c, p, p->ey);
		put_end(c, END_PART, p->cw - 1, p->ey);
		put(c, R_TRANSFER, ( p->cw == 2 ) ? CTX_SAVE : CTX_RESTORE);
		put_entry(c, p, FALSE, TRUE, MODE_WPP, 0, 0, 0, p->ey + 1, p->eqp, p->econst);
	}
}

/* Tiles: the tiles of the slice in hand up to (lx, ly) started one after another */
LOCAL void tile_rest( CMDS *c, PIC *p, UW ltx, UW lty )
{
	while ( p->ety < lty || ( p->ety == lty && p->etx < ltx ) ) {
		UW	tx = p->etx, ty = p->ety;

		put_end(c, END_PART, p->col[tx + 1] - 1U, p->row[ty + 1] - 1U);
		put(c, R_TRANSFER, CTX_RESTORE);
		if ( ++tx >= p->tiles_x ) {
			tx = 0;
			ty++;
		}
		put_entry(c, p, FALSE, TRUE, MODE_TILE, tx, ty, p->col[tx], p->row[ty], p->eqp, p->econst);
	}
}

/* One slice segment into the command list */
LOCAL void put_slice( CMDS *c, PIC *p, UW no, BOOL last )
{
	BOOL	wpp = ( p->job->pps.flags & HEVC_PPS_ENTROPY_SYNC ) != 0;
	BOOL	qp = TRUE;
	UW	tx = tile_of(p->col, p->tiles_x, p->sx), ty = tile_of(p->row, p->tiles_y, p->sy);

	if ( p->start_ts > 0 ) {
		if ( wpp ) {
			wpp_rows(c, p, p->py);
			if ( p->ex < 2 && ( p->ey < p->sy || p->sx > 2 ) && p->cw > 2 ) wpp_hold(c, p, p->py);
			put_end(c, END_SLICE, p->px, p->py);
			if ( p->sx == 2 || ( p->cw == 2 && p->ey < p->sy ) ) put(c, R_TRANSFER, CTX_SAVE);
		} else {
			tile_rest(c, p, tile_of(p->col, p->tiles_x, p->px), tile_of(p->row, p->tiles_y, p->py));
			put_end(c, END_SLICE, p->px, p->py);
		}
	}
	make_msgs(p);
	put_bits(c, p);
	if ( wpp ) {
		if ( p->start_ts == 0 || !p->dependent || p->cw == 1 ) put_ctx_init(c, p);
		else if ( p->sx == 0 ) put(c, R_TRANSFER, CTX_RESTORE);
		else qp = FALSE;
	} else {
		qp = p->start_ts == 0 || !p->dependent
		     || tx != tile_of(p->col, p->tiles_x, p->px) || ty != tile_of(p->row, p->tiles_y, p->py);
		if ( qp ) put_ctx_init(c, p);
	}
	put_msgs(c, p, no);
	put_params(c, p);
	if ( wpp ) put_entry(c, p, !p->dependent, qp, MODE_WPP, 0, 0, p->sx, p->sy, p->qp, slice_const(p));
	else put_entry(c, p, !p->dependent, qp, MODE_TILE, tx, ty, p->sx, p->sy, p->qp, slice_const(p));
	if ( last ) {
		if ( wpp ) {
			wpp_rows(c, p, p->ch - 1);
			if ( p->ex < 2 && p->cw > 2 ) wpp_hold(c, p, p->ch - 1);
		} else {
			tile_rest(c, p, p->tiles_x - 1, p->tiles_y - 1);
		}
		put_end(c, END_SLICE, p->cw - 1, p->ch - 1);
	}
}

/* The picture's scaling factors in the decoder's table */
LOCAL void make_scale( PIC *p )
{
	CONST T_HEVCSCALE	*sc = &p->job->scale;
	UINT			m;

	for ( m = 0; m < 6; m++ ) {
		knl_memcpy(p->scale + scale_at[0][m], sc->s4x4[m], 16);
		knl_memcpy(p->scale + scale_at[1][m], sc->s8x8[m], 64);
		scale_up(p->scale + scale_at[2][m], sc->s16x16[m], 2, sc->dc16[m]);
	}
	for ( m = 0; m < 2; m++ ) scale_up(p->scale + scale_at[3][m], sc->s32x32[m], 4, sc->dc32[m]);
}

/* Whether a NAL unit type may be referred to (Table 7-1: not 0, 2, .. 14, the sub-layer non-references) */
LOCAL BOOL ref_nal( UW t )
{
	return ( t & ~0xEU ) != 0;
}

/* Pass 2's configuration word */
LOCAL UW config2( CONST PIC *p, BOOL mvs )
{
	CONST T_HEVCSPS	*s = &p->job->sps;
	CONST T_HEVCPPS	*q = &p->job->pps;
	UW		v = (UW)s->bit_depth_luma | ( (UW)s->bit_depth_chroma << 4 );

	if ( s->bit_depth_luma != 8 ) v |= 1U << 8;
	if ( s->bit_depth_chroma != 8 ) v |= 1U << 9;
	v |= p->log2_ctb << 10;
	if ( q->flags & HEVC_PPS_CONSTRAINED_INTRA ) v |= 1U << 13;
	if ( s->flags & HEVC_SPS_STRONG_INTRA_SMOOTHING ) v |= 1U << 14;
	if ( mvs ) v |= 1U << 15;
	v |= ( q->log2_parallel_merge_level_minus2 + 2U ) << 16;
	if ( p->temporal_mvp ) v |= 1U << 19;
	if ( s->flags & HEVC_SPS_PCM_LOOP_FILTER_OFF ) v |= 1U << 20;
	v |= ( (UW)q->cb_qp_offset & 31 ) << 21;
	v |= ( (UW)q->cr_qp_offset & 31 ) << 26;
	return v;
}

/* Pass 1: the command list run, its memories made larger as it asks */
LOCAL ER pass1( CONST PIC *p, UW ncmd )
{
	UW	tries, st;
	ER	er;

	for ( tries = 0; tries < 6; tries++ ) {
		UW	pus = (UW)( ( hv.pu.size / p->ch ) & ~63ULL );
		UW	cos = (UW)( ( hv.coeff.size / p->ch ) & ~63ULL );

		(void)tk_clr_flg(hv.flg, ~1U);
		out_w(REG(R_PUWBASE), A64(hv.pu.pa));
		out_w(REG(R_PUWSTRIDE), L64(pus));
		out_w(REG(R_COEFFWBASE), A64(hv.coeff.pa));
		out_w(REG(R_COEFFWSTRIDE), L64(cos));
		out_w(REG(R_CFNUM), ncmd);
		DeviceBarrier();
		out_w(REG(R_CFBASE), A64(hv.cmd.pa));
		er = pass_wait(1);
		if ( er < E_OK ) {
			hv.info.last_error = 0x10000 | in_w(REG(R_STATUS));
			return E_TMOUT;
		}
		if ( in_w(REG(R_CFSTATUS)) == in_w(REG(R_CFNUM)) ) return E_OK;
		st = in_w(REG(R_STATUS));
		if ( ( st & ( ST_PU_FULL | ST_COEFF_FULL ) ) == 0 ) {
			hv.info.last_error = st;
			return E_IO;
		}
		/* twice as large, as far as one piece of memory goes */
		if ( st & ST_PU_FULL ) {
			UD n = hv.pu.size * 2;

			mem_put(&hv.pu);
			if ( mem_get(&hv.pu, n) < E_OK && mem_get(&hv.pu, n / 2) < E_OK ) return E_NOMEM;
		}
		if ( st & ST_COEFF_FULL ) {
			UD n = hv.coeff.size * 2;

			mem_put(&hv.coeff);
			if ( mem_get(&hv.coeff, n) < E_OK && mem_get(&hv.coeff, n / 2) < E_OK ) return E_NOMEM;
		}
	}
	hv.info.last_error = 0x20000;
	return E_IO;
}

/* Pass 2: the picture reconstructed into its slot */
LOCAL ER pass2( CONST PIC *p )
{
	CONST T_HEVCJOB	*j = p->job;
	CONST SLOT	*out = &hv.slot[j->out_slot];
	UW		pus = (UW)( ( hv.pu.size / p->ch ) & ~63ULL );
	UW		cos = (UW)( ( hv.coeff.size / p->ch ) & ~63ULL );
	BOOL		use_mv = ( j->sps.flags & HEVC_SPS_TEMPORAL_MVP ) != 0;
	BOOL		mk_mv = use_mv && ( j->sps.max_sub_layers_minus1 >= p->sl->temporal_id_plus1
					    || ref_nal(p->sl->nal_unit_type) );
	UW		i;

	(void)tk_clr_flg(hv.flg, ~2U);
	out_w(REG(R_PURBASE), A64(hv.pu.pa));
	out_w(REG(R_PURSTRIDE), L64(pus));
	out_w(REG(R_COEFFRBASE), A64(hv.coeff.pa));
	out_w(REG(R_COEFFRSTRIDE), L64(cos));
	out_w(REG(R_OUTYBASE), A64(out->luma.pa));
	out_w(REG(R_OUTCBASE), A64(out->chroma.pa));
	out_w(REG(R_OUTYSTRIDE), L64(hv.ystride));
	out_w(REG(R_OUTCSTRIDE), L64(hv.cstride));
	for ( i = 0; i < HEVC_MAX_DPB; i++ ) {
		CONST SLOT *r = ( i < j->ndpb ) ? &hv.slot[j->dpb[i].slot] : out;

		out_w(REG(R_REFS + 16 * i), A64(r->luma.pa));
		out_w(REG(R_REFS + 16 * i + 4), L64(hv.ystride));
		out_w(REG(R_REFS + 16 * i + 8), A64(r->chroma.pa));
		out_w(REG(R_REFS + 16 * i + 12), L64(hv.cstride));
	}
	out_w(REG(R_CONFIG2), config2(p, mk_mv));
	out_w(REG(R_FRAMESIZE), ( (UW)j->sps.height << 16 ) | j->sps.width);
	out_w(REG(R_CURRPOC), (UW)p->sl->poc);
	out_w(REG(R_COLSTRIDE), L64(hv.mvstride));
	out_w(REG(R_MVSTRIDE), L64(hv.mvstride));
	out_w(REG(R_MVBASE), mk_mv ? A64(out->mv.pa) : 0);
	out_w(REG(R_COLBASE), ( use_mv && p->col_dpb >= 0 && (UW)p->col_dpb < j->ndpb )
			      ? A64(hv.slot[j->dpb[p->col_dpb].slot].mv.pa) : 0);
	DeviceBarrier();
	out_w(REG(R_NUMROWS), p->ch);
	if ( pass_wait(2) < E_OK ) {
		hv.info.last_error = 0x30000;
		return E_TMOUT;
	}
	return E_OK;
}

/* A job checked against the slots and the decoder */
LOCAL ER job_check( CONST T_HEVCJOB *j, SZ size )
{
	CONST T_HEVCSLICE	*sl = (CONST T_HEVCSLICE *)( j + 1 );
	UW			i, k;
	SZ			head;

	if ( size < (SZ)sizeof(*j) || j->magic != HEVC_JOB_MAGIC || j->size != (UW)size ) return E_PAR;
	if ( j->nslice == 0 || j->nslice > HEVC_MAX_SLICES || j->ndpb > HEVC_MAX_DPB ) return E_PAR;
	head = (SZ)sizeof(*j) + (SZ)j->nslice * (SZ)sizeof(T_HEVCSLICE);
	if ( size < head || size - head > (SZ)hv.bits.size ) return E_PAR;
	if ( hv.conf.nslots == 0 || j->out_slot >= hv.conf.nslots ) return E_PAR;
	if ( j->sps.width != hv.conf.width || j->sps.height != hv.conf.height ) return E_PAR;
	if ( j->sps.chroma_format_idc != 1 || j->sps.bit_depth_luma != hv.conf.bitdepth
	     || j->sps.bit_depth_chroma != hv.conf.bitdepth ) return E_PAR;
	if ( ( j->pps.flags & HEVC_PPS_TILES ) && ( j->pps.flags & HEVC_PPS_ENTROPY_SYNC ) ) return E_PAR;
	if ( (UW)j->sps.log2_min_cb + j->sps.log2_diff_max_min_cb < 4
	     || (UW)j->sps.log2_min_cb + j->sps.log2_diff_max_min_cb > 6 ) return E_PAR;
	for ( i = 0; i < j->ndpb; i++ ) {
		if ( j->dpb[i].slot >= hv.conf.nslots ) return E_PAR;
	}
	for ( i = 0; i < j->nslice; i++ ) {
		if ( sl[i].data_offset + (SZ)sl[i].data_size > size - head ) return E_PAR;
		if ( sl[i].header_bytes >= sl[i].data_size ) return E_PAR;
		if ( sl[i].type > HEVC_SLICE_I ) return E_PAR;
		for ( k = 0; k <= sl[i].num_ref_idx_l0_minus1 && k < HEVC_MAX_DPB; k++ ) {
			if ( sl[i].type != HEVC_SLICE_I && sl[i].ref_l0[k] >= j->ndpb ) return E_PAR;
		}
		for ( k = 0; k <= sl[i].num_ref_idx_l1_minus1 && k < HEVC_MAX_DPB; k++ ) {
			if ( sl[i].type == HEVC_SLICE_B && sl[i].ref_l1[k] >= j->ndpb ) return E_PAR;
		}
	}
	return E_OK;
}

LOCAL ER decode( CONST T_HEVCJOB *j, SZ size )
{
	CONST T_HEVCSLICE	*sl = (CONST T_HEVCSLICE *)( j + 1 );
	CONST UB		*bytes = (CONST UB *)( sl + j->nslice );
	SZ			nbytes = size - (SZ)sizeof(*j) - (SZ)j->nslice * (SZ)sizeof(T_HEVCSLICE);
	PIC			*p;
	CMDS			c;
	UW			i;
	SYSTIM_U		t0, t1;
	ER			er;

	er = job_check(j, size);
	if ( er < E_OK ) return er;
	p = (PIC *)Kmalloc(sizeof(PIC));
	if ( p == NULL ) return E_NOMEM;
	knl_memset(p, 0, sizeof(*p));
	p->job = j;
	p->col_dpb = -1;
	er = pic_layout(p);
	if ( er < E_OK ) goto out;
	for ( i = 0; i < j->nslice; i++ ) {
		if ( sl[i].segment_addr >= p->cw * p->ch ) {
			er = E_PAR;
			goto out;
		}
	}
	p->temporal_mvp = ( sl[0].flags & HEVC_SL_TEMPORAL_MVP ) != 0;
	if ( j->sps.flags & HEVC_SPS_SCALING_LIST ) make_scale(p);

	knl_memcpy(hv.bits.va, bytes, (INT)nbytes);
	cache_flush(hv.bits.va, (UD)nbytes);

	c.p = (UW *)hv.cmd.va;
	c.max = (UW)( hv.cmd.size / 8 );
	c.n = 0;
	c.full = FALSE;
	for ( i = 0; i < j->nslice; i++ ) {
		UW	prev;

		p->sl = &sl[i];
		p->qp = (UW)( 26 + j->pps.init_qp_minus26 + sl[i].qp_delta );
		p->merge = ( sl[i].type == HEVC_SLICE_I ) ? 0 : 5U - sl[i].five_minus_max_merge_cand;
		p->dependent = ( sl[i].flags & HEVC_SL_DEPENDENT ) != 0;
		p->n_l0 = ( sl[i].type == HEVC_SLICE_I ) ? 0 : sl[i].num_ref_idx_l0_minus1 + 1U;
		p->n_l1 = ( sl[i].type != HEVC_SLICE_B ) ? 0 : sl[i].num_ref_idx_l1_minus1 + 1U;
		p->start_ts = p->ts_of[sl[i].segment_addr];
		p->sx = sl[i].segment_addr % p->cw;
		p->sy = sl[i].segment_addr / p->cw;
		prev = ( p->start_ts == 0 ) ? 0 : p->rs_of[p->start_ts - 1];
		p->px = prev % p->cw;
		p->py = prev / p->cw;
		if ( i == 0 && p->start_ts != 0 ) {
			er = E_PAR;			/* a picture starts at its first CTB */
			goto out;
		}
		put_slice(&c, p, i, i + 1 == j->nslice);
	}
	if ( c.full ) {
		er = E_LIMIT;
		goto out;
	}
	cache_flush(hv.cmd.va, (UD)c.n * 8);

	tk_get_otm_u(&t0, NULL);
	er = pass1(p, c.n);
	if ( er >= E_OK ) er = pass2(p);
	tk_get_otm_u(&t1, NULL);
	if ( er >= E_OK ) {
		CONST SLOT *o = &hv.slot[j->out_slot];

		cache_flush(o->luma.va, o->luma.size);
		cache_flush(o->chroma.va, o->chroma.size);
		hv.info.decoded++;
		hv.info.last_us = (UW)( t1 - t0 );
	} else {
		hv.info.errors++;
	}
out:
	if ( p->ts_of != NULL ) Kfree(p->ts_of);
	Kfree(p);
	return er;
}

/* ---------------------------------------------------------------- the slots */

LOCAL void slots_free( void )
{
	UW	i;

	for ( i = 0; i < HEVC_MAX_SLOTS; i++ ) {
		mem_put(&hv.slot[i].luma);
		mem_put(&hv.slot[i].chroma);
		mem_put(&hv.slot[i].mv);
	}
	knl_memset(&hv.conf, 0, sizeof(hv.conf));
}

LOCAL ER conf_set( CONST T_HEVCCONF *c )
{
	UW	cols, h8, i;
	ER	er = E_OK;

	if ( c->width < 32 || c->height < 32 || c->width > HEVC_MAX_WIDTH || c->height > HEVC_MAX_HEIGHT
	     || ( c->bitdepth != 8 && c->bitdepth != 10 ) || c->nslots == 0 || c->nslots > HEVC_MAX_SLOTS ) return E_PAR;
	slots_free();
	hv.colw = ( c->bitdepth == 10 ) ? 96 : 128;
	cols = ( c->width + hv.colw - 1 ) / hv.colw;
	h8 = ( c->height + 7 ) & ~7U;
	hv.ystride = h8 * 128;
	hv.cstride = hv.ystride / 2;
	hv.mvstride = ( c->width + 63 ) & ~63U;
	for ( i = 0; i < c->nslots && er >= E_OK; i++ ) {
		er = mem_get(&hv.slot[i].luma, (UD)cols * hv.ystride);
		if ( er >= E_OK ) er = mem_get(&hv.slot[i].chroma, (UD)cols * hv.cstride);
		if ( er >= E_OK ) er = mem_get(&hv.slot[i].mv, (UD)hv.mvstride * ( ( ( c->height + 63 ) & ~63U ) >> 4 ));
	}
	if ( er < E_OK ) {
		slots_free();
		return er;
	}
	/* the pass 1 memories as the picture's size asks first; larger when pass 1 says */
	mem_put(&hv.pu);
	mem_put(&hv.coeff);
	er = mem_get(&hv.pu, (UD)c->width * c->height / 4);
	if ( er >= E_OK ) er = mem_get(&hv.coeff, (UD)c->width * c->height);
	if ( er < E_OK ) {
		slots_free();
		return er;
	}
	hv.conf = *c;
	return E_OK;
}

/*
 * A slot's 10-bit picture as P010 in rows, from byte 'off' of it: each
 * run of samples to the end of a column or of the row is unpacked from
 * its words into 16 bits, the sample's bits at the top
 */
LOCAL SZ picture10( CONST SLOT *s, UD off, UB *d, SZ size )
{
	UW	w = hv.conf.width, h = hv.conf.height;
	UD	ysamp = (UD)w * h, all = ( ysamp + ysamp / 2 ) * 2, at;
	UH	run[96];
	SZ	n = 0;

	for ( at = off; at < all && n < size; ) {
		UD	k = at / 2;
		UW	x, y, i, p, cnt, skip, len;
		CONST UW *col;

		if ( k < ysamp ) {
			y = (UW)( k / w );
			x = (UW)( k % w );
			col = (CONST UW *)( s->luma.va + (UD)( x / 96 ) * hv.ystride + (UD)y * 128 );
		} else {
			UD c = k - ysamp;

			y = (UW)( c / w );
			x = (UW)( c % w );
			col = (CONST UW *)( s->chroma.va + (UD)( x / 96 ) * hv.cstride + (UD)y * 128 );
		}
		p = x % 96;
		cnt = 96 - p;
		if ( cnt > w - x ) cnt = w - x;
		for ( i = 0; i < cnt; ) {
			UW	word = col[p / 3], sh;

			for ( sh = p % 3; sh < 3 && i < cnt; sh++, i++, p++ ) {
				run[i] = (UH)( ( ( word >> ( 10 * sh ) ) & 0x3FFU ) << 6 );
			}
		}
		skip = (UW)( at - k * 2 );		/* 1 when the offset is the upper byte of a sample */
		len = cnt * 2 - skip;
		if ( (SZ)len > size - n ) len = (UW)( size - n );
		knl_memcpy(d + n, (UB *)run + skip, (INT)len);
		n += len;
		at += len;
	}
	return n;
}

/* A slot's picture in rows, NV12 or P010, from byte 'off' of it */
LOCAL ER picture( UW slot, D off, void *buf, SZ size, SZ *p_asize )
{
	UW	w = hv.conf.width, h = hv.conf.height;
	UD	ysize = (UD)w * h, all = ysize + ysize / 2, at;
	UB	*d = (UB *)buf;
	CONST SLOT *s;
	SZ	n = 0;

	if ( slot >= hv.conf.nslots ) return E_PAR;
	s = &hv.slot[slot];
	if ( off < 0 ) return E_PAR;
	if ( hv.conf.bitdepth == 10 ) {
		n = picture10(s, (UD)off, d, size);
		if ( p_asize != NULL ) *p_asize = n;
		return E_OK;
	}
	for ( at = (UD)off; at < all && n < size; ) {
		UW	x, y, run;
		CONST UB *src;

		if ( at < ysize ) {
			y = (UW)( at / w );
			x = (UW)( at % w );
			src = s->luma.va + (UD)( x / 128 ) * hv.ystride + (UD)y * 128 + ( x % 128 );
		} else {
			UD c = at - ysize;

			y = (UW)( c / w );
			x = (UW)( c % w );
			src = s->chroma.va + (UD)( x / 128 ) * hv.cstride + (UD)y * 128 + ( x % 128 );
		}
		/* to the end of the column, of the row, of what is asked */
		run = 128 - ( x % 128 );
		if ( run > w - x ) run = w - x;
		if ( (SZ)run > size - n ) run = (UW)( size - n );
		knl_memcpy(d + n, src, (INT)run);
		n += run;
		at += run;
	}
	if ( p_asize != NULL ) *p_asize = n;
	return E_OK;
}

/* ---------------------------------------------------------------- the object */

LOCAL INT ob_text( void *ctx, UB *t, INT n, INT max )
{
	(void)ctx;
	n = knl_oj_put(t, n, max, "<p>The HEVC decoder of the BCM2712. Record 3: the pictures' size and the slots "
			  "(T_HEVCCONF); record 1: one picture's job (T_HEVCJOB, its slices, their bytes), "
			  "decoded as it is written; record 2: a slot's picture as NV12, or P010 for 10 bits (the slot in the "
			  "upper 32 bits of the offset); record 4: T_HEVCINFO (include/ts/hevc.h).</p>");
	return n;
}

LOCAL INT ob_attr( void *ctx, UB *j, INT n, INT max )
{
	(void)ctx;
	n = knl_oj_put(j, n, max, ",\"virtual\":false,\"chip\":\"BCM2712\",\"codec\":\"hevc\",\"decoded\":");
	return knl_oj_put_num(j, n, max, (INT)hv.info.decoded);
}

LOCAL ER ob_rea( void *ctx, UD *pos, INT recno, D off, void *buf, SZ size, SZ *p_asize )
{
	ER	er;

	(void)ctx;
	(void)pos;
	tk_loc_mtx(hv.mtx, TMO_FEVR);
	switch ( recno ) {
	case HEVC_REC_PICTURE:
		er = picture((UW)( (UD)off >> 32 ), (D)( (UD)off & 0xFFFFFFFFULL ), buf, size, p_asize);
		break;
	case HEVC_REC_CONF:
		er = ( size >= (SZ)sizeof(hv.conf) ) ? E_OK : E_PAR;
		if ( er >= E_OK ) {
			knl_memcpy(buf, &hv.conf, sizeof(hv.conf));
			if ( p_asize != NULL ) *p_asize = sizeof(hv.conf);
		}
		break;
	case HEVC_REC_INFO:
		er = ( size >= (SZ)sizeof(hv.info) ) ? E_OK : E_PAR;
		if ( er >= E_OK ) {
			knl_memcpy(buf, &hv.info, sizeof(hv.info));
			if ( p_asize != NULL ) *p_asize = sizeof(hv.info);
		}
		break;
	default:
		er = E_NOEXS;
		break;
	}
	tk_unl_mtx(hv.mtx);
	return er;
}

LOCAL ER ob_wri( void *ctx, UD *pos, INT recno, D off, CONST void *buf, SZ size, SZ *p_asize )
{
	ER	er;

	(void)ctx;
	(void)pos;
	if ( off != 0 ) return E_PAR;
	tk_loc_mtx(hv.mtx, TMO_FEVR);
	switch ( recno ) {
	case HEVC_REC_JOB:
		er = ( size <= HEVC_JOB_MAX ) ? decode((CONST T_HEVCJOB *)buf, size) : E_PAR;
		break;
	case HEVC_REC_CONF:
		er = ( size >= (SZ)sizeof(T_HEVCCONF) ) ? conf_set((CONST T_HEVCCONF *)buf) : E_PAR;
		break;
	default:
		er = E_NOEXS;
		break;
	}
	tk_unl_mtx(hv.mtx);
	if ( er >= E_OK && p_asize != NULL ) *p_asize = size;
	return er;
}

LOCAL UD ob_size( void *ctx, INT recno )
{
	(void)ctx;
	switch ( recno ) {
	case HEVC_REC_PICTURE:	return (UD)hv.conf.width * hv.conf.height * 3 / 2 * ( hv.conf.bitdepth == 10 ? 2 : 1 );
	case HEVC_REC_CONF:	return sizeof(T_HEVCCONF);
	case HEVC_REC_INFO:	return sizeof(T_HEVCINFO);
	default:		return 0;
	}
}

LOCAL CONST T_OBDVOPS hevc_ops = {
	OB_S_VIDEODEC, "hevc", HEVC_NREC, 0666, 0, 0,
	ob_text, ob_attr, ob_rea, ob_wri, ob_size, NULL
};

/* ---------------------------------------------------------------- start */

EXPORT ER knl_hevc_init( void )
{
	T_CMTX	cm;
	T_CFLG	cf;
	UW	mmu;
	ER	er;

	if ( hv.present ) return E_OK;
	cm.exinf = NULL;
	cm.mtxatr = TA_TFIFO;
	cm.ceilpri = 0;
	hv.mtx = tk_cre_mtx(&cm);
	cf.exinf = NULL;
	cf.flgatr = TA_TFIFO | TA_WMUL;
	cf.iflgptn = 0;
	hv.flg = tk_cre_flg(&cf);
	if ( hv.mtx <= 0 || hv.flg <= 0 ) return E_SYS;

	clock_up();
	/* the decoder reaches memory by its physical addresses */
	mmu = in_w(IOMMU_CTRL);
	if ( ( mmu & MMU_ENABLE ) && !( mmu & MMU_BYPASS ) ) out_w(IOMMU_CTRL, mmu | MMU_BYPASS);
	hv.info.version = in_w(REG(R_VERSION));
	tm_printf((UB *)"hevc: version %08x, clock %u Hz, iommu2 %08x -> %08x\n",
		  hv.info.version, hv.info.clock_hz, mmu, in_w(IOMMU_CTRL));

	if ( ( er = mem_get(&hv.cmd, PAGE_SIZE << CMD_ORDER) ) < E_OK ) return er;
	if ( ( er = mem_get(&hv.bits, PAGE_SIZE << BITS_ORDER) ) < E_OK ) return er;

	/* both passes' interrupts on, anything latched cleared */
	out_w(IC_CTRL, IC_ACTIVE1_EN | IC_ACTIVE2_EN);
	out_w(IC_CTRL, in_w(IC_CTRL) & ~IC_ZERO);
	knl_define_inthdr(HV_INTNO, TA_HLNG, (FP)hevc_inthdr);
	SetIntMode(HV_INTNO, IM_LEVEL);
	EnableInt(HV_INTNO, INTPRI_DEVICE);

	hv.present = TRUE;
	hv.info.present = 1;
	er = knl_obdev_add((CONST UB *)HEVC_OBJ_NAME, NULL, &hevc_ops, NULL, &hv.uuid);
	if ( er < E_OK ) tm_printf((UB *)"hevc: the object is not made (%d)\n", er);
	return E_OK;
}

/* ---------------------------------------------------------------- the kernel's own use */

EXPORT ER knl_hevc_conf( CONST T_HEVCCONF *c )
{
	ER	er;

	if ( !hv.present ) return E_NOEXS;
	tk_loc_mtx(hv.mtx, TMO_FEVR);
	er = conf_set(c);
	tk_unl_mtx(hv.mtx);
	return er;
}

EXPORT ER knl_hevc_decode( CONST void *job, SZ size )
{
	ER	er;

	if ( !hv.present ) return E_NOEXS;
	tk_loc_mtx(hv.mtx, TMO_FEVR);
	er = decode((CONST T_HEVCJOB *)job, size);
	tk_unl_mtx(hv.mtx);
	return er;
}

EXPORT ER knl_hevc_picture( UW slot, D off, void *buf, SZ size, SZ *p_asize )
{
	ER	er;

	if ( !hv.present ) return E_NOEXS;
	tk_loc_mtx(hv.mtx, TMO_FEVR);
	er = picture(slot, off, buf, size, p_asize);
	tk_unl_mtx(hv.mtx);
	return er;
}

EXPORT void knl_hevc_info( T_HEVCINFO *in )
{
	*in = hv.info;
}
