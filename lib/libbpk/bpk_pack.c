/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk_pack.c
 *	An archive (書庫, BPK) written (design 11.20)
 *
 *	The writer makes what bpk_parse reads. The stream is the extension
 *	(the root's link), a local header for each object and then each
 *	object's records, a record header (type, subtype, size) before the
 *	bytes of each; the local header says where an object's records
 *	start, how many there are, how many are link records and how many
 *	bytes the others take. The stream is compressed with LH5, or stored,
 *	and goes after the global header in a 指定付箋 of application
 *	8000 C003 8000, in a figure, in a binary TAD of its own: 管理情報,
 *	the figure's start, the 付箋, the figure's end.
 *
 *	The global header carries a checksum of its own bytes, the CRC-16
 *	of the stream inflated, the bytes the objects' records take (links
 *	aside) and the time the archive was made. The local headers' own
 *	sizes and CRC are 0: the stream is compressed as one.
 */

#include <ts/bpk.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define GH_TYPE_BOOK	1		/* the global header's headtype */
#define GH_VERSION_BOOK	0x0100
#define EXT_LINK	4		/* the extension: the root's link after its two words */
#define EXT_VOBJ	( EXT_LINK + BPK_LINKREC )
#define EXT_SIZE	( EXT_VOBJ + 32 )	/* the virtual object segment, its data empty */
#define TAD_VERSION	0x0121
#define CHSZ_FUSEN	0x0010		/* the 付箋's letters, in the figure's units */
#define COL_BLACK	0x10000000U
#define COL_WHITE	0x10FFFFFFU

LOCAL void wr16( UB *p, UINT v )
{
	p[0] = (UB)v;
	p[1] = (UB)( v >> 8 );
}

LOCAL void wr32( UB *p, UINT v )
{
	p[0] = (UB)v;
	p[1] = (UB)( v >> 8 );
	p[2] = (UB)( v >> 16 );
	p[3] = (UB)( v >> 24 );
}

LOCAL UINT rd16( const UB *p )
{
	return (UINT)p[0] | ( (UINT)p[1] << 8 );
}

LOCAL UINT rd32( const UB *p )
{
	return rd16(p) | ( rd16(p + 2) << 16 );
}

/* A UTF-8 text as max TRON characters at p, the rest 0 */
LOCAL void put_tc( UB *p, const char *s, INT max )
{
	UH	*t = calloc((size_t)max + 1, sizeof(UH));
	INT	i;

	if ( t == NULL ) {
		memset(p, 0, (size_t)max * 2);
		return;
	}
	if ( s != NULL ) bpk_utf8_tron(s, t, max);
	for ( i = 0; i < max; i++ ) wr16(p + 2 * i, t[i]);
	free(t);
}

LOCAL ER fail( char *err, INT max, const char *s, ER er )
{
	if ( err != NULL && max > 0 ) {
		strncpy(err, s, (size_t)max - 1);
		err[max - 1] = 0;
	}
	return er;
}

/* ---------------------------------------------------------------- records of an object */

/*
 * A 実行機能付箋 entry: laid out as a 付箋 is -- a box and how it looks,
 * the application, its name and the kind of what it opens -- and the
 * length of its data. The data has the window at 4 and, for an editor,
 * the window's colour at bgoff; マイクロスクリプト's $SV are its last
 * 200 bytes.
 */
EXPORT INT bpk_exec_put( UB *out, const BPKAPPL *a, const INT *win, UINT bg, BOOL hasbg, const INT *sv )
{
	UINT	dlen = 0x22, i;
	UB	*d = out + BPK_EXECHEAD;

	if ( sv != NULL ) dlen += 50 * 4;
	memset(out, 0, BPK_EXECHEAD + dlen);
	wr16(out + 8, CHSZ_FUSEN);
	wr32(out + 10, COL_BLACK);
	wr32(out + 14, COL_BLACK);
	wr32(out + 18, COL_WHITE);
	wr16(out + 22, 4);
	for ( i = 0; i < 3; i++ ) wr16(out + EX_APPL + 2 * i, a->appl[i]);
	put_tc(out + EX_NAME, a->name, 16);
	put_tc(out + EX_TYPE, a->kind, 16);
	wr16(out + EX_DLEN, dlen);
	if ( win != NULL ) {
		for ( i = 0; i < 4; i++ ) wr16(d + 4 + 2 * i, (UINT)win[i]);
	}
	if ( hasbg && a->bgoff != 0 && a->bgoff + 4 <= dlen ) wr32(d + a->bgoff, bg);
	if ( sv != NULL ) {
		for ( i = 0; i < 50; i++ ) wr32(d + dlen - 200 + 4 * i, (UINT)sv[i]);
	}
	return (INT)( BPK_EXECHEAD + dlen );
}

/*
 * A link record: the file system's name (TRON code), the target's local
 * header, the VLINK's attr and relationship and the application of what
 * it points at.
 */
EXPORT void bpk_link_put( UB *out, UINT fid, UINT attr, UINT rel, const UH *appl )
{
	INT	i;

	memset(out, 0, BPK_LINKREC);
	put_tc(out + LK_FS_NAME, "TessronOS", 20);
	wr16(out + LK_F_ID, fid);
	wr16(out + LK_ATR, attr);
	wr16(out + LK_ATR + 2, rel);
	for ( i = 0; i < 3; i++ ) wr16(out + LK_ATR + 4 + 2 * i, appl != NULL ? appl[i] : 0);
}

/* ---------------------------------------------------------------- the archive */

EXPORT BOOL bpk_is_archive( const UB *raw, UINT len )
{
	UINT	pos = 0;

	if ( raw == NULL || len < 2 || rd16(raw) != TS_INFO ) return FALSE;
	while ( pos + 4 <= len ) {
		UINT	w = rd16(raw + pos), l;

		if ( w <= TC_SPEC ) {
			pos += 2;
			continue;
		}
		pos += 2;
		if ( w == TS_EXT ) continue;
		l = rd16(raw + pos);
		pos += 2;
		if ( l == TS_LONG ) {
			if ( pos + 4 > len ) return FALSE;
			l = rd32(raw + pos);
			pos += 4;
		}
		if ( l > len - pos ) return FALSE;
		if ( w == TS_DFUSEN && l >= DF_DATA + BPK_GLOBALHEAD && rd16(raw + pos + DF_APPL) == BPK_APPL_HI
		  && rd16(raw + pos + DF_APPL + 2) == BPK_APPL_ARCHIVE && rd16(raw + pos + DF_APPL + 4) == BPK_APPL_HI ) {
			return TRUE;
		}
		pos += l;
	}
	return FALSE;
}

/* The extension: one link, the root's, and its virtual object segment with no data */
LOCAL void put_ext( UB *p, const BPKWRITE *w )
{
	const BPKWFILE	*r = &w->file[0];
	INT		i;

	memset(p, 0, EXT_SIZE);
	wr16(p, 1);
	wr16(p + 2, EXT_SIZE - EXT_LINK);
	bpk_link_put(p + EXT_LINK, (UINT)r->head.origid, w->rootattr, 0, w->rootappl);
	for ( i = 0; i < 4; i++ ) wr16(p + EXT_VOBJ + 2 * i, (UINT)w->rootview[i]);
	wr16(p + EXT_VOBJ + 8, (UINT)( w->rootview[3] - w->rootview[1] ));
	wr16(p + EXT_VOBJ + 10, w->rootchsz);
	for ( i = 0; i < 4; i++ ) wr32(p + EXT_VOBJ + 12 + 4 * i, w->rootcol[i]);
	wr32(p + EXT_VOBJ + 28, 0);
}

EXPORT ER bpk_write( const BPKWRITE *w, UB **p_out, UINT *p_len, BPKWSTAT *st, char *err, INT errmax )
{
	UINT		extlen, origsize, pos, compsize, fusen, total, i;
	INT		f, r, view[4];
	UB		*plain, *packed, *out, *g, *d;
	BPKGHEAD	gh;
	BPKWSTAT	s;

	*p_out = NULL;
	*p_len = 0;
	memset(&s, 0, sizeof(s));
	if ( w->nfiles <= 0 ) return fail(err, errmax, "書籍にする実身がありません", E_PAR);
	if ( w->nfiles > ( ( w->maxfiles > 0 && w->maxfiles <= BPK_FILES_FORM ) ? w->maxfiles : BPK_FILES_MAX ) ) {
		return fail(err, errmax, ( w->maxfiles > BPK_FILES_MAX ) ? "実身が多すぎます" : "書籍に入る実身は650までです", E_LIMIT);
	}
	if ( w->method != BPK_LH5 && w->method != BPK_LH0 ) return fail(err, errmax, "その圧縮方式では書けません", E_PAR);

	/* the size of the stream */
	extlen = ( w->ext != NULL ) ? w->extlen : w->hasroot ? EXT_SIZE : 0;
	origsize = extlen + (UINT)w->nfiles * BPK_LOCALHEAD;
	for ( f = 0; f < w->nfiles; f++ ) {
		for ( r = 0; r < w->file[f].nrec; r++ ) {
			const BPKREC	*q = &w->file[f].rec[r];

			if ( q->size > 256 * 1024 * 1024 || origsize + BPK_RECHEAD + q->size < origsize ) {
				return fail(err, errmax, "実身が大きすぎます", E_LIMIT);
			}
			origsize += BPK_RECHEAD + q->size;
			if ( q->type == BPK_RT_LINK ) s.nlink++;
			else s.filesize += q->size;
		}
	}
	if ( origsize > 256 * 1024 * 1024 ) return fail(err, errmax, "書籍が大きすぎます", E_LIMIT);

	/* the stream: the extension, the local headers, the records */
	plain = malloc(origsize ? origsize : 1);
	if ( plain == NULL ) return fail(err, errmax, "書籍を作る場所がありません", E_NOMEM);
	if ( w->ext != NULL ) memcpy(plain, w->ext, extlen);
	else if ( w->hasroot ) put_ext(plain, w);
	pos = extlen + (UINT)w->nfiles * BPK_LOCALHEAD;
	for ( f = 0; f < w->nfiles; f++ ) {
		const BPKWFILE	*o = &w->file[f];
		BPKLHEAD	h = o->head;

		h.offset = pos;
		h.f_nrec = (UINT)o->nrec;
		h.f_nlink = 0;
		h.f_size = 0;
		h.compmethod = 0;
		h.origsize = h.compsize = 0;
		h.crc = 0;
		for ( r = 0; r < o->nrec; r++ ) {
			const BPKREC	*q = &o->rec[r];

			wr16(plain + pos + RH_TYPE, q->type);
			wr16(plain + pos + RH_SUBTYPE, q->sub);
			wr32(plain + pos + RH_SIZE, q->size);
			pos += BPK_RECHEAD;
			if ( q->size != 0 ) memcpy(plain + pos, q->data, q->size);
			pos += q->size;
			if ( q->type == BPK_RT_LINK ) h.f_nlink++;
			else h.f_size += q->size;
		}
		bpk_lhead_put(&h, plain + extlen + (UINT)f * BPK_LOCALHEAD);
	}
	s.origsize = origsize;
	s.crc = bpk_crc16(plain, origsize, 0);

	/* compressed, or as it is */
	if ( w->method == BPK_LH5 ) {
		UINT	room = origsize + origsize / 4 + 4096;
		BPKLH5E	*z = malloc(sizeof(BPKLH5E));
		INT	n;

		packed = malloc(room);
		if ( z == NULL || packed == NULL ) {
			free(z);
			free(packed);
			free(plain);
			return fail(err, errmax, "書籍を作る場所がありません", E_NOMEM);
		}
		n = lh5_encode(z, plain, origsize, packed, room);
		free(z);
		if ( n < 0 ) {
			free(packed);
			free(plain);
			return fail(err, errmax, "圧縮できません", E_LIMIT);
		}
		compsize = (UINT)n;
		free(plain);
	} else {
		packed = plain;
		compsize = origsize;
	}
	s.compsize = compsize;

	/*
	 * The file: 管理情報 (10), the figure's start (28), the 付箋 (8 and
	 * its data, kept to an even length), the figure's end (4)
	 */
	fusen = DF_DATA + BPK_GLOBALHEAD + compsize;
	fusen += fusen & 1;
	total = 10 + 28 + 8 + fusen + 4;
	out = malloc(total);
	if ( out == NULL ) {
		free(packed);
		return fail(err, errmax, "書籍を作る場所がありません", E_NOMEM);
	}
	memset(out, 0, total);
	if ( w->hasroot && w->rootview[2] > w->rootview[0] && w->rootview[3] > w->rootview[1] ) {
		for ( i = 0; i < 4; i++ ) view[i] = w->rootview[i];
	} else {
		view[0] = 0;
		view[1] = 0;
		view[2] = 200;
		view[3] = 24;
	}
	pos = 0;
	wr16(out + pos, TS_INFO);
	wr16(out + pos + 2, 6);
	wr16(out + pos + 4, 0);
	wr16(out + pos + 6, 2);
	wr16(out + pos + 8, TAD_VERSION);
	pos += 10;
	wr16(out + pos, TS_FIG);
	wr16(out + pos + 2, 24);
	for ( i = 0; i < 4; i++ ) {
		wr16(out + pos + 4 + 2 * i, (UINT)view[i]);
		wr16(out + pos + 12 + 2 * i, (UINT)view[i]);
	}
	wr16(out + pos + 20, (UINT)-120 & 0xFFFF);		/* 120 to the inch */
	wr16(out + pos + 22, (UINT)-120 & 0xFFFF);
	pos += 28;
	wr16(out + pos, TS_DFUSEN);
	wr16(out + pos + 2, TS_LONG);
	wr32(out + pos + 4, fusen);
	pos += 8;
	d = out + pos;
	for ( i = 0; i < 4; i++ ) wr16(d + DF_VIEW + 2 * i, (UINT)view[i]);
	wr16(d + DF_CHSZ, w->hasroot ? w->rootchsz : 0);
	wr32(d + DF_FRCOL, w->hasroot ? w->rootcol[0] : COL_BLACK);
	wr32(d + DF_CHCOL, w->hasroot ? w->rootcol[1] : COL_BLACK);
	wr32(d + DF_TBCOL, w->hasroot ? w->rootcol[2] : COL_WHITE);
	wr16(d + DF_PICT, 0);
	wr16(d + DF_APPL, BPK_APPL_HI);
	wr16(d + DF_APPL + 2, BPK_APPL_ARCHIVE);
	wr16(d + DF_APPL + 4, BPK_APPL_HI);
	put_tc(d + DF_NAME, w->name, 16);
	wr32(d + DF_DLEN, BPK_GLOBALHEAD + compsize);
	g = d + DF_DATA;
	memset(&gh, 0, sizeof(gh));
	gh.headtype = GH_TYPE_BOOK;
	gh.version = GH_VERSION_BOOK;
	gh.crc = s.crc;
	gh.nfiles = (UINT)w->nfiles;
	gh.method = (UINT)w->method;
	gh.time = w->time;
	gh.filesize = s.filesize;
	gh.origsize = origsize;
	gh.compsize = compsize;
	gh.extsize = extlen;
	bpk_ghead_put(&gh, g);
	memcpy(g + BPK_GLOBALHEAD, packed, compsize);
	free(packed);
	pos += fusen;
	wr16(out + pos, TS_FIGEND);
	wr16(out + pos + 2, 0);

	*p_out = out;
	*p_len = total;
	if ( st != NULL ) *st = s;
	return E_OK;
}
