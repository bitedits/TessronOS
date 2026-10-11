/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	prn_rec.c
 *	What is drawn on a page, made into the drawing of a PDF page
 *	(design 11.22.3)
 *
 *	A page is drawn through an environment that records (dp_open_rec)
 *	with the same calls that draw it on the screen, and each call comes
 *	here as it was made: a rectangle, a line, a polygon, a picture, a
 *	glyph of a face. Each is written as the operators of PDF that draw
 *	the same thing, in the page's own pixels; the sheet's matrix,
 *	written once at the head of the page, takes those pixels to points.
 *
 *	The screen draws a line of one pixel through the middle of the
 *	pixels it covers and fills a shape up to its edges, so a line is
 *	drawn half a pixel in from the coordinates given and a fill at them,
 *	and the two meet on paper as they meet on the screen.
 *
 *	What may be touched is one rectangle at a time. A change of it closes
 *	the state saved for the last one (Q) and saves a new one with the
 *	new rectangle as its clip (q re W n), which also forgets the colours
 *	and widths set; they are set again when next needed.
 *
 *	Letters come as glyphs with a matrix (fn.c). Those along one
 *	baseline in one face and size are gathered into one string of
 *	glyphs (TJ), each moved by the difference between where the line
 *	put it and where the face's own advance would, so that the page
 *	keeps the screen's spacing to the pixel and the words stay together
 *	when they are found or copied. A letter that is turned, or stands
 *	in a column of 縦書き, has a matrix of its own.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <ts/dp.h>
#include <ts/fn.h>
#include <ts/img.h>
#include <ts/print.h>
#include "prn_local.h"

/* ---------------------------------------------------------------- numbers */

EXPORT void prn_micro( PBUF *b, D v )
{
	UD	u;
	INT	frac, n = 6, i;
	char	t[6];

	if ( v < 0 ) {
		pb_bytes(b, "-", 1);
		u = (UD)( -v );
	} else {
		u = (UD)v;
	}
	pb_int(b, (D)( u / 1000000 ));
	frac = (INT)( u % 1000000 );
	if ( frac == 0 ) {
		return;
	}
	for ( i = 5; i >= 0; i-- ) {
		t[i] = (char)( '0' + frac % 10 );
		frac /= 10;
	}
	while ( n > 0 && t[n - 1] == '0' ) {
		n--;
	}
	pb_bytes(b, ".", 1);
	pb_bytes(b, t, n);
}

/* A coordinate in pixels plus thousandths: x + half a pixel is (x, 500) */
LOCAL void coord( PBUF *b, INT v, INT milli )
{
	pb_milli(b, (D)v * 1000 + milli);
}

LOCAL void sp( PBUF *b )
{
	pb_bytes(b, " ", 1);
}

/* ---------------------------------------------------------------- colours */

LOCAL UW grey_of( UW c )
{
	UW	r = ( c >> 16 ) & 0xFF, g = ( c >> 8 ) & 0xFF, bl = c & 0xFF;
	UW	y = ( 299 * r + 587 * g + 114 * bl + 500 ) / 1000;

	return ( y << 16 ) | ( y << 8 ) | y;
}

LOCAL void colour_ops( PRNPAGE *pg, UW c, BOOL stroke )
{
	PBUF	*b = &pg->c;
	UW	r = ( c >> 16 ) & 0xFF, g = ( c >> 8 ) & 0xFF, bl = c & 0xFF;

	if ( pg->doc->gray ) {
		pb_milli(b, (D)( ( 299 * r + 587 * g + 114 * bl ) / 255 ));
		pb_str(b, stroke ? " G\n" : " g\n");
		return;
	}
	pb_milli(b, (D)( r * 1000 / 255 ));
	sp(b);
	pb_milli(b, (D)( g * 1000 / 255 ));
	sp(b);
	pb_milli(b, (D)( bl * 1000 / 255 ));
	pb_str(b, stroke ? " RG\n" : " rg\n");
}

/* How many bits of a tone's four rows are set: of 128 */
LOCAL INT tone_bits( INT tone )
{
	INT	i, n = 0;

	for ( i = 0; i < 4; i++ ) {
		UW	v = dp_tone_rows[tone & ( DP_TONE_MAX - 1 )][i];

		while ( v != 0 ) {
			n += (INT)( v & 1 );
			v >>= 1;
		}
	}
	return n;
}

/*
 * A tone of two colours is laid on paper as the colour it reads as:
 * the two mixed in the share of the dither. A dither of dots is the
 * screen's way of making a colour it does not have; paper has it.
 */
LOCAL UW tone_colour( CONST T_DPPAT *pat )
{
	INT	n = tone_bits(pat->tone), k;
	UW	out = 0;

	if ( pat->fore == pat->back || n == 128 ) {
		return pat->fore;
	}
	if ( n == 0 ) {
		return pat->back;
	}
	for ( k = 0; k < 24; k += 8 ) {
		UW	a = ( pat->fore >> k ) & 0xFF, z = ( pat->back >> k ) & 0xFF;

		out |= ( ( a * (UW)n + z * (UW)( 128 - n ) + 64 ) / 128 ) << k;
	}
	return out;
}

/* ---------------------------------------------------------------- pictures */

LOCAL UD fnv( UD h, CONST void *p, SZ n )
{
	CONST UB	*b = (CONST UB *)p;
	SZ		i;

	for ( i = 0; i < n; i++ ) {
		h = ( h ^ b[i] ) * 1099511628211ULL;
	}
	return h;
}

LOCAL PRNRES *res_find( PRNRES *list, UD hash )
{
	for ( ; list != NULL; list = list->next ) {
		if ( list->hash == hash ) {
			return list;
		}
	}
	return NULL;
}

LOCAL PRNRES *res_new( PRNDOC *doc, PRNRES **list, INT *count, UD hash )
{
	PRNRES	*r = (PRNRES *)Kmalloc(sizeof(PRNRES));

	if ( r == NULL ) {
		return NULL;
	}
	r->hash = hash;
	r->res = ++*count;
	r->obj = pdf_new_obj(&doc->pdf);
	r->next = *list;
	*list = r;

	return r;
}

/*
 * Rows of samples written as a picture: each row led by the PNG filter
 * that takes the row above from it (Up), which turns a picture's flat
 * areas and repeated rows into noughts that pack well.
 */
LOCAL ER image_obj( PRNDOC *doc, INT obj, INT w, INT h, INT comps, CONST UB *rows,
		    INT smask, BOOL mask_only )
{
	PBUF	d, dict;
	INT	y, x, rl = w * comps;
	ER	er;

	pb_init(&d);
	pb_init(&dict);
	if ( !mask_only ) {
		for ( y = 0; y < h; y++ ) {
			UB	f = 2;

			pb_bytes(&d, &f, 1);
			for ( x = 0; x < rl; x++ ) {
				UB	v = rows[(SZ)y * rl + x];

				if ( y > 0 ) {
					v = (UB)( v - rows[(SZ)( y - 1 ) * rl + x] );
				}
				pb_bytes(&d, &v, 1);
			}
		}
	}
	pb_str(&dict, " /Type /XObject /Subtype /Image /Width ");
	pb_int(&dict, w);
	pb_str(&dict, " /Height ");
	pb_int(&dict, h);
	if ( mask_only ) {
		/* one bit a pixel, set where the ink goes */
		pb_str(&dict, " /ImageMask true /BitsPerComponent 1 /Decode [ 1 0 ]");
		er = pdf_stream_buf(&doc->pdf, obj, &dict, rows, (SZ)( ( w + 7 ) / 8 ) * h,
				    TRUE, doc->level);
	} else {
		pb_str(&dict, ( comps == 3 ) ? " /ColorSpace /DeviceRGB" : " /ColorSpace /DeviceGray");
		pb_str(&dict, " /BitsPerComponent 8 /DecodeParms << /Predictor 15 /Colors ");
		pb_int(&dict, comps);
		pb_str(&dict, " /Columns ");
		pb_int(&dict, w);
		pb_str(&dict, " >>");
		if ( smask > 0 ) {
			pb_str(&dict, " /SMask ");
			pb_int(&dict, smask);
			pb_str(&dict, " 0 R");
		}
		er = d.fail ? E_NOMEM : pdf_stream_buf(&doc->pdf, obj, &dict, d.p, d.n, TRUE, doc->level);
	}
	pb_free(&d);
	pb_free(&dict);

	return er;
}

/*
 * A picture of the page's: its colours (grey when the file is), and
 * where it is clear a soft mask that leaves the paper. Written once and
 * found again by its pixels when drawn again -- a link's icon is the
 * same picture on every link.
 */
LOCAL PRNRES *picture( PRNDOC *doc, CONST T_DPRECIMG *im )
{
	UD	h = 14695981039346656037ULL;
	INT	x, y, w = im->w, ht = im->h, comps = doc->gray ? 1 : 3, step = 1;
	PRNRES	*r;
	UB	*rows, *alpha = NULL;
	BOOL	clear = FALSE;
	INT	smask = 0;

	h = fnv(h, &im->kind, sizeof(im->kind));
	h = fnv(h, &w, sizeof(w));
	h = fnv(h, &ht, sizeof(ht));
	h = fnv(h, &im->colour, sizeof(im->colour));
	h = fnv(h, &doc->gray, sizeof(doc->gray));
	for ( y = 0; y < ht; y++ ) {
		if ( im->kind == DP_REC_ARGB ) {
			h = fnv(h, (CONST UW *)im->px + (SZ)y * im->pitch, sizeof(UW) * (SZ)w);
		} else if ( im->kind == DP_REC_MONO ) {
			h = fnv(h, (CONST UB *)im->px + (SZ)y * im->pitch, (SZ)( w + 7 ) / 8);
		} else {
			h = fnv(h, (CONST UB *)im->px + (SZ)y * im->pitch, (SZ)w);
		}
	}
	r = res_find(doc->imgs, h);
	if ( r != NULL ) {
		return r;
	}
	r = res_new(doc, &doc->imgs, &doc->nimg, h);
	if ( r == NULL ) {
		return NULL;
	}

	if ( im->kind == DP_REC_MONO ) {
		/* the bits as they are, each row whole bytes */
		SZ	rb = (SZ)( w + 7 ) / 8;

		rows = (UB *)Kmalloc(rb * (SZ)ht + 1);
		if ( rows == NULL ) {
			return NULL;
		}
		for ( y = 0; y < ht; y++ ) {
			knl_memcpy(rows + (SZ)y * rb, (CONST UB *)im->px + (SZ)y * im->pitch, rb);
		}
		(void)image_obj(doc, r->obj, w, ht, 1, rows, 0, TRUE);
		Kfree(rows);
		return r;
	}

	/* 高速: a large picture at half its pixels each way */
	if ( doc->quality == PRN_Q_FAST && im->kind == DP_REC_ARGB && w >= 64 && ht >= 64 ) {
		step = 2;
	}
	w = ( im->w + step - 1 ) / step;
	ht = ( im->h + step - 1 ) / step;
	rows = (UB *)Kmalloc((SZ)w * ht * comps + 1);
	alpha = (UB *)Kmalloc((SZ)w * ht + 1);
	if ( rows == NULL || alpha == NULL ) {
		if ( rows != NULL ) Kfree(rows);
		if ( alpha != NULL ) Kfree(alpha);
		return NULL;
	}
	for ( y = 0; y < ht; y++ ) {
		for ( x = 0; x < w; x++ ) {
			UW	c;
			UB	a = 255;
			SZ	o = (SZ)y * w + x;

			if ( im->kind == DP_REC_ARGB ) {
				c = ((CONST UW *)im->px)[(SZ)y * step * im->pitch + x * step];
				if ( c == im->colour || c == IMG_CLEAR ) {
					a = 0;
					c = 0x00FFFFFFU;
					clear = TRUE;
				}
			} else {
				/* cover in one colour: the colour, and the cover as how much of it shows */
				c = im->colour;
				a = ((CONST UB *)im->px)[(SZ)y * im->pitch + x];
				clear = TRUE;
			}
			if ( comps == 1 ) {
				rows[o] = (UB)( grey_of(c) & 0xFF );
			} else {
				rows[o * 3] = (UB)( c >> 16 );
				rows[o * 3 + 1] = (UB)( c >> 8 );
				rows[o * 3 + 2] = (UB)c;
			}
			alpha[o] = a;
		}
	}
	if ( clear ) {
		smask = pdf_new_obj(&doc->pdf);
		(void)image_obj(doc, smask, w, ht, 1, alpha, 0, FALSE);
	}
	(void)image_obj(doc, r->obj, w, ht, comps, rows, smask, FALSE);
	Kfree(rows);
	Kfree(alpha);

	return r;
}

/*
 * A tile of pixels laid from the environment's origin, as a pattern of
 * the sheet's: a cell the tile's size holding the tile as a picture,
 * laid with the sheet's matrix moved to the origin so that the tiles
 * fall where the screen puts them.
 */
LOCAL PRNRES *tile_pattern( PRNPAGE *pg, CONST T_DPPAT *pat, INT ox, INT oy )
{
	PRNDOC		*doc = pg->doc;
	UD		h = 14695981039346656037ULL;
	INT		hs = pat->hs, vs = pat->vs, stride = ( hs + 31 ) / 32, x, y;
	PRNRES		*r;
	T_DPRECIMG	im;
	UW		*px;
	PRNRES		*pic;
	PBUF		d, cell;
	D		e, f;

	ox %= hs;
	oy %= vs;
	h = fnv(h, pat->tile, sizeof(UW) * (SZ)hs * vs);
	if ( pat->mask != NULL ) {
		h = fnv(h, pat->mask, sizeof(UW) * (SZ)stride * vs);
	}
	h = fnv(h, &hs, sizeof(hs));
	h = fnv(h, &vs, sizeof(vs));
	h = fnv(h, &ox, sizeof(ox));
	h = fnv(h, &oy, sizeof(oy));
	h = fnv(h, &doc->k, sizeof(doc->k));
	h = fnv(h, &doc->e, sizeof(doc->e));
	h = fnv(h, &doc->f, sizeof(doc->f));
	h = fnv(h, &doc->gray, sizeof(doc->gray));
	r = res_find(doc->pats, h);
	if ( r != NULL ) {
		return r;
	}
	/* the tile as a picture, its masked pixels clear */
	px = (UW *)Kmalloc(sizeof(UW) * (SZ)hs * vs);
	if ( px == NULL ) {
		return NULL;
	}
	for ( y = 0; y < vs; y++ ) {
		for ( x = 0; x < hs; x++ ) {
			BOOL	on = TRUE;

			if ( pat->mask != NULL ) {
				on = (BOOL)( ( ( pat->mask[y * stride + x / 32] >> ( 31 - ( x & 31 ) ) ) & 1U ) != 0 );
			}
			px[y * hs + x] = on ? ( pat->tile[y * hs + x] & 0x00FFFFFFU ) : IMG_CLEAR;
		}
	}
	im.kind = DP_REC_ARGB;
	im.x = 0;
	im.y = 0;
	im.w = hs;
	im.h = vs;
	im.px = px;
	im.pitch = hs;
	im.colour = IMG_CLEAR;
	pic = picture(doc, &im);
	Kfree(px);
	if ( pic == NULL ) {
		return NULL;
	}
	r = res_new(doc, &doc->pats, &doc->npat, h);
	if ( r == NULL ) {
		return NULL;
	}
	pb_init(&d);
	pb_init(&cell);
	pb_str(&cell, "q ");
	pb_int(&cell, hs);
	pb_str(&cell, " 0 0 -");
	pb_int(&cell, vs);
	pb_str(&cell, " 0 ");
	pb_int(&cell, vs);
	pb_str(&cell, " cm /I Do Q\n");
	/* the sheet's matrix, moved to where the tiles start */
	e = doc->e + doc->k * ox;
	f = doc->f - doc->k * oy;
	pb_str(&d, " /Type /Pattern /PatternType 1 /PaintType 1 /TilingType 1 /BBox [ 0 0 ");
	pb_int(&d, hs);
	sp(&d);
	pb_int(&d, vs);
	pb_str(&d, " ] /XStep ");
	pb_int(&d, hs);
	pb_str(&d, " /YStep ");
	pb_int(&d, vs);
	pb_str(&d, " /Matrix [ ");
	prn_micro(&d, doc->k);
	pb_str(&d, " 0 0 ");
	prn_micro(&d, -doc->k);
	sp(&d);
	prn_micro(&d, e);
	sp(&d);
	prn_micro(&d, f);
	pb_str(&d, " ] /Resources << /XObject << /I ");
	pb_int(&d, pic->obj);
	pb_str(&d, " 0 R >> >>");
	(void)pdf_stream_buf(&doc->pdf, r->obj, &d, cell.p, cell.n, FALSE, 0);
	pb_free(&d);
	pb_free(&cell);

	return r;
}

/* ---------------------------------------------------------------- the state */

LOCAL void text_end( PRNPAGE *pg );

LOCAL void forget_state( PRNPAGE *pg )
{
	pg->fill_set = FALSE;
	pg->stroke_set = FALSE;
	pg->fill_pat = 0;
	pg->stroke_pat = 0;
	pg->lw = -1;
	pg->dash = DP_LINE_SOLID;
	pg->cap = -1;
	pg->tw = -1;
}

/* What may be touched made the rectangle given; the whole page needs no clip */
LOCAL void set_clip( PRNPAGE *pg, CONST T_DPRECT *c )
{
	BOOL	whole;

	if ( c->left == pg->clip.left && c->top == pg->clip.top
	  && c->right == pg->clip.right && c->bottom == pg->clip.bottom ) {
		return;
	}
	text_end(pg);
	whole = (BOOL)( c->left <= pg->page.left && c->top <= pg->page.top
		     && c->right >= pg->page.right && c->bottom >= pg->page.bottom );
	if ( pg->clipped ) {
		pb_str(&pg->c, "Q\n");
		pg->clipped = FALSE;
		forget_state(pg);
	}
	if ( !whole ) {
		pb_str(&pg->c, "q ");
		coord(&pg->c, c->left, 0);
		sp(&pg->c);
		coord(&pg->c, c->top, 0);
		sp(&pg->c);
		coord(&pg->c, c->right - c->left, 0);
		sp(&pg->c);
		coord(&pg->c, c->bottom - c->top, 0);
		pb_str(&pg->c, " re W n\n");
		pg->clipped = TRUE;
	}
	pg->clip = *c;
}

/* The colour or pattern a shape is filled or stroked with, set when it is not already */
LOCAL void set_paint( PRNPAGE *pg, CONST T_DPPAT *pat, INT ox, INT oy, BOOL stroke )
{
	if ( pat->kind == DP_PAT_TILE && pat->tile != NULL && pat->hs > 0 && pat->vs > 0 ) {
		PRNRES	*r = tile_pattern(pg, pat, ox, oy);

		if ( r != NULL ) {
			if ( ( stroke ? pg->stroke_pat : pg->fill_pat ) != r->res ) {
				pb_str(&pg->c, stroke ? "/Pattern CS /P" : "/Pattern cs /P");
				pb_int(&pg->c, r->res);
				pb_str(&pg->c, stroke ? " SCN\n" : " scn\n");
				if ( stroke ) {
					pg->stroke_pat = r->res;
					pg->stroke_set = FALSE;
				} else {
					pg->fill_pat = r->res;
					pg->fill_set = FALSE;
				}
			}
			return;
		}
	}
	{
		UW	c = ( pat->kind == DP_PAT_TILE && pat->tile != NULL ) ? pat->tile[0]
			  : tone_colour(pat);

		c &= 0x00FFFFFFU;
		if ( stroke ) {
			if ( !pg->stroke_set || pg->stroke != c || pg->stroke_pat != 0 ) {
				colour_ops(pg, c, TRUE);
				pg->stroke = c;
				pg->stroke_set = TRUE;
				pg->stroke_pat = 0;
			}
		} else {
			if ( !pg->fill_set || pg->fill != c || pg->fill_pat != 0 ) {
				colour_ops(pg, c, FALSE);
				pg->fill = c;
				pg->fill_set = TRUE;
				pg->fill_pat = 0;
			}
		}
	}
}

LOCAL void set_line( PRNPAGE *pg, INT width, UINT dash, INT cap )
{
	INT	lw = ( ( width > 0 ) ? width : 1 ) * 1000;

	if ( pg->lw != lw ) {
		pb_milli(&pg->c, lw);
		pb_str(&pg->c, " w\n");
		pg->lw = lw;
	}
	if ( pg->cap != cap ) {
		pb_int(&pg->c, cap);
		pb_str(&pg->c, " J 0 j\n");
		pg->cap = cap;
	}
	if ( pg->dash != dash ) {
		/* the screen's kinds of line, a pixel to a step */
		switch ( dash ) {
		case DP_LINE_DOT:	pb_str(&pg->c, "[1 1] 0 d\n");		break;
		case DP_LINE_DASH:	pb_str(&pg->c, "[4 4] 0 d\n");		break;
		case DP_LINE_CHAIN:	pb_str(&pg->c, "[8 3 2 3] 0 d\n");	break;
		default:		pb_str(&pg->c, "[] 0 d\n");		break;
		}
		pg->dash = dash;
	}
}

/* ---------------------------------------------------------------- shapes */

LOCAL void move_line( PBUF *b, INT x, INT y, INT mx, INT my, CONST char *op )
{
	coord(b, x, mx);
	sp(b);
	coord(b, y, my);
	sp(b);
	pb_str(b, op);
	pb_str(b, "\n");
}

/*
 * A box with rounded corners as a path of lines and quarter ellipses,
 * each quarter a Bézier curve with its handles 0.5523 of the radius
 * along the sides. All in thousandths of a pixel.
 */
LOCAL void round_path( PBUF *b, D l, D t, D r, D bt, D rx, D ry )
{
	D	kx = rx * 36195 / 65536, ky = ry * 36195 / 65536;

	pb_milli(b, l + rx);  sp(b);  pb_milli(b, t);  pb_str(b, " m\n");
	pb_milli(b, r - rx);  sp(b);  pb_milli(b, t);  pb_str(b, " l\n");
	pb_milli(b, r - rx + kx);  sp(b);  pb_milli(b, t);  sp(b);
	pb_milli(b, r);  sp(b);  pb_milli(b, t + ry - ky);  sp(b);
	pb_milli(b, r);  sp(b);  pb_milli(b, t + ry);  pb_str(b, " c\n");
	pb_milli(b, r);  sp(b);  pb_milli(b, bt - ry);  pb_str(b, " l\n");
	pb_milli(b, r);  sp(b);  pb_milli(b, bt - ry + ky);  sp(b);
	pb_milli(b, r - rx + kx);  sp(b);  pb_milli(b, bt);  sp(b);
	pb_milli(b, r - rx);  sp(b);  pb_milli(b, bt);  pb_str(b, " c\n");
	pb_milli(b, l + rx);  sp(b);  pb_milli(b, bt);  pb_str(b, " l\n");
	pb_milli(b, l + rx - kx);  sp(b);  pb_milli(b, bt);  sp(b);
	pb_milli(b, l);  sp(b);  pb_milli(b, bt - ry + ky);  sp(b);
	pb_milli(b, l);  sp(b);  pb_milli(b, bt - ry);  pb_str(b, " c\n");
	pb_milli(b, l);  sp(b);  pb_milli(b, t + ry);  pb_str(b, " l\n");
	pb_milli(b, l);  sp(b);  pb_milli(b, t + ry - ky);  sp(b);
	pb_milli(b, l + rx - kx);  sp(b);  pb_milli(b, t);  sp(b);
	pb_milli(b, l + rx);  sp(b);  pb_milli(b, t);  pb_str(b, " c h\n");
}

/*
 * Whether a box (from the origin) misses what may be touched. A page is
 * drawn as the screen draws it, the rest of the document running on
 * below its foot; what lands off the sheet is left out of the file, so
 * that the words of one page are not found again, unseen, on another.
 */
LOCAL BOOL outside( CONST T_DPRECST *st, INT l, INT t, INT r, INT b )
{
	l += st->ox;
	r += st->ox;
	t += st->oy;
	b += st->oy;
	return (BOOL)( r <= st->clip.left || l >= st->clip.right
		    || b <= st->clip.top || t >= st->clip.bottom );
}

LOCAL BOOL op_outside( CONST T_DPRECST *st, CONST T_DPRECOP *op )
{
	INT	l, t, r, b, i, w = ( op->width > 0 ) ? op->width : 1;

	if ( op->p == NULL || op->n <= 0 ) {
		return outside(st, op->r.left - w, op->r.top - w, op->r.right + w, op->r.bottom + w);
	}
	l = r = op->p[0].x;
	t = b = op->p[0].y;
	for ( i = 1; i < op->n; i++ ) {
		if ( op->p[i].x < l ) l = op->p[i].x;
		if ( op->p[i].x > r ) r = op->p[i].x;
		if ( op->p[i].y < t ) t = op->p[i].y;
		if ( op->p[i].y > b ) b = op->p[i].y;
	}
	return outside(st, l - w, t - w, r + w + 1, b + w + 1);
}

LOCAL void rec_draw( void *arg, CONST T_DPRECST *st, CONST T_DPRECOP *op,
		     CONST T_DPPAT *pat )
{
	PRNPAGE	*pg = (PRNPAGE *)arg;
	PBUF	*b = &pg->c;
	INT	ox = st->ox, oy = st->oy, i, w = ( op->width > 0 ) ? op->width : 1;

	if ( op_outside(st, op) ) {
		return;
	}
	set_clip(pg, &st->clip);
	text_end(pg);
	switch ( op->kind ) {
	case DP_REC_RECT:
		if ( op->r.right <= op->r.left || op->r.bottom <= op->r.top ) {
			return;
		}
		set_paint(pg, pat, ox, oy, FALSE);
		coord(b, op->r.left + ox, 0);
		sp(b);
		coord(b, op->r.top + oy, 0);
		sp(b);
		coord(b, op->r.right - op->r.left, 0);
		sp(b);
		coord(b, op->r.bottom - op->r.top, 0);
		pb_str(b, " re f\n");
		break;

	case DP_REC_FRAME:
		if ( op->r.right - op->r.left <= 2 * w || op->r.bottom - op->r.top <= 2 * w ) {
			/* nothing left inside: the frame is the whole box */
			T_DPRECOP	f = *op;

			f.kind = DP_REC_RECT;
			rec_draw(arg, st, &f, pat);
			return;
		}
		set_paint(pg, pat, ox, oy, TRUE);
		set_line(pg, w, DP_LINE_SOLID, 0);
		pb_milli(b, (D)( op->r.left + ox ) * 1000 + w * 500);
		sp(b);
		pb_milli(b, (D)( op->r.top + oy ) * 1000 + w * 500);
		sp(b);
		pb_milli(b, (D)( op->r.right - op->r.left - w ) * 1000);
		sp(b);
		pb_milli(b, (D)( op->r.bottom - op->r.top - w ) * 1000);
		pb_str(b, " re S\n");
		break;

	case DP_REC_LINE:
		if ( op->p[0].x == op->p[1].x && op->p[0].y == op->p[1].y ) {
			T_DPRECOP	f = *op;

			f.kind = DP_REC_RECT;
			f.r.left = op->p[0].x;
			f.r.top = op->p[0].y;
			f.r.right = f.r.left + 1;
			f.r.bottom = f.r.top + 1;
			rec_draw(arg, st, &f, pat);
			return;
		}
		set_paint(pg, pat, ox, oy, TRUE);
		set_line(pg, 1, DP_LINE_SOLID, 2);
		move_line(b, op->p[0].x + ox, op->p[0].y + oy, 500, 500, "m");
		coord(b, op->p[1].x + ox, 500);
		sp(b);
		coord(b, op->p[1].y + oy, 500);
		pb_str(b, " l S\n");
		break;

	case DP_REC_STROKE: {
		/* a square brush the width wide: centred on the pixel for an odd width */
		INT	m = ( w & 1 ) ? 500 : 0;

		if ( op->n < 2 ) {
			return;
		}
		set_paint(pg, pat, ox, oy, TRUE);
		set_line(pg, w, op->line, ( op->line == DP_LINE_SOLID ) ? 2 : 0);
		move_line(b, op->p[0].x + ox, op->p[0].y + oy, m, m, "m");
		for ( i = 1; i < op->n; i++ ) {
			move_line(b, op->p[i].x + ox, op->p[i].y + oy, m, m, "l");
		}
		pb_str(b, op->closed ? "h S\n" : "S\n");
		break;
	}

	case DP_REC_FILL:
		if ( op->n < 3 ) {
			return;
		}
		set_paint(pg, pat, ox, oy, FALSE);
		move_line(b, op->p[0].x + ox, op->p[0].y + oy, 0, 0, "m");
		for ( i = 1; i < op->n; i++ ) {
			move_line(b, op->p[i].x + ox, op->p[i].y + oy, 0, 0, "l");
		}
		pb_str(b, ( op->rule == DP_POLY_WIND ) ? "h f\n" : "h f*\n");
		break;

	case DP_REC_ROUND:
	case DP_REC_ROUNDF: {
		D	l = (D)( op->r.left + ox ) * 1000, t = (D)( op->r.top + oy ) * 1000;
		D	r = (D)( op->r.right + ox ) * 1000, bt = (D)( op->r.bottom + oy ) * 1000;
		D	rx = (D)op->rx * 1000, ry = (D)op->ry * 1000;
		D	in = (D)w * 500;

		if ( op->r.right <= op->r.left || op->r.bottom <= op->r.top ) {
			return;
		}
		if ( op->kind == DP_REC_ROUNDF ) {
			if ( op->r.right - op->r.left <= 2 * w || op->r.bottom - op->r.top <= 2 * w ) {
				T_DPRECOP	f = *op;

				f.kind = DP_REC_ROUND;
				rec_draw(arg, st, &f, pat);
				return;
			}
			l += in;
			t += in;
			r -= in;
			bt -= in;
			rx -= in;
			ry -= in;
		}
		if ( rx > ( r - l ) / 2 || op->rx == DP_ROUND_FULL ) rx = ( r - l ) / 2;
		if ( ry > ( bt - t ) / 2 || op->ry == DP_ROUND_FULL ) ry = ( bt - t ) / 2;
		if ( rx < 0 ) rx = 0;
		if ( ry < 0 ) ry = 0;
		if ( op->kind == DP_REC_ROUNDF ) {
			set_paint(pg, pat, ox, oy, TRUE);
			set_line(pg, w, DP_LINE_SOLID, 0);
		} else {
			set_paint(pg, pat, ox, oy, FALSE);
		}
		round_path(b, l, t, r, bt, rx, ry);
		pb_str(b, ( op->kind == DP_REC_ROUNDF ) ? "S\n" : "f\n");
		break;
	}

	default:
		break;
	}
}

LOCAL void rec_image( void *arg, CONST T_DPRECST *st, CONST T_DPRECIMG *im )
{
	PRNPAGE	*pg = (PRNPAGE *)arg;
	PBUF	*b = &pg->c;
	PRNRES	*r;

	if ( outside(st, im->x, im->y, im->x + im->w, im->y + im->h) ) {
		return;
	}
	set_clip(pg, &st->clip);
	text_end(pg);
	r = picture(pg->doc, im);
	if ( r == NULL ) {
		return;
	}
	if ( im->kind == DP_REC_MONO ) {
		T_DPPAT	pat;

		dp_pat_colour(&pat, im->colour);
		set_paint(pg, &pat, 0, 0, FALSE);
	}
	/* the picture's unit square onto its box, its first row at the top */
	pb_str(b, "q ");
	pb_int(b, im->w);
	pb_str(b, " 0 0 -");
	pb_int(b, im->h);
	sp(b);
	coord(b, im->x + st->ox, 0);
	sp(b);
	coord(b, im->y + st->oy + im->h, 0);
	pb_str(b, " cm /Im");
	pb_int(b, r->res);
	pb_str(b, " Do Q\n");
}

/* ---------------------------------------------------------------- letters */

LOCAL ER face_table( void *arg, UW tag, UB *buf, SZ *p_len )
{
	return fn_sfnt_table((ID)(UBINT)arg, tag, buf, p_len);
}

LOCAL PDFFACE *face_of( PRNDOC *doc, ID fid )
{
	PDFFACE	*ff;

	for ( ff = doc->faces; ff != NULL; ff = ff->next ) {
		if ( ff->key == fid ) {
			return ( ff->used != NULL ) ? ff : NULL;
		}
	}
	ff = (PDFFACE *)Kmalloc(sizeof(PDFFACE));
	if ( ff == NULL ) {
		return NULL;
	}
	if ( pdf_face_init(ff, face_table, (void *)(UBINT)fid) < E_OK ) {
		knl_memset(ff, 0, sizeof(*ff));		/* remembered as one that cannot be carried */
	} else {
		ff->res = ++doc->nface;
		ff->obj = pdf_new_obj(&doc->pdf);
	}
	ff->key = fid;
	ff->next = doc->faces;
	doc->faces = ff;

	return ( ff->used != NULL ) ? ff : NULL;
}

/* The run gathered, written as one string of glyphs */
LOCAL void run_flush( PRNPAGE *pg )
{
	PBUF	*b = &pg->c;
	INT	i, k;
	D	a;

	if ( pg->nrun == 0 ) {
		return;
	}
	for ( k = 0; k < 4; k++ ) {
		pb_fix(b, (D)pg->run_m[k] * pg->run_px);
		sp(b);
	}
	pb_fix(b, pg->run_e[0]);
	sp(b);
	pb_fix(b, pg->run_m[5]);
	pb_str(b, " Tm [<");
	a = (D)pg->run_m[0] * pg->run_px;	/* the em across, 16.16 pixels */
	for ( i = 0; i < pg->nrun; i++ ) {
		if ( i > 0 ) {
			/*
			 * How far the face would have moved, less how far the
			 * line did, in thousandths of the em: what TJ moves
			 * back by.
			 */
			D	t = 0;

			if ( a != 0 ) {
				t = (D)pg->face->adv[pg->run_g[i - 1]]
				    - ( ( (D)pg->run_e[i] - pg->run_e[i - 1] ) * 1000 ) / a;
			}
			if ( t != 0 ) {
				pb_str(b, "> ");
				pb_int(b, t);
				pb_str(b, " <");
			}
		}
		pb_hex(b, pg->run_g[i], 4);
	}
	pb_str(b, ">] TJ\n");
	pg->nrun = 0;
}

LOCAL void text_end( PRNPAGE *pg )
{
	if ( pg->in_text ) {
		run_flush(pg);
		pb_str(&pg->c, "ET\n");
		pg->in_text = FALSE;
		pg->face = NULL;
	}
}

LOCAL void rec_glyph( void *arg, CONST T_DPRECST *st, CONST T_DPRECGLY *g )
{
	PRNPAGE	*pg = (PRNPAGE *)arg;
	PBUF	*b = &pg->c;
	PDFFACE	*ff;
	W	m[6];
	INT	k;
	T_DPPAT	pat;
	INT	tr = ( g->bold != 0 ) ? 2 : 0;
	INT	gx = (INT)( g->m[4] >> 16 ), gy = (INT)( g->m[5] >> 16 );

	/* the letter's em square, however it is turned, is within two ems of its place */
	if ( outside(st, gx - 2 * g->px, gy - 2 * g->px, gx + 2 * g->px, gy + 2 * g->px) ) {
		return;
	}
	set_clip(pg, &st->clip);
	ff = face_of(pg->doc, g->fid);
	if ( ff == NULL || g->gi >= (UINT)ff->nglyph ) {
		return;
	}
	pdf_face_use(ff, g->gi, g->cp);
	for ( k = 0; k < 6; k++ ) {
		m[k] = g->m[k];
	}
	m[4] += (W)st->ox << 16;
	m[5] += (W)st->oy << 16;

	/* the letter carries on the run when nothing but its place along it differs */
	if ( pg->in_text && pg->nrun > 0 && pg->nrun < PRN_RUN_MAX && pg->face == ff
	  && pg->run_px == g->px && pg->run_m[0] == m[0] && pg->run_m[1] == 0 && m[1] == 0
	  && pg->run_m[2] == m[2] && pg->run_m[3] == m[3] && pg->run_m[5] == m[5]
	  && m[4] > pg->run_e[pg->nrun - 1] && pg->tr == tr && pg->fill_set
	  && pg->fill == ( g->colour & 0x00FFFFFFU ) && pg->fill_pat == 0
	  && ( tr == 0 || pg->tw == g->bold ) ) {
		pg->run_g[pg->nrun] = (UH)g->gi;
		pg->run_e[pg->nrun] = m[4];
		pg->nrun++;
		return;
	}
	if ( !pg->in_text ) {
		pb_str(b, "BT\n");
		pg->in_text = TRUE;
		pg->face = NULL;
		pg->tr = -1;
	}
	run_flush(pg);
	if ( pg->face != ff ) {
		pb_str(b, "/F");
		pb_int(b, ff->res);
		pb_str(b, " 1 Tf\n");
		pg->face = ff;
	}
	dp_pat_colour(&pat, g->colour);
	set_paint(pg, &pat, 0, 0, FALSE);
	if ( tr != pg->tr ) {
		pb_int(b, tr);
		pb_str(b, " Tr\n");
		pg->tr = tr;
	}
	if ( tr == 2 && pg->tw != g->bold ) {
		/* thickened: the outline stroked as well, in the same colour */
		set_paint(pg, &pat, 0, 0, TRUE);
		pb_fix(b, g->bold);
		pb_str(b, " w\n");
		pg->tw = g->bold;
		pg->lw = -1;
	} else if ( tr == 2 ) {
		set_paint(pg, &pat, 0, 0, TRUE);
	}
	for ( k = 0; k < 6; k++ ) {
		pg->run_m[k] = m[k];
	}
	pg->run_px = g->px;
	pg->run_g[0] = (UH)g->gi;
	pg->run_e[0] = m[4];
	pg->nrun = 1;
}

EXPORT CONST T_DPREC prn_rec_ops = { rec_draw, rec_image, rec_glyph };

/* ---------------------------------------------------------------- pages */

EXPORT void prn_page_begin( PRNPAGE *pg, PRNDOC *doc, INT w, INT h )
{
	knl_memset(pg, 0, sizeof(*pg));
	pg->doc = doc;
	pb_init(&pg->c);
	pg->page.right = w;
	pg->page.bottom = h;
	pg->clip = pg->page;
	forget_state(pg);
}

EXPORT void prn_page_end( PRNPAGE *pg )
{
	text_end(pg);
	if ( pg->clipped ) {
		pb_str(&pg->c, "Q\n");
		pg->clipped = FALSE;
	}
}

EXPORT void prn_doc_free( PRNDOC *doc )
{
	PDFFACE	*ff, *fn;
	PRNRES	*r, *rn;

	for ( ff = doc->faces; ff != NULL; ff = fn ) {
		fn = ff->next;
		pdf_face_free(ff);
		Kfree(ff);
	}
	for ( r = doc->imgs; r != NULL; r = rn ) {
		rn = r->next;
		Kfree(r);
	}
	for ( r = doc->pats; r != NULL; r = rn ) {
		rn = r->next;
		Kfree(r);
	}
	doc->faces = NULL;
	doc->imgs = NULL;
	doc->pats = NULL;
	pdf_free(&doc->pdf);
}
