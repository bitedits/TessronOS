/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	hevc.h
 *	The HEVC decoder of the Raspberry Pi 5 as a device object (design
 *	6.7): "動画の復号器", of the subtype OB_S_VIDEODEC.
 *
 *	The decoder decodes one picture at a time from what the program has
 *	parsed of the stream: the sequence's and the picture's parameters,
 *	the slices' headers and the slices' bytes as they are in the stream
 *	(emulation prevention bytes left in). It does not parse the stream
 *	itself and does not keep the decoding order: the program says, for
 *	each picture, which of the decoder's picture slots it goes into and
 *	which slots its references are in.
 *
 *	  1. Record HEVC_REC_CONF is written with a T_HEVCCONF: the size of
 *	     the pictures, their bit depth (8 or 10) and the number of
 *	     slots. The slots are made then (memory the decoder reaches,
 *	     4:2:0 in columns of 128 bytes).
 *	  2. Record HEVC_REC_JOB is written with one picture: a T_HEVCJOB,
 *	     then T_HEVCJOB.nslice T_HEVCSLICE, then the slices' bytes. The
 *	     write returns when the picture is decoded (or fails).
 *	  3. Record HEVC_REC_PICTURE is read for the picture of a slot. With
 *	     8 bits it is NV12: the luma plane (width × height bytes), then
 *	     U and V by turns (width × height / 2 bytes). With 10 bits it is
 *	     P010: the same planes with each sample in 16 bits, little
 *	     endian, its 10 bits at the top and zeros below (width × height
 *	     × 2 bytes, then width × height). The read's offset gives the
 *	     slot in its upper 32 bits and the byte in the lower.
 *	  4. Record HEVC_REC_INFO is read for a T_HEVCINFO.
 *
 *	The Main and Main 10 profiles: 8 or 10 bits, the same for luma and
 *	chroma; 4:2:0; neither tiles together with wavefronts; pictures up
 *	to HEVC_MAX_WIDTH × HEVC_MAX_HEIGHT.
 *
 *	Errors: E_NOEXS when there is no decoder (no such object at all),
 *	E_PAR for a job that does not fit the slots or the decoder, E_TMOUT
 *	when the decoder does not finish, E_IO when it reports an error.
 */

#ifndef __TS_HEVC_H__
#define __TS_HEVC_H__

#include <tk/typedef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HEVC_OBJ_NAME		"動画の復号器"

/* Records of the object */
#define HEVC_REC_JOB		1		/* W: T_HEVCJOB, the slices, their bytes */
#define HEVC_REC_PICTURE	2		/* R: a slot's picture, NV12 or P010 */
#define HEVC_REC_CONF		3		/* RW: T_HEVCCONF */
#define HEVC_REC_INFO		4		/* R: T_HEVCINFO */
#define HEVC_NREC		5

#define HEVC_MAX_WIDTH		1920
#define HEVC_MAX_HEIGHT		1088
#define HEVC_MAX_SLOTS		17		/* 16 references and the picture decoded */
#define HEVC_MAX_SLICES		64
#define HEVC_MAX_DPB		16
#define HEVC_JOB_MAX		( 4 * 1024 * 1024 )	/* bytes of a job, its slices' bytes included */

#define HEVC_JOB_MAGIC		0x48455631U	/* "HEV1" */

typedef struct {
	UW	width, height;		/* luma samples */
	UW	bitdepth;		/* 8 (Main) or 10 (Main 10) */
	UW	nslots;			/* 1 .. HEVC_MAX_SLOTS */
} T_HEVCCONF;

typedef struct {
	UW	present;		/* 1: the decoder is there */
	UW	version;		/* what the decoder says it is */
	UW	clock_hz;		/* its clock */
	UW	decoded;		/* pictures decoded */
	UW	errors;			/* jobs that failed */
	UW	last_error;		/* the status the last failure ended with */
	UW	last_us;		/* microseconds the last picture took */
	UW	rsv;
} T_HEVCINFO;

/* The sequence's parameters (T_HEVCSPS.flags) */
#define HEVC_SPS_SEPARATE_COLOUR_PLANE	0x0001
#define HEVC_SPS_SCALING_LIST		0x0002
#define HEVC_SPS_AMP			0x0004
#define HEVC_SPS_SAO			0x0008
#define HEVC_SPS_PCM			0x0010
#define HEVC_SPS_PCM_LOOP_FILTER_OFF	0x0020
#define HEVC_SPS_TEMPORAL_MVP		0x0040
#define HEVC_SPS_STRONG_INTRA_SMOOTHING	0x0080

typedef struct {
	UH	width, height;		/* pic_width/height_in_luma_samples */
	UB	bit_depth_luma, bit_depth_chroma;	/* T_HEVCCONF.bitdepth, both */
	UB	chroma_format_idc;	/* 1 */
	UB	log2_min_cb;		/* log2_min_luma_coding_block_size */
	UB	log2_diff_max_min_cb;
	UB	log2_min_tb;		/* log2_min_luma_transform_block_size */
	UB	log2_diff_max_min_tb;
	UB	max_th_depth_inter, max_th_depth_intra;
	UB	pcm_bit_depth_luma, pcm_bit_depth_chroma;
	UB	log2_min_pcm_cb;
	UB	log2_diff_max_min_pcm_cb;
	UB	max_sub_layers_minus1;
	UW	flags;			/* HEVC_SPS_* */
} T_HEVCSPS;

/* The picture's parameters (T_HEVCPPS.flags) */
#define HEVC_PPS_SIGN_DATA_HIDING	0x0001
#define HEVC_PPS_CONSTRAINED_INTRA	0x0002
#define HEVC_PPS_TRANSFORM_SKIP		0x0004
#define HEVC_PPS_CU_QP_DELTA		0x0008
#define HEVC_PPS_WEIGHTED_PRED		0x0010
#define HEVC_PPS_WEIGHTED_BIPRED	0x0020
#define HEVC_PPS_TRANSQUANT_BYPASS	0x0040
#define HEVC_PPS_TILES			0x0080
#define HEVC_PPS_ENTROPY_SYNC		0x0100	/* wavefronts */
#define HEVC_PPS_LOOP_FILTER_TILES	0x0200	/* loop filter across tiles */

typedef struct {
	B	init_qp_minus26;
	UB	diff_cu_qp_delta_depth;
	B	cb_qp_offset, cr_qp_offset;
	UB	num_tile_columns_minus1, num_tile_rows_minus1;
	UB	column_width_minus1[20];
	UB	row_height_minus1[22];
	UB	log2_parallel_merge_level_minus2;
	UB	rsv;
	UW	flags;			/* HEVC_PPS_* */
} T_HEVCPPS;

/*
 * The scaling lists in raster order (each 8×8 list of the 16×16 and
 * 32×32 sizes before it is upsampled), with the DC values of those two
 */
typedef struct {
	UB	s4x4[6][16];
	UB	s8x8[6][64];
	UB	s16x16[6][64];
	UB	s32x32[2][64];
	UB	dc16[6];
	UB	dc32[2];
} T_HEVCSCALE;

/* One picture of the decoded picture buffer */
#define HEVC_DPB_LONG_TERM	0x01

typedef struct {
	UB	slot;			/* the decoder's slot the picture is in */
	UB	flags;			/* HEVC_DPB_* */
	UH	rsv;
	W	poc;			/* PicOrderCntVal */
} T_HEVCDPB;

/* A slice's header (T_HEVCSLICE.flags) */
#define HEVC_SL_SAO_LUMA		0x0001
#define HEVC_SL_SAO_CHROMA		0x0002
#define HEVC_SL_TEMPORAL_MVP		0x0004
#define HEVC_SL_MVD_L1_ZERO		0x0008
#define HEVC_SL_CABAC_INIT		0x0010
#define HEVC_SL_COLLOCATED_FROM_L0	0x0020
#define HEVC_SL_DEBLOCKING_OFF		0x0040
#define HEVC_SL_LOOP_FILTER_SLICES	0x0080	/* loop filter across slices */
#define HEVC_SL_DEPENDENT		0x0100	/* a dependent slice segment */

/* T_HEVCSLICE.type */
#define HEVC_SLICE_B		0
#define HEVC_SLICE_P		1
#define HEVC_SLICE_I		2

typedef struct {
	UW	data_offset;		/* the slice's NAL unit in the job's bytes */
	UW	data_size;		/* its bytes, header included */
	UW	header_bytes;		/* of them, the header's (to the slice data), as they are in the stream */
	UW	segment_addr;		/* slice_segment_address */
	UB	type;			/* HEVC_SLICE_* */
	UB	nal_unit_type;
	UB	temporal_id_plus1;
	UB	num_ref_idx_l0_minus1, num_ref_idx_l1_minus1;
	UB	collocated_ref_idx;
	UB	five_minus_max_merge_cand;
	B	qp_delta;
	B	cb_qp_offset, cr_qp_offset;
	B	beta_offset_div2, tc_offset_div2;
	UB	ref_l0[HEVC_MAX_DPB];	/* RefPicList0, as indexes into T_HEVCJOB.dpb */
	UB	ref_l1[HEVC_MAX_DPB];
	UB	luma_log2_weight_denom;
	B	delta_chroma_log2_weight_denom;
	B	delta_luma_weight[2][HEVC_MAX_DPB];
	B	luma_offset[2][HEVC_MAX_DPB];
	B	delta_chroma_weight[2][HEVC_MAX_DPB][2];
	B	chroma_offset[2][HEVC_MAX_DPB][2];
	UH	rsv;
	W	poc;			/* the picture's PicOrderCntVal */
	UW	flags;			/* HEVC_SL_* */
} T_HEVCSLICE;

typedef struct {
	UW		magic;		/* HEVC_JOB_MAGIC */
	UW		size;		/* bytes of the whole job */
	UW		out_slot;	/* the slot the picture goes into */
	UW		nslice;		/* T_HEVCSLICE after this */
	UW		ndpb;		/* entries of dpb used */
	UW		rsv;
	T_HEVCDPB	dpb[HEVC_MAX_DPB];
	T_HEVCSPS	sps;
	T_HEVCPPS	pps;
	T_HEVCSCALE	scale;
} T_HEVCJOB;

/*
 * The kernel's: the decoder made an object when the machine has one
 * (Raspberry Pi 5). The job and the picture as the object's records
 * take them, for the kernel's own check (HEVCTEST=1).
 */
IMPORT ER  knl_hevc_init( void );
IMPORT ER  knl_hevc_conf( CONST T_HEVCCONF *c );
IMPORT ER  knl_hevc_decode( CONST void *job, SZ size );
IMPORT ER  knl_hevc_picture( UW slot, D off, void *buf, SZ size, SZ *p_asize );
IMPORT void knl_hevc_info( T_HEVCINFO *in );

#ifdef __cplusplus
}
#endif
#endif /* __TS_HEVC_H__ */
