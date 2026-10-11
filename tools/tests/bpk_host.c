/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk_host.c
 *	lib/libbpk built for the host, for tools/tests/test_bpk.py
 *
 *	  bpk_host archive ARCHIVE OUTDIR
 *	      Cuts the archive up and converts every object's TAD records
 *	      into OUTDIR/<n>_<rec>.xtad (the objects named "f0000..." after
 *	      their index), the pictures into OUTDIR/<n>_<rec>_<k>.png, and
 *	      prints a line of counts and one of each object's name.
 *	  bpk_host tad RECORD OUT
 *	      Converts one TAD record, its link records all pointing at
 *	      "f0001", into OUT (and OUT.<k>.png for its pictures).
 *	  bpk_host fax MH|MR IN W H OUT
 *	      Decodes one compressed plane into rows of (W + 7) / 8 bytes.
 *	  bpk_host totron UTF8FILE / bpk_host fromtron WORDSFILE
 *	      UTF-8 to TRON code words (little endian) and back, to stdout.
 *	  bpk_host lh5 IN OUT
 *	      Encodes IN with lh5_encode into OUT, decodes it again and
 *	      says whether it came back the same.
 *	  bpk_host rewrite ARCHIVE lh5|lh0 OUT
 *	      Writes the archive again with bpk_write from what bpk_parse
 *	      gave, reads OUT back and compares the two streams.
 *
 *	cc -O2 -I include -o bpk_host tools/tests/bpk_host.c lib/libbpk/bpk_*.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tk/typedef.h>
#include <tk/errno.h>
#include <ts/bpk.h>

LOCAL UB *load( CONST char *path, long *p_len )
{
	FILE	*f = fopen(path, "rb");
	UB	*b;
	long	n;

	if ( f == NULL ) { perror(path); exit(2); }
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc(n > 0 ? n + 1 : 1);
	if ( b == NULL || (long)fread(b, 1, n, f) != n ) { perror(path); exit(2); }
	fclose(f);
	b[n] = 0;
	*p_len = n;
	return b;
}

LOCAL void save( const char *path, const void *b, long n )
{
	FILE	*f = fopen(path, "wb");

	if ( f == NULL || (long)fwrite(b, 1, n, f) != n ) { perror(path); exit(2); }
	fclose(f);
}

/* ---------------------------------------------------------------- the converter's hooks */

typedef struct {
	BPKARC		*a;
	INT		cur;
	char		(*ids)[40];
	CONST char	*outdir;
	CONST char	*out;		/* one record: its file */
	INT		vseq, npic;
} CTX;

LOCAL const char *h_target( void *ctx, INT n )
{
	CTX	*c = ctx;
	INT	t;

	if ( c->a == NULL ) return "f0001-0000-0000-0000-000000000000";
	t = ( n >= 0 && n < c->a->file[c->cur].nlink ) ? c->a->file[c->cur].link[n] : -1;
	return ( t >= 0 ) ? c->ids[t] : NULL;
}

LOCAL void h_vid( void *ctx, char *out )
{
	CTX	*c = ctx;

	sprintf(out, "00000000-0000-0000-0000-%012d", ++c->vseq);
}

LOCAL ER h_pic( void *ctx, INT recno, INT n, const UB *png, INT len )
{
	CTX	*c = ctx;
	char	path[1024];

	if ( c->a != NULL ) snprintf(path, sizeof(path), "%s/%s_%d_%d.png", c->outdir, c->ids[c->cur], recno, n);
	else snprintf(path, sizeof(path), "%s.%d.png", c->out, n);
	save(path, png, len);
	c->npic++;
	return E_OK;
}

LOCAL INT do_archive( CONST char *path, CONST char *outdir )
{
	long	n;
	UB	*raw = load(path, &n);
	BPKARC	a;
	BPKCONV	*cv = malloc(sizeof(BPKCONV));
	BPKHOOK	hook = { h_target, h_vid, h_pic, NULL };
	CTX	c;
	char	err[256];
	UINT	i;
	INT	r, ndoc = 0, nkeep = 0;
	ER	er;

	memset(&a, 0, sizeof(a));
	er = bpk_parse(&a, raw, (UINT)n, err, sizeof(err));
	if ( er < E_OK ) {
		printf("error %d %s\n", (int)er, err);
		return 1;
	}
	memset(&c, 0, sizeof(c));
	c.a = &a;
	c.outdir = outdir;
	c.ids = calloc(a.nfiles, sizeof(*c.ids));
	for ( i = 0; i < a.nfiles; i++ ) sprintf(c.ids[i], "f%04u-0000-0000-0000-000000000000", i);
	hook.ctx = &c;
	bpk_conv_init(cv, &hook);
	for ( i = 0; i < a.nfiles; i++ ) {
		BPKFILE	*f = &a.file[i];
		INT	t = 0;

		c.cur = (INT)i;
		bpk_conv_object(cv);
		printf("object %u %s\n", i, f->name);
		for ( r = 0; r < f->nrec; r++ ) {
			BPKBUF	b;
			char	p[1024];

			if ( f->rec[r].type != BPK_RT_TAD ) continue;
			if ( !bpk_tad_is_doc(f->rec[r].data, f->rec[r].size) ) {
				nkeep++;
				continue;
			}
			bpk_buf_init(&b);
			er = bpk_tad_to_xml(cv, f->name, c.ids[i], t, f->rec[r].data, f->rec[r].size, &b);
			if ( er < E_OK ) {
				printf("error %d converting %u/%d\n", (int)er, i, r);
				return 1;
			}
			snprintf(p, sizeof(p), "%s/%s_%d.xtad", outdir, c.ids[i], t);
			save(p, b.s, b.n);
			bpk_buf_free(&b);
			t++;
			ndoc++;
		}
	}
	printf("objects %u records %d kept %d pictures %d tadseg %u\n", a.nfiles, ndoc, nkeep, c.npic, cv->nskipped);
	return 0;
}

LOCAL INT do_tad( CONST char *path, CONST char *out )
{
	long	n;
	UB	*rec = load(path, &n);
	BPKCONV	*cv = malloc(sizeof(BPKCONV));
	BPKHOOK	hook = { h_target, h_vid, h_pic, NULL };
	BPKBUF	b;
	CTX	c;
	ER	er;

	memset(&c, 0, sizeof(c));
	c.out = out;
	hook.ctx = &c;
	bpk_conv_init(cv, &hook);
	bpk_buf_init(&b);
	er = bpk_tad_to_xml(cv, "test", "f0000-0000-0000-0000-000000000000", 0, rec, (UINT)n, &b);
	save(out, b.s, b.n);
	printf("er %d pictures %d tadseg %u doc %d\n", (int)er, c.npic, cv->nskipped,
	       bpk_tad_is_doc(rec, (UINT)n) ? 1 : 0);
	return ( er < E_OK ) ? 1 : 0;
}

LOCAL INT do_fax( CONST char *mode, CONST char *in, INT w, INT h, CONST char *out )
{
	long	n;
	UB	*c = load(in, &n);
	UINT	rb = (UINT)( w + 7 ) / 8;
	UB	*o = malloc(rb * (UINT)h);
	INT	used = bpk_fax_decode(c, (UINT)n, (BOOL)( strcmp(mode, "MR") == 0 ), w, h, o, rb);

	printf("used %d of %ld\n", used, n);
	if ( used < 0 ) return 1;
	save(out, o, (long)rb * h);
	return 0;
}

/* A file encoded by lh5_encode, decoded again by lh5_decode and compared */
LOCAL INT do_lh5( CONST char *in, CONST char *out )
{
	long	n;
	UB	*raw = load(in, &n), *enc, *dec;
	UINT	room = (UINT)n + (UINT)n / 4 + 4096;
	BPKLH5E	*ze = malloc(sizeof(BPKLH5E));
	BPKLH5	*zd = malloc(sizeof(BPKLH5));
	INT	m, k;

	enc = malloc(room);
	dec = malloc((size_t)n + 1);
	m = lh5_encode(ze, raw, (UINT)n, enc, room);
	if ( m < 0 ) {
		printf("encode failed\n");
		return 1;
	}
	save(out, enc, m);
	k = lh5_decode(zd, enc, (UINT)m, dec, (UINT)n);
	printf("in %ld out %d back %d same %d\n", n, m, k, ( k == (INT)n && memcmp(dec, raw, (size_t)n) == 0 ) ? 1 : 0);
	return ( k == (INT)n && memcmp(dec, raw, (size_t)n) == 0 ) ? 0 : 1;
}

/*
 * An archive read and written again by bpk_write from what bpk_parse
 * gave -- the objects' local headers and records, the extension as it
 * was -- with LH5 or stored, into OUT; then OUT read and its stream
 * compared with the first's.
 */
LOCAL INT do_rewrite( CONST char *in, CONST char *method, CONST char *out )
{
	long		n;
	UB		*raw = load(in, &n), *w = NULL;
	BPKARC		a, b;
	BPKWRITE	wr;
	BPKWFILE	*f;
	BPKWSTAT	st;
	UINT		len = 0, i;
	char		err[256];
	ER		er;

	if ( bpk_parse(&a, raw, (UINT)n, err, sizeof(err)) < E_OK ) {
		printf("error %s\n", err);
		return 1;
	}
	f = calloc(a.nfiles, sizeof(BPKWFILE));
	for ( i = 0; i < a.nfiles; i++ ) {
		f[i].head = a.file[i].head;
		f[i].nrec = a.file[i].nrec;
		f[i].rec = a.file[i].rec;
	}
	memset(&wr, 0, sizeof(wr));
	wr.name = a.name;
	wr.time = a.head.time;
	wr.method = ( strcmp(method, "lh0") == 0 ) ? BPK_LH0 : BPK_LH5;
	wr.nfiles = (INT)a.nfiles;
	wr.file = f;
	wr.ext = a.plain;
	wr.extlen = a.extsize;
	er = bpk_write(&wr, &w, &len, &st, err, sizeof(err));
	if ( er < E_OK ) {
		printf("error %d %s\n", (int)er, err);
		return 1;
	}
	save(out, w, len);
	if ( bpk_parse(&b, w, len, err, sizeof(err)) < E_OK ) {
		printf("error reading back %s\n", err);
		return 1;
	}
	printf("files %u orig %u comp %u crc %04x was %04x same %d crcok %d filesize %u was %u\n", b.nfiles, b.origsize,
	       b.compsize, b.head.crc, a.head.crc,
	       ( a.plainlen == b.plainlen && memcmp(a.plain, b.plain, a.plainlen) == 0 ) ? 1 : 0, b.crcok ? 1 : 0,
	       b.head.filesize, a.head.filesize);
	return 0;
}

int main( int argc, char **argv )
{
	long	n;

	if ( argc == 4 && strcmp(argv[1], "lh5") == 0 ) return do_lh5(argv[2], argv[3]);
	if ( argc == 5 && strcmp(argv[1], "rewrite") == 0 ) return do_rewrite(argv[2], argv[3], argv[4]);

	if ( argc == 4 && strcmp(argv[1], "archive") == 0 ) return do_archive(argv[2], argv[3]);
	if ( argc == 4 && strcmp(argv[1], "tad") == 0 ) return do_tad(argv[2], argv[3]);
	if ( argc == 7 && strcmp(argv[1], "fax") == 0 ) return do_fax(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]), argv[6]);
	if ( argc == 3 && strcmp(argv[1], "totron") == 0 ) {
		UB	*s = load(argv[2], &n);
		UH	*w = calloc((size_t)n * 2 + 8, sizeof(UH));
		INT	k = bpk_utf8_tron((CONST char *)s, w, (INT)n * 2 + 8), i;

		for ( i = 0; i < k; i++ ) printf("%s%04x", i ? " " : "", w[i]);
		printf("\n");
		return 0;
	}
	if ( argc == 3 && strcmp(argv[1], "fromtron") == 0 ) {
		UB	*s = load(argv[2], &n);
		UH	*w = calloc((size_t)n / 2 + 1, sizeof(UH));
		char	*o = malloc((size_t)n * 4 + 8);
		long	i;

		for ( i = 0; i + 1 < n; i += 2 ) w[i / 2] = (UH)( s[i] | ( s[i + 1] << 8 ) );
		bpk_tron_utf8(w, (INT)( n / 2 ), o, (INT)n * 4 + 8);
		fputs(o, stdout);
		return 0;
	}
	fprintf(stderr, "usage: bpk_host archive|tad|fax|totron|fromtron|lh5|rewrite ...\n");
	return 2;
}
