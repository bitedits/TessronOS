/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	prn.h
 *	印刷: pages written into a PDF file (design 11.22)
 *
 *	A program that prints says how many pages of its レイアウト用紙 it
 *	has and draws each of them when asked, into a drawing environment
 *	as large as that paper, with the calls it draws its window with.
 *	This layer lays each page on the 印刷用紙 as the 印刷の指定 say --
 *	which pages, how many copies, the paper, its way round, the
 *	magnification -- and writes the file: the shapes and lines as
 *	shapes and lines, the letters as letters of the face they were
 *	drawn in (the face carried in the file, cut down to the letters
 *	used), the pictures packed.
 *
 *	The file becomes a real object of the kind a file taken in from
 *	outside becomes (xf_import): its bytes in record 1, a record 0
 *	that shows its name. It can be given out again as a file the way
 *	any such object can.
 *
 *	The same pages can be drawn into pixels instead (prn_sheet), which
 *	is what 画面表示 shows before anything is written.
 */

#ifndef __TS_PRN_H__
#define __TS_PRN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <ts/dp.h>
#include <ts/uuid.h>

/* 品質 */
#define PRN_Q_NORMAL	0		/* 通常 */
#define PRN_Q_FAST	1		/* 高速: pictures at half their pixels, packed quickly */
#define PRN_Q_FINE	2		/* 高密度: packed hardest */

/* 倍率 */
#define PRN_SCALE_AUTO	0		/* 自動: the layout paper fitted to the printing paper */

#define PRN_PAGE_MAX	999		/* 部分ページ goes up to this */
#define PRN_COPY_MAX	99
#define PRN_DEPTH_MAX	8

/* 印刷の指定: what 印刷設定 and 印刷詳細設定 set */
typedef struct {
	BOOL	all;			/* 全ページ; else from .. to */
	INT	from, to;		/* counting from 1 */
	INT	copies;			/* 部数 */
	INT	quality;		/* PRN_Q_* */
	BOOL	colour;			/* カラー; off draws in greys */
	INT	paper_w, paper_h;	/* 印刷用紙 in tenths of a millimetre; 0 the layout paper's */
	BOOL	rotate;			/* 用紙回転: each sheet turned a quarter */
	INT	scale;			/* 倍率 in percent, PRN_SCALE_AUTO for 自動 */
	BOOL	links;			/* 仮身: 印刷 */
	BOOL	expand;			/* 展開 */
	INT	depth;			/* 深さ */
} T_PRNSET;

/* A program's pages, as this layer asks for them */
typedef struct {
	INT	npage;			/* pages of layout paper */
	INT	lw, lh;			/* the layout paper, in the drawing's pixels */
	INT	unit;			/* thousandths of a point to a pixel: 1000 a point, 750 at 96 to the inch */
	CONST UB *name;			/* 実身名: the file's title and its name */

	/*
	 * Draw layout page 'page' (from 0) into gid, the paper's corner at
	 * (0, 0) and its size 'sheet'. The environment may record rather
	 * than hold pixels; the program cannot tell and need not.
	 */
	ER	(*draw)( void *arg, INT gid, INT page, CONST T_DPRECT *sheet );
	void	*arg;
} T_PRNJOB;

IMPORT void prn_default( T_PRNSET *set );

/* The sheets that will be printed: copies of the pages in range */
IMPORT INT  prn_sheets( CONST T_PRNJOB *job, CONST T_PRNSET *set );
/* Which layout page sheet 'k' (from 0) is */
IMPORT INT  prn_sheet_page( CONST T_PRNJOB *job, CONST T_PRNSET *set, INT k );

/*
 * The printing paper in thousandths of a point, as it lies, and the
 * magnification used, in hundredths of a percent: what 印刷詳細設定
 * shows under 自動.
 */
IMPORT void prn_paper( CONST T_PRNJOB *job, CONST T_PRNSET *set, INT *p_w, INT *p_h,
		       INT *p_pct100 );

/*
 * A sheet drawn into pixels w by h -- the printing paper made that
 * size -- for 画面表示: white paper, and the layout page on it as it
 * will be printed.
 */
IMPORT ER   prn_sheet( CONST T_PRNJOB *job, CONST T_PRNSET *set, INT k, UW *px,
		       INT w, INT h );

/* The whole file in memory, the caller's to Kfree */
IMPORT ER   prn_pdf( CONST T_PRNJOB *job, CONST T_PRNSET *set, UB **p_bytes, SZ *p_len );

/*
 * The file made a real object named after the job ("名前.pdf"), its
 * link put in 'box' when one is given.
 */
IMPORT ER   prn_object( CONST T_PRNJOB *job, CONST T_PRNSET *set, CONST TS_UUID *box,
			TS_UUID *p_uuid );

/* What the last file came to, for a test or the message line */
typedef struct {
	INT	pages;			/* pages in the file */
	INT	faces;			/* faces carried */
	INT	glyphs;			/* glyphs of them kept */
	INT	images;			/* pictures, each once however often drawn */
	INT	patterns;
	SZ	bytes;
} T_PRNSTAT;

IMPORT void prn_stat( T_PRNSTAT *st );

#ifdef __cplusplus
}
#endif

#endif /* __TS_PRN_H__ */
