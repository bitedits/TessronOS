/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	pdf.h
 *	The writer of PDF files, as the parts of 印刷 share it (design 11.22)
 *
 *	pdf.c keeps the bytes of the file and the numbers of its objects;
 *	pdf_font.c makes the faces a file carries, cut down to the letters
 *	it uses; prn_rec.c turns what is drawn into the drawing of a page.
 *	Nothing here knows about a window or a panel, and nothing here uses
 *	floating point: a number in the file is written from integers.
 */

#ifndef __PRN_PDF_H__
#define __PRN_PDF_H__

/* ---------------------------------------------------------------- bytes */

/* A run of bytes that grows as it is written: a page's drawing, a dictionary */
typedef struct {
	UB	*p;
	SZ	n, cap;
	BOOL	fail;
} PBUF;

IMPORT void pb_init( PBUF *b );
IMPORT void pb_free( PBUF *b );
IMPORT void pb_bytes( PBUF *b, CONST void *s, SZ n );
IMPORT void pb_str( PBUF *b, CONST char *s );
IMPORT void pb_int( PBUF *b, D v );
/* v thousandths written as a decimal with what it needs of three places */
IMPORT void pb_milli( PBUF *b, D v );
/* a 16.16 number written to three places */
IMPORT void pb_fix( PBUF *b, D v );
IMPORT void pb_hex( PBUF *b, UW v, INT digits );
/* a name of PDF (/Name), its unusual bytes written as #xx */
IMPORT void pb_name( PBUF *b, CONST UB *s );
/* words as a string of UTF-16BE in hex, with its byte-order mark: <FEFF...> */
IMPORT void pb_text16( PBUF *b, CONST UB *utf8 );

/* One letter out of UTF-8; its code, and how many bytes it took in *p_n */
IMPORT UW   pdf_utf8( CONST UB *s, INT *p_n );

/* ---------------------------------------------------------------- the file */

#define PDF_CHUNK	65536

typedef struct pdf_chunk {
	struct pdf_chunk	*next;
	SZ			n;
	UB			b[PDF_CHUNK];
} PDFCHUNK;

typedef struct {
	PDFCHUNK	*head, *tail;
	SZ		size;			/* bytes written */
	SZ		*off;			/* where each object begins, by number */
	INT		nobj, cap;
	BOOL		fail;

	/* for reading the finished file back out */
	PDFCHUNK	*rd;
	SZ		rd_at;
} PDF;

IMPORT ER   pdf_open( PDF *f );
IMPORT void pdf_free( PDF *f );
IMPORT void pdf_write( PDF *f, CONST void *s, SZ n );
IMPORT void pdf_puts( PDF *f, CONST char *s );
IMPORT void pdf_buf( PDF *f, CONST PBUF *b );

/* A number for an object that is written later */
IMPORT INT  pdf_new_obj( PDF *f );
/* "n 0 obj" for a number given out before; pdf_end_obj ends it */
IMPORT void pdf_begin_obj( PDF *f, INT num );
IMPORT void pdf_end_obj( PDF *f );
/* A whole object of a dictionary and a stream, packed when 'pack' */
IMPORT ER   pdf_stream( PDF *f, INT num, CONST char *dict, CONST UB *data, SZ len,
			BOOL pack, INT level );
/* The same with the dictionary's entries in a buffer */
IMPORT ER   pdf_stream_buf( PDF *f, INT num, CONST PBUF *dict, CONST UB *data, SZ len,
			    BOOL pack, INT level );
/* The table of where the objects are, and the trailer naming the catalog and the information */
IMPORT ER   pdf_close( PDF *f, INT root, INT info );

/* The finished file read out from the start, a piece at a time: 0 at the end */
IMPORT void pdf_rewind( PDF *f );
IMPORT INT  pdf_read( PDF *f, void *buf, SZ n );

/* ---------------------------------------------------------------- faces */

/*
 * How a face's tables are reached: 'table' fills in the bytes of the
 * table with that tag (four letters as one number, the first letter
 * the highest byte) and says how long it is, or answers below E_OK
 * when the face has no such table. With buf NULL it only says the
 * length.
 */
typedef ER (*PDF_TABLE)( void *arg, UW tag, UB *buf, SZ *p_len );

#define PDF_TAG(a, b, c, d)	( ( (UW)(a) << 24 ) | ( (UW)(b) << 16 ) | ( (UW)(c) << 8 ) | (UW)(d) )

/* A face as a file carries it: which letters of it are used */
typedef struct pdf_face {
	struct pdf_face	*next;
	ID		key;			/* whose face this is (fn_'s number) */
	INT		res;			/* /F<res> in the page's resources */
	INT		obj;			/* the Type0 font's object */
	INT		nglyph;
	UB		*used;			/* a bit for each glyph */
	UW		*uni;			/* the letter each glyph was drawn for, 0 none */
	UH		*adv;			/* each glyph's advance, thousandths of the em */
	INT		upem;
	BOOL		cff;			/* outlines of PostScript (CFF), else TrueType */
	UB		psname[64];
	INT		ascent, descent, capheight;	/* thousandths of the em */
	INT		bbox[4];
	PDF_TABLE	table;
	void		*arg;
} PDFFACE;

/* A face got ready for use: its tables read for the advances and the names */
IMPORT ER   pdf_face_init( PDFFACE *ff, PDF_TABLE table, void *arg );
IMPORT void pdf_face_use( PDFFACE *ff, UINT gid, UW cp );
IMPORT void pdf_face_free( PDFFACE *ff );

/*
 * The face written: the font cut down to the glyphs used (each glyph
 * keeps its number, those not used made empty), its widths, and the
 * table that turns its glyphs back into letters so that the words of
 * the page can be found and taken out.
 */
IMPORT ER   pdf_face_write( PDF *f, PDFFACE *ff );

/*
 * The font program alone, cut down: what pdf_face_write puts in the
 * file, for a test to look at. The bytes are the caller's to Kfree.
 */
IMPORT ER   pdf_cff_subset( PDFFACE *ff, UB **p_out, SZ *p_len );
IMPORT ER   pdf_ttf_subset( PDFFACE *ff, UB **p_out, SZ *p_len );

#endif /* __PRN_PDF_H__ */
