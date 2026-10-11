/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	btfs.c
 *	The BTRON file system read from a disk image (design 11.9)
 *
 *	Only reading: the system header, the file ID table, each file's
 *	header and record indexes at any index level, and the records' bytes
 *	through their runs of blocks. A file and what it links to are then
 *	made one stored BTRON archive, so that they are taken out as an
 *	archive is, by the same code.
 *
 *	The layouts are in include/ts/btfs.h.
 */

#include <ts/btfs.h>
#include <stdlib.h>
#include <string.h>

#define SCAN_MAX	( 64 * 1024 )	/* the system header looked for in this much */
#define SCAN_STEP	512

/* ---------------------------------------------------------------- numbers, in either order */

LOCAL UINT g16( const BTFS *fs, const UB *p )
{
	return fs->le ? ( (UINT)p[0] | ( (UINT)p[1] << 8 ) ) : ( ( (UINT)p[0] << 8 ) | (UINT)p[1] );
}

LOCAL UINT g24( const BTFS *fs, const UB *p )
{
	return fs->le ? ( (UINT)p[0] | ( (UINT)p[1] << 8 ) | ( (UINT)p[2] << 16 ) )
		      : ( ( (UINT)p[0] << 16 ) | ( (UINT)p[1] << 8 ) | (UINT)p[2] );
}

LOCAL UINT g32( const BTFS *fs, const UB *p )
{
	return fs->le ? ( g16(fs, p) | ( g16(fs, p + 2) << 16 ) ) : ( ( g16(fs, p) << 16 ) | g16(fs, p + 2) );
}

LOCAL void wr16( UB *p, UINT v )
{
	p[0] = (UB)v;
	p[1] = (UB)( v >> 8 );
}

/* A name of TRON characters, as the file system keeps it, as words and as UTF-8 */
LOCAL void get_name( const BTFS *fs, const UB *p, UH *name, char *out, INT max )
{
	INT	i;

	for ( i = 0; i < BTFS_NAMELEN; i++ ) name[i] = (UH)g16(fs, p + 2 * i);
	bpk_tron_utf8(name, BTFS_NAMELEN, out, max);
}

LOCAL BOOL same_name( const UH *a, const UH *b )
{
	INT	i;

	for ( i = 0; i < BTFS_NAMELEN; i++ ) {
		if ( a[i] != b[i] ) return FALSE;
		if ( a[i] == 0 ) break;
	}
	return TRUE;
}

/* ---------------------------------------------------------------- the CD-ROM */

EXPORT ER btfs_cd_find( BTFS_READ rd, void *ctx, D *p_off, D *p_len, BOOL *p_boot )
{
	UB	*b = malloc(BTFS_CD_BLOCK);
	INT	i;
	ER	er = E_NOEXS;

	if ( b == NULL ) return E_NOMEM;
	for ( i = 0; i < BTFS_CD_VDMAX; i++ ) {
		UINT	loc, sz;

		if ( rd(ctx, (D)( BTFS_CD_VDSTART + i ) * BTFS_CD_BLOCK, b, BTFS_CD_BLOCK) < E_OK ) break;
		if ( memcmp(b + CDVPD_STDID, "CD001", 5) != 0 || b[CDVPD_TYPE] == CDVPD_T_END ) break;
		if ( b[CDVPD_TYPE] != CDVPD_T_VPD
		  || memcmp(b + CDVPD_SYSID, CDVPD_SYSID_BTRON, sizeof(CDVPD_SYSID_BTRON) - 1) != 0 ) continue;
		/* both orders are written; the low byte first is read, the other when it is 0 */
		loc = (UINT)b[CDVPD_VOLLOC] | ( (UINT)b[CDVPD_VOLLOC + 1] << 8 ) | ( (UINT)b[CDVPD_VOLLOC + 2] << 16 )
		    | ( (UINT)b[CDVPD_VOLLOC + 3] << 24 );
		if ( loc == 0 ) {
			loc = ( (UINT)b[CDVPD_VOLLOC + 4] << 24 ) | ( (UINT)b[CDVPD_VOLLOC + 5] << 16 )
			    | ( (UINT)b[CDVPD_VOLLOC + 6] << 8 ) | (UINT)b[CDVPD_VOLLOC + 7];
		}
		sz = (UINT)b[CDVPD_VOLSZ] | ( (UINT)b[CDVPD_VOLSZ + 1] << 8 ) | ( (UINT)b[CDVPD_VOLSZ + 2] << 16 )
		   | ( (UINT)b[CDVPD_VOLSZ + 3] << 24 );
		if ( sz == 0 ) {
			sz = ( (UINT)b[CDVPD_VOLSZ + 4] << 24 ) | ( (UINT)b[CDVPD_VOLSZ + 5] << 16 )
			   | ( (UINT)b[CDVPD_VOLSZ + 6] << 8 ) | (UINT)b[CDVPD_VOLSZ + 7];
		}
		if ( loc == 0 ) continue;
		*p_off = (D)loc * BTFS_CD_BLOCK;
		*p_len = (D)sz * BTFS_CD_BLOCK;
		if ( p_boot != NULL ) {
			*p_boot = (BOOL)( memcmp(b + CDVPD_VOLID, CDVPD_VOLID_BOOT, sizeof(CDVPD_VOLID_BOOT) - 1) == 0 );
		}
		er = E_OK;
		break;
	}
	free(b);
	return er;
}

/* ---------------------------------------------------------------- the ISO 9660 side */

LOCAL UINT le32( const UB *p )
{
	return (UINT)p[0] | ( (UINT)p[1] << 8 ) | ( (UINT)p[2] << 16 ) | ( (UINT)p[3] << 24 );
}

/* One UCS character as UTF-8 at o (room for 4 bytes); how many bytes */
LOCAL INT utf8_put( char *o, UINT c )
{
	if ( c < 0x80 ) {
		o[0] = (char)c;
		return 1;
	}
	if ( c < 0x800 ) {
		o[0] = (char)( 0xC0 | ( c >> 6 ) );
		o[1] = (char)( 0x80 | ( c & 0x3F ) );
		return 2;
	}
	if ( c < 0x10000 ) {
		o[0] = (char)( 0xE0 | ( c >> 12 ) );
		o[1] = (char)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
		o[2] = (char)( 0x80 | ( c & 0x3F ) );
		return 3;
	}
	o[0] = (char)( 0xF0 | ( c >> 18 ) );
	o[1] = (char)( 0x80 | ( ( c >> 12 ) & 0x3F ) );
	o[2] = (char)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
	o[3] = (char)( 0x80 | ( c & 0x3F ) );
	return 4;
}

/*
 * A directory record's name as UTF-8: Joliet's in UCS-2 with the high
 * byte first, else the bytes as they are; the version (";1") and a dot
 * left at the end taken off.
 */
LOCAL void iso_name( const UB *p, INT n, BOOL joliet, char *out, INT max )
{
	INT	i, k = 0;

	if ( joliet ) {
		for ( i = 0; i + 1 < n && k < max - 5; i += 2 ) {
			UINT	c = ( (UINT)p[i] << 8 ) | p[i + 1];

			if ( c >= 0xD800 && c < 0xDC00 && i + 3 < n ) {
				UINT	lo = ( (UINT)p[i + 2] << 8 ) | p[i + 3];

				if ( lo >= 0xDC00 && lo < 0xE000 ) {
					c = 0x10000 + ( ( c - 0xD800 ) << 10 ) + ( lo - 0xDC00 );
					i += 2;
				}
			}
			k += utf8_put(out + k, c);
		}
	} else {
		for ( i = 0; i < n && k < max - 1; i++ ) out[k++] = (char)p[i];
	}
	out[k] = 0;
	for ( i = 0; i < k; i++ ) {
		if ( out[i] == ';' ) {
			out[i] = 0;
			k = i;
			break;
		}
	}
	if ( k > 1 && out[k - 1] == '.' ) out[k - 1] = 0;
}

/* The directory record at p (its length byte first) as an entry */
LOCAL BOOL iso_rec( const UB *p, BOOL joliet, BTFS_ISOENT *e )
{
	INT	len = p[0], nl = p[32];

	if ( len < 34 || 33 + nl > len ) return FALSE;
	memset(e, 0, sizeof(*e));
	e->extent = le32(p + 2);
	e->size = le32(p + 10);
	memcpy(e->date, p + 18, 7);
	e->dir = (BOOL)( ( p[25] & 0x02 ) != 0 );
	e->hidden = (BOOL)( ( p[25] & 0x01 ) != 0 );
	e->joliet = joliet;
	if ( nl == 1 && ( p[33] == 0 || p[33] == 1 ) ) {
		e->name[0] = ( p[33] == 0 ) ? '.' : 0;		/* itself, or the one above */
		return TRUE;
	}
	iso_name(p + 33, nl, joliet, e->name, sizeof(e->name));
	return TRUE;
}

EXPORT ER btfs_iso_root( BTFS_READ rd, void *ctx, BTFS_ISOENT *root )
{
	UB	*b = malloc(BTFS_CD_BLOCK);
	INT	i;
	BOOL	got = FALSE;
	ER	er = E_NOEXS;

	if ( b == NULL ) return E_NOMEM;
	for ( i = 0; i < BTFS_CD_VDMAX; i++ ) {
		if ( rd(ctx, (D)( BTFS_CD_VDSTART + i ) * BTFS_CD_BLOCK, b, BTFS_CD_BLOCK) < E_OK ) break;
		if ( memcmp(b + CDVPD_STDID, "CD001", 5) != 0 || b[CDVPD_TYPE] == CDVPD_T_END ) break;
		if ( b[CDVPD_TYPE] == 1 && !got ) {
			/* the primary volume descriptor: its root's record at byte 156 */
			if ( iso_rec(b + 156, FALSE, root) ) {
				got = TRUE;
				er = E_OK;
			}
		} else if ( b[CDVPD_TYPE] == 2 && b[88] == 0x25 && b[89] == 0x2F
			    && ( b[90] == 0x40 || b[90] == 0x43 || b[90] == 0x45 ) ) {
			/* a supplementary one of Joliet (UCS-2, levels 1 to 3): its names are taken */
			if ( iso_rec(b + 156, TRUE, root) ) {
				er = E_OK;
				break;
			}
		}
	}
	free(b);
	if ( er >= E_OK ) {
		root->name[0] = 0;
		root->dir = TRUE;
	}
	return er;
}

EXPORT ER btfs_iso_list( BTFS_READ rd, void *ctx, const BTFS_ISOENT *dir, BTFS_ISOPUT put, void *arg )
{
	UB	*b;
	UINT	at = 0;
	ER	er = E_OK;

	if ( !dir->dir ) return E_PAR;
	if ( dir->size > 16 * 1024 * 1024 ) return E_OBJ;
	b = malloc(BTFS_CD_BLOCK);
	if ( b == NULL ) return E_NOMEM;
	while ( at < dir->size && er >= E_OK ) {
		UINT	in = 0;

		er = rd(ctx, (D)dir->extent * BTFS_CD_BLOCK + at, b, BTFS_CD_BLOCK);
		if ( er < E_OK ) break;
		/* records do not cross a block: a length of nought ends the block's */
		while ( in < BTFS_CD_BLOCK && b[in] != 0 ) {
			BTFS_ISOENT	e;

			if ( in + b[in] > BTFS_CD_BLOCK ) {
				er = E_OBJ;
				break;
			}
			if ( iso_rec(b + in, dir->joliet, &e) && e.name[0] != 0 && !( e.name[0] == '.' && e.name[1] == 0 )
			  && ( b[in + 25] & 0x04 ) == 0 ) {
				if ( put(arg, &e) < 0 ) {
					free(b);
					return E_OK;
				}
			}
			in += b[in];
		}
		at += BTFS_CD_BLOCK;
	}
	free(b);
	return er;
}

/* What btfs_iso_find looks for in one directory */
typedef struct {
	const char	*name;
	INT		len;
	BTFS_ISOENT	*e;
	BOOL		found;
} ISOWANT;

LOCAL INT iso_want( void *arg, const BTFS_ISOENT *e )
{
	ISOWANT	*w = (ISOWANT *)arg;
	INT	i;

	for ( i = 0; i < w->len; i++ ) {
		char	a = e->name[i], b = w->name[i];

		if ( a >= 'a' && a <= 'z' ) a = (char)( a - 'a' + 'A' );
		if ( b >= 'a' && b <= 'z' ) b = (char)( b - 'a' + 'A' );
		if ( a != b ) return 0;
	}
	if ( e->name[w->len] != 0 ) return 0;
	*w->e = *e;
	w->found = TRUE;
	return -1;
}

EXPORT ER btfs_iso_find( BTFS_READ rd, void *ctx, const char *path, BTFS_ISOENT *e )
{
	BTFS_ISOENT	cur;
	ISOWANT		w;
	ER		er;

	er = btfs_iso_root(rd, ctx, &cur);
	if ( er < E_OK ) return er;
	while ( *path == '/' ) path++;
	while ( *path != 0 ) {
		INT	n;

		for ( n = 0; path[n] != 0 && path[n] != '/'; n++ ) ;
		w.name = path;
		w.len = n;
		w.e = e;
		w.found = FALSE;
		er = btfs_iso_list(rd, ctx, &cur, iso_want, &w);
		if ( er < E_OK ) return er;
		if ( !w.found ) return E_NOEXS;
		cur = *e;
		path += n;
		while ( *path == '/' ) path++;
		if ( *path != 0 && !cur.dir ) return E_NOEXS;
	}
	*e = cur;
	return E_OK;
}

/* ---------------------------------------------------------------- blocks */

LOCAL ER read_at( BTFS *fs, D off, void *buf, UINT len )
{
	if ( fs->plen > 0 && ( off < fs->part || off + len > fs->part + fs->plen ) ) return E_OBJ;
	return fs->rd(fs->ctx, off, buf, len);
}

/* n logical blocks from block blk into buf */
LOCAL ER read_blk( BTFS *fs, UINT blk, UINT n, void *buf )
{
	if ( blk == 0 || ( fs->nlb > 0 && ( blk >= fs->nlb || n > fs->nlb - blk ) ) ) return E_OBJ;
	return read_at(fs, fs->base + (D)blk * fs->bsize, buf, n * fs->bsize);
}

/* A file ID's entry: its header's block and its reference count */
LOCAL void fid_entry( const BTFS *fs, UINT fid, UINT *p_blk, UINT *p_ref )
{
	const UB	*e = fs->fidt + 4 * fid;

	if ( fs->fidhigh ) {
		*p_blk = (UINT)e[0] | ( (UINT)e[1] << 8 ) | ( (UINT)e[2] << 16 );
		*p_ref = e[3];
	} else {
		*p_blk = g24(fs, e + 1);
		*p_ref = e[0];
	}
}

EXPORT BOOL btfs_fid_used( BTFS *fs, UINT fid )
{
	UINT	blk, ref;

	if ( fs->fidt == NULL || fid >= fs->nfmax ) return FALSE;
	fid_entry(fs, fid, &blk, &ref);
	return (BOOL)( blk != 0 );
}

/* Whether block blk holds the header of file fid */
LOCAL BOOL is_header( BTFS *fs, UINT blk, UINT fid )
{
	if ( read_blk(fs, blk, 1, fs->blk) < E_OK ) return FALSE;
	return (BOOL)( g32(fs, fs->blk + FH_STARTID) == BTFS_STARTID && g16(fs, fs->blk + FH_FID) == fid );
}

/* ---------------------------------------------------------------- the file system */

EXPORT ER btfs_open( BTFS *fs, BTFS_READ rd, void *ctx, D off, D len )
{
	UB	h[BTFS_SYSHEAD];
	UINT	k, bytes, blk, ref;
	BOOL	found = FALSE;

	memset(fs, 0, sizeof(*fs));
	fs->rd = rd;
	fs->ctx = ctx;
	fs->part = off;
	fs->plen = len;
	for ( k = SCAN_STEP; k < SCAN_MAX && !found; k += SCAN_STEP ) {
		if ( len > 0 && (D)k + BTFS_SYSHEAD > len ) break;
		if ( rd(ctx, off + k, h, BTFS_SYSHEAD) < E_OK ) break;
		if ( ( h[0] == 0x42 || h[0] == 0x52 ) && h[1] == 0xFE && h[2] == 0x64 && h[3] <= 2 ) {
			fs->le = FALSE;
		} else if ( h[0] == 0xFE && ( h[1] == 0x42 || h[1] == 0x52 ) && h[2] <= 2 && h[3] == 0x64 ) {
			fs->le = TRUE;
		} else {
			continue;
		}
		fs->bsize = g16(fs, h + SH_BSIZE);
		if ( fs->bsize < 512 || fs->bsize % 512 != 0 || fs->bsize > k ) continue;
		fs->base = off + k - fs->bsize;
		found = TRUE;
	}
	if ( !found ) return E_NOEXS;
	fs->format = g16(fs, h + SH_FORMAT);
	fs->ssys = g16(fs, h + SH_SSYS);
	fs->sfidt = g16(fs, h + SH_SFIDT);
	fs->sfnmt = g16(fs, h + SH_SFNMT);
	fs->nfmax = g16(fs, h + SH_NFMAX);
	fs->nlb = g32(fs, h + SH_NLB);
	fs->nfree = g32(fs, h + SH_NFREE);
	fs->mtime = g32(fs, h + SH_MTIME);
	fs->ctime = g32(fs, h + SH_CTIME);
	get_name(fs, h + SH_NAME, fs->name, fs->uname, sizeof(fs->uname));
	{
		UH	loc[BTFS_NAMELEN];

		get_name(fs, h + SH_LOCAT, loc, fs->ulocat, sizeof(fs->ulocat));
	}
	if ( fs->ssys == 0 || fs->sfidt == 0 || fs->nfmax == 0 || (UD)fs->sfidt * fs->bsize < (UD)fs->nfmax * 4 ) {
		return E_OBJ;
	}

	/* the file ID table, whole */
	bytes = fs->sfidt * fs->bsize;
	fs->fidt = malloc(bytes);
	fs->blk = malloc(fs->bsize);
	if ( fs->fidt == NULL || fs->blk == NULL ) {
		btfs_close(fs);
		return E_NOMEM;
	}
	if ( read_blk(fs, 1 + fs->ssys, fs->sfidt, fs->fidt) < E_OK ) {
		btfs_close(fs);
		return E_IO;
	}

	/*
	 * An entry is the count and then the block, each in the file
	 * system's order; one written as a single word with the count in its
	 * top byte is read too. The root's entry says which: its header
	 * must be where the entry points.
	 */
	fid_entry(fs, 0, &blk, &ref);
	if ( !is_header(fs, blk, 0) ) {
		fs->fidhigh = TRUE;
		fid_entry(fs, 0, &blk, &ref);
		if ( !is_header(fs, blk, 0) ) {
			btfs_close(fs);
			return E_OBJ;
		}
	}
	return E_OK;
}

EXPORT void btfs_close( BTFS *fs )
{
	free(fs->fidt);
	free(fs->blk);
	fs->fidt = NULL;
	fs->blk = NULL;
}

/* ---------------------------------------------------------------- a file */

/* The record indexes, gathered in their order */
typedef struct {
	UB	*e;
	UINT	n, max;
} IDXS;

LOCAL ER idx_put( IDXS *x, const UB *p, UINT n )
{
	if ( x->n + n > x->max ) {
		UINT	m = ( x->max == 0 ) ? 64 : x->max;
		UB	*g;

		while ( m < x->n + n ) m *= 2;
		g = realloc(x->e, (size_t)m * BTFS_RECIDX);
		if ( g == NULL ) return E_NOMEM;
		x->e = g;
		x->max = m;
	}
	memcpy(x->e + (size_t)x->n * BTFS_RECIDX, p, (size_t)n * BTFS_RECIDX);
	x->n += n;
	return E_OK;
}

/* The indexes under the indirect entries p[0..n) of one level, lv levels down to the index blocks */
LOCAL ER idx_walk( BTFS *fs, const UB *p, UINT n, INT lv, IDXS *x )
{
	UB	*b;
	UINT	i;
	ER	er = E_OK;

	b = malloc(fs->bsize);
	if ( b == NULL ) return E_NOMEM;
	for ( i = 0; i < n && er >= E_OK; i++ ) {
		UINT	blk = g32(fs, p + i * BTFS_INDIR + 4);

		if ( blk == 0 ) continue;		/* unused */
		er = read_blk(fs, blk, 1, b);
		if ( er < E_OK ) break;
		if ( lv <= 1 ) er = idx_put(x, b, fs->bsize / BTFS_RECIDX);
		else er = idx_walk(fs, b, fs->bsize / BTFS_INDIR, lv - 1, x);
	}
	free(b);
	return er;
}

LOCAL ER run_add( BTFSREC *r, UINT cnt, UINT blk )
{
	UINT	*b;
	UH	*c;

	if ( cnt == 0 ) return E_OK;
	b = realloc(r->blk, sizeof(UINT) * (size_t)( r->nrun + 1 ));
	if ( b == NULL ) return E_NOMEM;
	r->blk = b;
	c = realloc(r->cnt, sizeof(UH) * (size_t)( r->nrun + 1 ));
	if ( c == NULL ) return E_NOMEM;
	r->cnt = c;
	r->blk[r->nrun] = blk;
	r->cnt[r->nrun] = (UH)cnt;
	r->nrun++;
	return E_OK;
}

/* The indexes made records: a connection index adds its runs to the record before it */
LOCAL ER idx_records( BTFS *fs, const IDXS *x, BTFSFILE *f )
{
	BTFSREC	*last = NULL;
	UINT	i;
	INT	k;
	ER	er;

	f->rec = calloc(x->n ? x->n : 1, sizeof(BTFSREC));
	if ( f->rec == NULL ) return E_NOMEM;
	for ( i = 0; i < x->n; i++ ) {
		const UB	*e = x->e + (size_t)i * BTFS_RECIDX;
		BTFSREC		*r;

		if ( e[0] != 0 ) {
			/* a connection index: four runs, each a count and a block */
			if ( last == NULL ) continue;
			for ( k = 0; k < 4; k++ ) {
				er = run_add(last, e[4 * k], g24(fs, e + 4 * k + 1));
				if ( er < E_OK ) return er;
			}
			continue;
		}
		if ( e[1] == 0 ) continue;			/* unused */
		if ( ( e[1] & 0xE0 ) != 0x80 ) {
			last = NULL;
			continue;
		}
		r = &f->rec[f->nrec++];
		r->type = (UH)( e[1] & 0x1F );
		r->sub = (UH)g16(fs, e + 2);
		r->lfid = -1;
		if ( r->type == 0 ) {
			r->lfid = (INT)g16(fs, e + 4);
			for ( k = 0; k < 5; k++ ) r->latr[k] = (UH)g16(fs, e + 6 + 2 * k);
			f->nlink++;
			last = NULL;
			continue;
		}
		r->off = g16(fs, e + 6);
		r->size = g32(fs, e + 8);
		er = run_add(r, e[12], g24(fs, e + 13));
		if ( er < E_OK ) return er;
		last = r;
	}
	return E_OK;
}

EXPORT ER btfs_file( BTFS *fs, UINT fid, BTFSFILE *f )
{
	UB	*h;
	UINT	blk, ref, ridx;
	IDXS	x;
	ER	er = E_OK;

	memset(f, 0, sizeof(*f));
	f->fid = fid;
	f->target = -1;
	if ( fs->fidt == NULL || fid >= fs->nfmax ) return E_NOEXS;
	fid_entry(fs, fid, &blk, &ref);
	if ( blk == 0 ) return E_NOEXS;
	h = malloc(fs->bsize);
	if ( h == NULL ) return E_NOMEM;
	if ( read_blk(fs, blk, 1, h) < E_OK || g32(fs, h + FH_STARTID) != BTFS_STARTID
	  || g32(fs, h + FH_ENDID) != BTFS_ENDID ) {
		free(h);
		return E_OBJ;
	}
	f->ftype = g16(fs, h + FH_FTYPE);
	f->atype = g16(fs, h + FH_ATYPE);
	f->refcnt = ref;
	if ( ref == 255 && fs->format != BTFS_FMT_STD && g32(fs, h + FH_REFCNT) >= 255 ) f->refcnt = g32(fs, h + FH_REFCNT);
	f->ltime = g32(fs, h + FH_LTIME);
	f->atime = g32(fs, h + FH_ATIME);
	f->mtime = g32(fs, h + FH_MTIME);
	f->ctime = g32(fs, h + FH_CTIME);
	get_name(fs, h + FH_NAME, f->name, f->uname, sizeof(f->uname));

	if ( ( f->ftype >> 12 ) == BTFS_FT_LINK ) {
		/* a link file: where its target is */
		const UB	*l = h + BTFS_FILEHEAD;
		UH		tn[BTFS_NAMELEN];

		f->islink = TRUE;
		get_name(fs, l + FL_FSNAME, tn, f->tfsname, sizeof(f->tfsname));
		if ( same_name(tn, fs->name) ) f->target = (INT)g16(fs, l + FL_FID);
		free(h);
		return E_OK;
	}
	f->idxlv = (INT)(H)g16(fs, h + FH_IDXLV);
	f->size = g32(fs, h + FH_SIZE);
	f->nblk = g32(fs, h + FH_NBLK);
	ridx = BTFS_RIDXOFS;
	if ( fs->format == BTFS_FMT_EXT2 ) {
		UINT	r = g16(fs, h + FH_RIDXOFS);

		if ( r % BTFS_RECIDX == 0 && r >= BTFS_FILEHEAD + BTFS_RECIDX && r + BTFS_RECIDX <= fs->bsize ) ridx = r;
	}
	if ( ridx >= fs->bsize ) {
		free(h);
		return E_OBJ;
	}
	memset(&x, 0, sizeof(x));
	switch ( f->idxlv ) {
	case 0:
		er = idx_put(&x, h + ridx, ( fs->bsize - ridx ) / BTFS_RECIDX);
		break;
	case 1:
	case 2:
		er = idx_walk(fs, h + ridx, ( fs->bsize - ridx ) / BTFS_INDIR, f->idxlv, &x);
		break;
	default:
		er = E_OBJ;
		break;
	}
	free(h);
	if ( er >= E_OK ) er = idx_records(fs, &x, f);
	free(x.e);
	if ( er < E_OK ) btfs_file_free(f);
	return er;
}

EXPORT void btfs_file_free( BTFSFILE *f )
{
	INT	i;

	for ( i = 0; f->rec != NULL && i < f->nrec; i++ ) {
		free(f->rec[i].blk);
		free(f->rec[i].cnt);
	}
	free(f->rec);
	f->rec = NULL;
	f->nrec = 0;
	f->nlink = 0;
}

EXPORT INT btfs_link_target( BTFS *fs, const BTFSREC *r )
{
	BTFSFILE	f;
	INT		t;

	if ( r->type != 0 || r->lfid < 0 || !btfs_fid_used(fs, (UINT)r->lfid) ) return -1;
	if ( btfs_file(fs, (UINT)r->lfid, &f) < E_OK ) return -1;
	t = r->lfid;
	if ( f.islink ) {
		/* a link file to a file of this same file system goes on to it */
		t = ( f.target >= 0 && btfs_fid_used(fs, (UINT)f.target) ) ? f.target : -1;
	}
	btfs_file_free(&f);
	return t;
}

EXPORT ER btfs_rec_read( BTFS *fs, const BTFSREC *r, UB *buf )
{
	UINT	left = r->size, at = 0, start = r->off;
	INT	k;
	ER	er;

	if ( r->off >= fs->bsize && r->size > 0 ) return E_OBJ;
	for ( k = 0; k < r->nrun && left > 0; k++ ) {
		UD	span = (UD)r->cnt[k] * fs->bsize - start;
		UINT	n = ( span < left ) ? (UINT)span : left;

		if ( r->blk[k] == 0 || ( fs->nlb > 0 && ( r->blk[k] >= fs->nlb || r->cnt[k] > fs->nlb - r->blk[k] ) ) ) {
			return E_OBJ;
		}
		er = read_at(fs, fs->base + (D)r->blk[k] * fs->bsize + start, buf + at, n);
		if ( er < E_OK ) return er;
		at += n;
		left -= n;
		start = 0;
	}
	return ( left == 0 ) ? E_OK : E_OBJ;
}

/* ---------------------------------------------------------------- one archive of a file and what it links to */

LOCAL ER fail( char *err, INT max, const char *s, ER er )
{
	if ( err != NULL && max > 0 ) {
		strncpy(err, s, (size_t)max - 1);
		err[max - 1] = 0;
	}
	return er;
}

EXPORT ER btfs_book( BTFS *fs, UINT fid, UB **p_out, UINT *p_len, INT *p_nfiles, char *err, INT errmax )
{
	BTFSFILE	*fl = NULL;
	BPKWFILE	*wf = NULL;
	BPKREC		*recs = NULL;
	UB		**data = NULL;
	INT		*map = NULL;
	INT		n = 0, i, r, nr = 0, at, maxfl;
	BPKWRITE	w;
	ER		er = E_OK;

	*p_out = NULL;
	*p_len = 0;
	if ( p_nfiles != NULL ) *p_nfiles = 0;
	map = malloc(sizeof(INT) * fs->nfmax);
	maxfl = 64;
	fl = calloc((size_t)maxfl, sizeof(BTFSFILE));
	if ( map == NULL || fl == NULL ) {
		er = fail(err, errmax, "作業の場所がありません", E_NOMEM);
		goto out;
	}
	for ( i = 0; i < (INT)fs->nfmax; i++ ) map[i] = -1;

	/* the files: the one asked for, then what each links to as it is reached */
	if ( btfs_file(fs, fid, &fl[0]) < E_OK || fl[0].islink ) {
		er = fail(err, errmax, "実身を読めません", E_OBJ);
		goto out;
	}
	map[fid] = 0;
	n = 1;
	for ( i = 0; i < n && er >= E_OK; i++ ) {
		for ( r = 0; r < fl[i].nrec; r++ ) {
			INT	t;

			if ( fl[i].rec[r].type != 0 ) continue;
			t = btfs_link_target(fs, &fl[i].rec[r]);
			if ( t < 0 || map[t] >= 0 ) continue;
			if ( n >= BPK_FILES_FORM ) {
				er = fail(err, errmax, "仮身で結ばれた実身が多すぎます（65534 まで）", E_LIMIT);
				break;
			}
			if ( n >= maxfl ) {
				/* the table grown as the files are reached */
				BTFSFILE	*g = realloc(fl, sizeof(BTFSFILE) * (size_t)maxfl * 2);

				if ( g == NULL ) {
					er = fail(err, errmax, "作業の場所がありません", E_NOMEM);
					break;
				}
				fl = g;
				memset(fl + maxfl, 0, sizeof(BTFSFILE) * (size_t)maxfl);
				maxfl *= 2;
			}
			if ( btfs_file(fs, (UINT)t, &fl[n]) < E_OK || fl[n].islink ) {
				btfs_file_free(&fl[n]);
				memset(&fl[n], 0, sizeof(fl[n]));
				continue;			/* not there: the link goes nowhere */
			}
			map[t] = n++;
		}
	}
	if ( er < E_OK ) goto out;

	/* every record's bytes, a link record made the archive's */
	for ( i = 0; i < n; i++ ) nr += fl[i].nrec;
	wf = calloc((size_t)n, sizeof(BPKWFILE));
	recs = calloc(nr ? (size_t)nr : 1, sizeof(BPKREC));
	data = calloc(nr ? (size_t)nr : 1, sizeof(UB *));
	if ( wf == NULL || recs == NULL || data == NULL ) {
		er = fail(err, errmax, "作業の場所がありません", E_NOMEM);
		goto out;
	}
	for ( i = 0, at = 0; i < n && er >= E_OK; i++ ) {
		BTFSFILE	*f = &fl[i];
		BPKLHEAD	*hd = &wf[i].head;

		memset(hd, 0, sizeof(*hd));
		hd->f_type = f->ftype;
		hd->f_atype = f->atype;
		memcpy(hd->name, f->name, sizeof(hd->name));
		hd->origid = i;
		hd->f_ltime = f->ltime;
		hd->f_atime = f->atime;
		hd->f_mtime = f->mtime;
		hd->f_ctime = f->ctime;
		wf[i].nrec = f->nrec;
		wf[i].rec = recs + at;
		for ( r = 0; r < f->nrec; r++, at++ ) {
			BTFSREC	*q = &f->rec[r];
			BPKREC	*o = &recs[at];

			o->type = q->type;
			o->sub = q->sub;
			if ( q->type == 0 ) {
				INT	t = btfs_link_target(fs, q), k;

				data[at] = calloc(1, BPK_LINKREC);
				if ( data[at] == NULL ) {
					er = fail(err, errmax, "作業の場所がありません", E_NOMEM);
					break;
				}
				for ( k = 0; k < BTFS_NAMELEN; k++ ) wr16(data[at] + LK_FS_NAME + 2 * k, fs->name[k]);
				wr16(data[at] + LK_F_ID, ( t >= 0 && map[t] >= 0 ) ? (UINT)map[t] : 0xFFFF);
				for ( k = 0; k < 5; k++ ) wr16(data[at] + LK_ATR + 2 * k, q->latr[k]);
				o->size = BPK_LINKREC;
			} else {
				if ( q->size > 256 * 1024 * 1024 ) {
					er = fail(err, errmax, "レコードが大きすぎます", E_LIMIT);
					break;
				}
				data[at] = malloc(q->size ? q->size : 1);
				if ( data[at] == NULL ) {
					er = fail(err, errmax, "作業の場所がありません", E_NOMEM);
					break;
				}
				if ( btfs_rec_read(fs, q, data[at]) < E_OK ) {
					er = fail(err, errmax, "レコードを読めません", E_IO);
					break;
				}
				o->size = q->size;
			}
			o->data = data[at];
		}
	}
	if ( er < E_OK ) goto out;

	memset(&w, 0, sizeof(w));
	w.name = fl[0].uname;
	w.time = fl[0].mtime;
	w.method = BPK_LH0;
	w.nfiles = n;
	w.file = wf;
	w.maxfiles = BPK_FILES_FORM;		/* read back at once, never kept as a 書籍 */
	er = bpk_write(&w, p_out, p_len, NULL, err, errmax);
	if ( er >= E_OK && p_nfiles != NULL ) *p_nfiles = n;

    out:
	if ( data != NULL ) {
		for ( i = 0; i < nr; i++ ) free(data[i]);
	}
	free(data);
	free(recs);
	free(wf);
	if ( fl != NULL ) {
		for ( i = 0; i < n; i++ ) btfs_file_free(&fl[i]);
	}
	free(fl);
	free(map);
	return er;
}
