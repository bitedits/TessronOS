/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	prn_local.h
 *	What prn.c and prn_rec.c share: the file being written, and the page
 *	being drawn into it (design 11.22)
 */

#ifndef __PRN_LOCAL_H__
#define __PRN_LOCAL_H__

#include "pdf.h"

/* A picture or a pattern written once and named by every page that draws it */
typedef struct prn_res {
	struct prn_res	*next;
	UD		hash;
	INT		res;		/* /Im<res> or /P<res> */
	INT		obj;
} PRNRES;

/* The file as a whole */
typedef struct {
	PDF		pdf;
	PDFFACE		*faces;
	PRNRES		*imgs;
	PRNRES		*pats;
	INT		nface, nimg, npat;
	BOOL		gray;		/* カラー off: everything in greys */
	INT		quality;	/* PRN_Q_* */
	INT		level;		/* how hard streams are packed */
	INT		res_obj;	/* the one dictionary of resources every page names */

	/*
	 * How the page's pixels lie on the sheet: x' = k x + e, y' = f - k y,
	 * k in millionths of a point to a pixel and e, f in millionths of a
	 * point. A pattern is laid in the sheet's own space and needs it.
	 */
	D		k, e, f;
} PRNDOC;

/* A run of glyphs on one baseline, written as one TJ */
#define PRN_RUN_MAX	128

/* The page being drawn: its drawing, and what it has set so far */
typedef struct {
	PRNDOC		*doc;
	PBUF		c;		/* the page's drawing (content stream) */
	T_DPRECT	page;		/* the layout page, pixels */
	T_DPRECT	clip;		/* the clip in force */
	BOOL		clipped;	/* inside a 'q ... W n' of its own */

	BOOL		fill_set, stroke_set;
	UW		fill, stroke;	/* the colours set */
	INT		fill_pat, stroke_pat;	/* or the pattern set, 0 none */
	INT		lw;		/* the line width set, in thousandths of a pixel; -1 none */
	UINT		dash;		/* DP_LINE_* set */
	INT		cap;		/* line cap set, -1 none */

	BOOL		in_text;
	PDFFACE		*face;		/* the face set with Tf */
	INT		tr;		/* the render mode set */
	W		tw;		/* the stroke width of a bold run set, 16.16 */

	/* the run being gathered */
	INT		nrun;
	UH		run_g[PRN_RUN_MAX];
	W		run_e[PRN_RUN_MAX];	/* each glyph's x, 16.16 */
	W		run_m[6];
	INT		run_px;
} PRNPAGE;

IMPORT CONST T_DPREC	prn_rec_ops;

IMPORT void prn_page_begin( PRNPAGE *pg, PRNDOC *doc, INT w, INT h );
IMPORT void prn_page_end( PRNPAGE *pg );	/* what is still open, closed */
IMPORT void prn_doc_free( PRNDOC *doc );

/* A number in millionths, written to six places */
IMPORT void prn_micro( PBUF *b, D v );

#endif /* __PRN_LOCAL_H__ */
