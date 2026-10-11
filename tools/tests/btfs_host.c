/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	btfs_host.c
 *	lib/libbtfs built for the host, for tools/tests/test_btfs.py
 *
 *	  btfs_host list IMAGE
 *	      Finds the BTRON partition of a CD-ROM image (or takes the
 *	      image as the partition itself), and prints the file system and
 *	      each file: its records, their types and sizes, the CRC-32 of
 *	      each record's bytes, and where each link record goes.
 *	  btfs_host book IMAGE FID OUT
 *	      Writes the file and what it links to as one archive into OUT,
 *	      and prints each 実行機能付箋 the archive's reader finds in it.
 *	  btfs_host iso IMAGE [PATH]
 *	      Prints the entries of a directory of the ISO 9660 side (the
 *	      root without PATH), or the bytes of the file PATH names.
 *
 *	cc -O2 -I include -o btfs_host tools/tests/btfs_host.c lib/libbtfs/btfs.c lib/libbpk/bpk_*.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tk/typedef.h>
#include <tk/errno.h>
#include <ts/btfs.h>

LOCAL FILE	*img;

LOCAL ER rd( void *ctx, D off, void *buf, UINT len )
{
	(void)ctx;
	if ( fseek(img, (long)off, SEEK_SET) != 0 || fread(buf, 1, len, img) != len ) return E_IO;
	return E_OK;
}

LOCAL UINT crc32( const UB *p, UINT n )
{
	UINT	c = 0xFFFFFFFFU, i;
	INT	k;

	for ( i = 0; i < n; i++ ) {
		c ^= p[i];
		for ( k = 0; k < 8; k++ ) c = ( c & 1 ) ? ( c >> 1 ) ^ 0xEDB88320U : c >> 1;
	}
	return c ^ 0xFFFFFFFFU;
}

LOCAL INT open_fs( CONST char *path, BTFS *fs )
{
	D	off = 0, len = 0;
	BOOL	boot = FALSE;
	ER	er;

	img = fopen(path, "rb");
	if ( img == NULL ) { perror(path); return 2; }
	if ( btfs_cd_find(rd, NULL, &off, &len, &boot) >= E_OK ) {
		printf("cd partition %lld %lld boot %d\n", (long long)off, (long long)len, boot ? 1 : 0);
	} else {
		off = 0;
		len = 0;
	}
	er = btfs_open(fs, rd, NULL, off, len);
	if ( er < E_OK ) {
		printf("open error %d\n", (int)er);
		return 1;
	}
	printf("fs %s locat %s bsize %u le %d format %04x nfmax %u nlb %u base %lld fidhigh %d\n", fs->uname, fs->ulocat,
	       fs->bsize, fs->le ? 1 : 0, fs->format, fs->nfmax, fs->nlb, (long long)fs->base, fs->fidhigh ? 1 : 0);
	return 0;
}

LOCAL INT do_list( CONST char *path )
{
	BTFS	fs;
	UINT	fid;
	INT	r, rc = open_fs(path, &fs);

	if ( rc != 0 ) return rc;
	for ( fid = 0; fid < fs.nfmax; fid++ ) {
		BTFSFILE	f;
		ER		er;

		if ( !btfs_fid_used(&fs, fid) ) continue;
		er = btfs_file(&fs, fid, &f);
		if ( er < E_OK ) {
			printf("file %u error %d\n", fid, (int)er);
			continue;
		}
		if ( f.islink ) {
			printf("file %u link %s target %d fs %s\n", fid, f.uname, f.target, f.tfsname);
			btfs_file_free(&f);
			continue;
		}
		printf("file %u name %s ftype %04x atype %u level %d size %u refs %u records %d links %d ctime %u\n",
		       fid, f.uname, f.ftype, f.atype, f.idxlv, f.size, f.refcnt, f.nrec, f.nlink, f.ctime);
		for ( r = 0; r < f.nrec; r++ ) {
			BTFSREC	*q = &f.rec[r];

			if ( q->type == 0 ) {
				printf(" rec %d link %d to %d\n", r, q->lfid, btfs_link_target(&fs, q));
			} else {
				UB	*b = malloc(q->size ? q->size : 1);

				er = btfs_rec_read(&fs, q, b);
				printf(" rec %d type %u sub %u size %u off %u runs %d crc %08x er %d\n", r, q->type, q->sub,
				       q->size, q->off, q->nrun, crc32(b, q->size), (int)er);
				free(b);
			}
		}
		btfs_file_free(&f);
	}
	btfs_close(&fs);
	return 0;
}

LOCAL INT do_book( CONST char *path, UINT fid, CONST char *out )
{
	BTFS	fs;
	UB	*b = NULL;
	UINT	len = 0;
	INT	n = 0, rc = open_fs(path, &fs);
	char	err[200];
	FILE	*o;
	ER	er;

	if ( rc != 0 ) return rc;
	err[0] = 0;
	er = btfs_book(&fs, fid, &b, &len, &n, err, sizeof(err));
	printf("book er %d files %d bytes %u %s\n", (int)er, n, len, err);
	if ( er < E_OK ) return 1;
	{
		/* each file's 実行機能付箋 as the archive's reader takes it */
		BPKARC	arc;
		INT	i, k;

		if ( bpk_parse(&arc, b, len, err, sizeof(err)) >= E_OK ) {
			for ( i = 0; i < (INT)arc.nfiles; i++ ) {
				for ( k = 0; k < arc.file[i].nexec; k++ ) {
					BPKEXEC	*e = &arc.file[i].exec[k];

					printf("exec %d %04x-%04x-%04x win %d %d %d %d\n", i, e->appl[0], e->appl[1],
					       e->appl[2], e->wl, e->wt, e->wr, e->wb);
				}
			}
			bpk_free(&arc);
		}
	}
	o = fopen(out, "wb");
	if ( o == NULL || fwrite(b, 1, len, o) != len ) { perror(out); return 2; }
	fclose(o);
	free(b);
	btfs_close(&fs);
	return 0;
}

LOCAL INT iso_put( void *arg, const BTFS_ISOENT *e )
{
	(void)arg;
	printf("%s %s %u %u%s\n", e->dir ? "dir" : "file", e->name, e->extent, e->size, e->joliet ? " joliet" : "");
	return 0;
}

LOCAL INT do_iso( CONST char *path, CONST char *name )
{
	BTFS_ISOENT	e;
	ER		er;

	img = fopen(path, "rb");
	if ( img == NULL ) { perror(path); return 2; }
	er = btfs_iso_find(rd, NULL, name, &e);
	if ( er < E_OK ) {
		printf("iso er %d\n", (INT)er);
		return 1;
	}
	if ( e.dir ) {
		er = btfs_iso_list(rd, NULL, &e, iso_put, NULL);
		printf("iso list er %d\n", (INT)er);
	} else {
		UB	*b = malloc(e.size + 1);

		if ( b == NULL || rd(NULL, (D)e.extent * BTFS_CD_BLOCK, b, e.size) < E_OK ) return 1;
		fwrite(b, 1, e.size, stdout);
		free(b);
	}
	return ( er < E_OK ) ? 1 : 0;
}

int main( int argc, char **argv )
{
	if ( ( argc == 3 || argc == 4 ) && strcmp(argv[1], "iso") == 0 ) return do_iso(argv[2], ( argc == 4 ) ? argv[3] : "");
	if ( argc == 3 && strcmp(argv[1], "list") == 0 ) return do_list(argv[2]);
	if ( argc == 5 && strcmp(argv[1], "book") == 0 ) return do_book(argv[2], (UINT)atoi(argv[3]), argv[4]);
	fprintf(stderr, "usage: btfs_host list IMAGE | book IMAGE FID OUT\n");
	return 2;
}
