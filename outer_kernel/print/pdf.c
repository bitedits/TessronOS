/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	pdf.c
 *	The bytes of a PDF file and the numbers of its objects (design 11.22)
 *
 *	A file is written from the front: each object as it becomes ready,
 *	its place noted against its number, and at the end the table of
 *	those places and the trailer. A number may be given out before its
 *	object is written -- a page names the face it uses long before the
 *	face can be cut down to its letters -- so the table is filled in by
 *	number, not in the order the objects were written.
 *
 *	The bytes are kept in pieces of 64KB rather than one block that
 *	grows: a file with pictures in it reaches megabytes, and a block of
 *	that size moved on every growth is time and memory spent for
 *	nothing.
 */

#include <sys/machine.h>
#include "kernel.h"
#include <tk/tkernel.h>
#include <ts/img.h>
#include "pdf.h"

/* ---------------------------------------------------------------- bytes */

EXPORT void pb_init( PBUF *b )
{
	b->p = NULL;
	b->n = 0;
	b->cap = 0;
	b->fail = FALSE;
}

EXPORT void pb_free( PBUF *b )
{
	if ( b->p != NULL ) {
		Kfree(b->p);
	}
	pb_init(b);
}

EXPORT void pb_bytes( PBUF *b, CONST void *s, SZ n )
{
	if ( b->fail || n <= 0 ) {
		return;
	}
	if ( b->n + n > b->cap ) {
		SZ	nc = b->cap * 2 + n + 256;
		UB	*np = (UB *)Krealloc(b->p, nc);

		if ( np == NULL ) {
			b->fail = TRUE;
			return;
		}
		b->p = np;
		b->cap = nc;
	}
	knl_memcpy(b->p + b->n, s, n);
	b->n += n;
}

EXPORT void pb_str( PBUF *b, CONST char *s )
{
	SZ	n = 0;

	while ( s[n] != 0 ) {
		n++;
	}
	pb_bytes(b, s, n);
}

EXPORT void pb_int( PBUF *b, D v )
{
	char	t[24];
	INT	n = 0, k = 0;
	char	o[24];
	UD	u;

	if ( v < 0 ) {
		o[k++] = '-';
		u = (UD)( -v );
	} else {
		u = (UD)v;
	}
	do {
		t[n++] = (char)( '0' + (INT)( u % 10 ) );
		u /= 10;
	} while ( u > 0 && n < 22 );
	while ( n > 0 ) {
		o[k++] = t[--n];
	}
	pb_bytes(b, o, k);
}

EXPORT void pb_milli( PBUF *b, D v )
{
	UD	u;
	INT	frac;
	char	t[4];
	INT	n = 3;

	if ( v < 0 ) {
		pb_bytes(b, "-", 1);
		u = (UD)( -v );
	} else {
		u = (UD)v;
	}
	pb_int(b, (D)( u / 1000 ));
	frac = (INT)( u % 1000 );
	if ( frac == 0 ) {
		return;
	}
	t[0] = (char)( '0' + frac / 100 );
	t[1] = (char)( '0' + ( frac / 10 ) % 10 );
	t[2] = (char)( '0' + frac % 10 );
	while ( n > 0 && t[n - 1] == '0' ) {
		n--;
	}
	pb_bytes(b, ".", 1);
	pb_bytes(b, t, n);
}

EXPORT void pb_fix( PBUF *b, D v )
{
	/* thousandths, rounded half away from nought */
	D	m = ( v >= 0 ) ? ( v * 1000 + 32768 ) >> 16 : -( ( -v * 1000 + 32768 ) >> 16 );

	pb_milli(b, m);
}

EXPORT void pb_hex( PBUF *b, UW v, INT digits )
{
	CONST char	*hx = "0123456789ABCDEF";
	char		t[8];
	INT		i;

	for ( i = digits - 1; i >= 0; i-- ) {
		t[i] = hx[v & 15];
		v >>= 4;
	}
	pb_bytes(b, t, digits);
}

EXPORT void pb_name( PBUF *b, CONST UB *s )
{
	pb_bytes(b, "/", 1);
	for ( ; *s != 0; s++ ) {
		UB	c = *s;

		if ( c < 0x21 || c > 0x7E || c == '#' || c == '/' || c == '(' || c == ')'
		  || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}'
		  || c == '%' ) {
			pb_bytes(b, "#", 1);
			pb_hex(b, c, 2);
		} else {
			pb_bytes(b, &c, 1);
		}
	}
}

EXPORT UW pdf_utf8( CONST UB *s, INT *p_n )
{
	UW	c = s[0];
	INT	n = 1, need = 0, i;

	if ( c >= 0xF0 && c < 0xF8 ) {
		c &= 0x07;
		need = 3;
	} else if ( c >= 0xE0 ) {
		c &= 0x0F;
		need = 2;
	} else if ( c >= 0xC0 ) {
		c &= 0x1F;
		need = 1;
	} else if ( c >= 0x80 ) {
		c = 0xFFFD;
	}
	for ( i = 0; i < need; i++ ) {
		if ( ( s[n] & 0xC0 ) != 0x80 ) {
			c = 0xFFFD;
			break;
		}
		c = ( c << 6 ) | ( s[n] & 0x3F );
		n++;
	}
	*p_n = n;

	return c;
}

EXPORT void pb_text16( PBUF *b, CONST UB *s )
{
	INT	n;

	pb_str(b, "<FEFF");
	while ( s != NULL && *s != 0 ) {
		UW	c = pdf_utf8(s, &n);

		s += n;
		if ( c >= 0x10000 ) {
			c -= 0x10000;
			pb_hex(b, 0xD800 | ( c >> 10 ), 4);
			pb_hex(b, 0xDC00 | ( c & 0x3FF ), 4);
		} else {
			pb_hex(b, c, 4);
		}
	}
	pb_str(b, ">");
}

/* ---------------------------------------------------------------- the file */

EXPORT ER pdf_open( PDF *f )
{
	knl_memset(f, 0, sizeof(*f));
	f->cap = 256;
	f->off = (SZ *)Kmalloc(sizeof(SZ) * (SZ)f->cap);
	if ( f->off == NULL ) {
		return E_NOMEM;
	}
	f->off[0] = 0;
	f->nobj = 1;				/* object 0 is the head of the free list */
	/* the head, and a line of high bytes that says the file is not text */
	pdf_puts(f, "%PDF-1.7\n%\xE2\xE3\xCF\xD3\n");

	return f->fail ? E_NOMEM : E_OK;
}

EXPORT void pdf_free( PDF *f )
{
	PDFCHUNK	*c, *n;

	for ( c = f->head; c != NULL; c = n ) {
		n = c->next;
		Kfree(c);
	}
	if ( f->off != NULL ) {
		Kfree(f->off);
	}
	knl_memset(f, 0, sizeof(*f));
}

EXPORT void pdf_write( PDF *f, CONST void *s, SZ n )
{
	CONST UB	*p = (CONST UB *)s;

	while ( n > 0 && !f->fail ) {
		SZ	k;

		if ( f->tail == NULL || f->tail->n == PDF_CHUNK ) {
			PDFCHUNK	*c = (PDFCHUNK *)Kmalloc(sizeof(PDFCHUNK));

			if ( c == NULL ) {
				f->fail = TRUE;
				return;
			}
			c->next = NULL;
			c->n = 0;
			if ( f->tail != NULL ) {
				f->tail->next = c;
			} else {
				f->head = c;
			}
			f->tail = c;
		}
		k = PDF_CHUNK - f->tail->n;
		if ( k > n ) {
			k = n;
		}
		knl_memcpy(f->tail->b + f->tail->n, p, k);
		f->tail->n += k;
		f->size += k;
		p += k;
		n -= k;
	}
}

EXPORT void pdf_puts( PDF *f, CONST char *s )
{
	SZ	n = 0;

	while ( s[n] != 0 ) {
		n++;
	}
	pdf_write(f, s, n);
}

EXPORT void pdf_buf( PDF *f, CONST PBUF *b )
{
	if ( b->fail ) {
		f->fail = TRUE;
		return;
	}
	pdf_write(f, b->p, b->n);
}

EXPORT INT pdf_new_obj( PDF *f )
{
	if ( f->nobj >= f->cap ) {
		INT	nc = f->cap * 2;
		SZ	*no = (SZ *)Krealloc(f->off, sizeof(SZ) * (SZ)nc);

		if ( no == NULL ) {
			f->fail = TRUE;
			return 0;
		}
		f->off = no;
		f->cap = nc;
	}
	f->off[f->nobj] = -1;			/* not written yet */

	return f->nobj++;
}

EXPORT void pdf_begin_obj( PDF *f, INT num )
{
	PBUF	b;

	if ( num <= 0 || num >= f->nobj ) {
		f->fail = TRUE;
		return;
	}
	f->off[num] = f->size;
	pb_init(&b);
	pb_int(&b, num);
	pb_str(&b, " 0 obj\n");
	pdf_buf(f, &b);
	pb_free(&b);
}

EXPORT void pdf_end_obj( PDF *f )
{
	pdf_puts(f, "\nendobj\n");
}

EXPORT ER pdf_stream_buf( PDF *f, INT num, CONST PBUF *dict, CONST UB *data, SZ len,
			  BOOL pack, INT level )
{
	UB	*z = NULL;
	SZ	zlen = 0;
	PBUF	b;

	if ( pack && len > 0 ) {
		if ( ts_zlib_deflate(data, len, &z, &zlen, level) < E_OK ) {
			z = NULL;		/* written as it is rather than not at all */
		}
	}
	pdf_begin_obj(f, num);
	pb_init(&b);
	pb_str(&b, "<<");
	if ( dict != NULL && dict->n > 0 ) {
		pb_bytes(&b, dict->p, dict->n);
	}
	if ( z != NULL ) {
		pb_str(&b, " /Filter /FlateDecode");
	}
	pb_str(&b, " /Length ");
	pb_int(&b, (D)( ( z != NULL ) ? zlen : len ));
	pb_str(&b, " >>\nstream\n");
	pdf_buf(f, &b);
	pb_free(&b);
	if ( z != NULL ) {
		pdf_write(f, z, zlen);
		Kfree(z);
	} else if ( len > 0 ) {
		pdf_write(f, data, len);
	}
	pdf_puts(f, "\nendstream");
	pdf_end_obj(f);

	return f->fail ? E_NOMEM : E_OK;
}

EXPORT ER pdf_stream( PDF *f, INT num, CONST char *dict, CONST UB *data, SZ len,
		      BOOL pack, INT level )
{
	PBUF	b;
	ER	er;

	pb_init(&b);
	if ( dict != NULL ) {
		pb_str(&b, " ");
		pb_str(&b, dict);
	}
	er = pdf_stream_buf(f, num, &b, data, len, pack, level);
	pb_free(&b);

	return er;
}

EXPORT ER pdf_close( PDF *f, INT root, INT info )
{
	PBUF	b;
	SZ	xref = f->size;
	INT	i;

	pb_init(&b);
	pb_str(&b, "xref\n0 ");
	pb_int(&b, f->nobj);
	pb_str(&b, "\n0000000000 65535 f \n");
	for ( i = 1; i < f->nobj; i++ ) {
		UD	o = ( f->off[i] >= 0 ) ? (UD)f->off[i] : 0;
		char	t[21];
		INT	k;

		if ( f->off[i] < 0 ) {
			/* given a number and never written: a free entry */
			pb_str(&b, "0000000000 00001 f \n");
			continue;
		}
		for ( k = 9; k >= 0; k-- ) {
			t[k] = (char)( '0' + (INT)( o % 10 ) );
			o /= 10;
		}
		pb_bytes(&b, t, 10);
		pb_str(&b, " 00000 n \n");
	}
	pb_str(&b, "trailer\n<< /Size ");
	pb_int(&b, f->nobj);
	pb_str(&b, " /Root ");
	pb_int(&b, root);
	pb_str(&b, " 0 R");
	if ( info > 0 ) {
		pb_str(&b, " /Info ");
		pb_int(&b, info);
		pb_str(&b, " 0 R");
	}
	pb_str(&b, " >>\nstartxref\n");
	pb_int(&b, (D)xref);
	pb_str(&b, "\n%%EOF\n");
	pdf_buf(f, &b);
	pb_free(&b);

	return f->fail ? E_NOMEM : E_OK;
}

EXPORT void pdf_rewind( PDF *f )
{
	f->rd = f->head;
	f->rd_at = 0;
}

EXPORT INT pdf_read( PDF *f, void *buf, SZ n )
{
	UB	*o = (UB *)buf;
	SZ	got = 0;

	while ( got < n && f->rd != NULL ) {
		SZ	k = f->rd->n - f->rd_at;

		if ( k <= 0 ) {
			f->rd = f->rd->next;
			f->rd_at = 0;
			continue;
		}
		if ( k > n - got ) {
			k = n - got;
		}
		knl_memcpy(o + got, f->rd->b + f->rd_at, k);
		f->rd_at += k;
		got += k;
	}

	return (INT)got;
}
