/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	mc_io.c
 *	マイクロカード: the cards written out and read in (design 11.18)
 *
 *	Written out, the cards are lines of text: @形式 (a definition of the
 *	fields, "@名前:種類:最大文字数:一覧表示幅" each, ended by "$", then
 *	each card as "@名前:データ" lines ended by "$", every letter of full
 *	width and ":" and "＼" in the data with "＼" before them), or values
 *	separated by tabs or commas, a line a card, the fields' names on the
 *	first line, texts in quotes with "＼" before a quote or a "＼" in
 *	them. The tray takes the values with tabs and no quotes. A sheet of
 *	基本表計算 takes the names in its first row and a card a row.
 *
 *	Read in, a text is @形式 when its first line begins with "@", else
 *	values separated by tabs when the first line has a tab, else by
 *	commas; quotes round a value may be left out. The values go to the
 *	fields of the same names, made to their kinds and sizes; a field the
 *	database does not have is passed over. A database with no fields
 *	takes them from what is read: @形式's definition, or the names of
 *	the first line, a field being a number when every value it has is
 *	one, as long as its longest value.
 */

#include "mc_db.h"

#define ESC_CH		0xFF3C		/* ＼ */

LOCAL void m_copy( void *d, CONST void *s, INT n )
{
	UB		*dd = (UB *)d;
	CONST UB	*ss = (CONST UB *)s;

	while ( n-- > 0 ) *dd++ = *ss++;
}

LOCAL void put_cp( CTBUF *b, UINT c )
{
	UB	u[4];

	ct_putn(b, u, mc_u8_put(c, u));
}

/* A value as it is shown, in out */
LOCAL INT value_text( CONST MCDB *db, CONST MCCARD *c, INT f, UB *out, INT max )
{
	return mc_shown(db, c, f, out, max);
}

LOCAL BOOL out_card( CONST MCDB *db, INT i, BOOL found_only )
{
	return (BOOL)( !found_only || !db->searching || db->c[i]->found );
}

/* Letters of half width made full: ASCII and the space */
LOCAL UINT full_width( UINT c )
{
	if ( c == ' ' ) return 0x3000;
	if ( c > 0x20 && c < 0x7F ) return c + 0xFEE0;
	return c;
}

/* A text in full width, with ＼ before the letters given */
LOCAL void put_escaped( CTBUF *b, CONST UB *s, INT n, BOOL full, CONST UINT *esc, INT nesc )
{
	INT	i = 0, k;
	UINT	c;

	while ( i < n ) {
		INT	l = mc_u8_get(s + i, n - i, &c);

		if ( l <= 0 ) break;
		if ( full ) c = full_width(c);
		for ( k = 0; k < nesc; k++ ) {
			if ( c == esc[k] ) put_cp(b, ESC_CH);
		}
		put_cp(b, c);
		i += l;
	}
}

EXPORT void mc_out_at( CTBUF *b, CONST MCDB *db, BOOL found_only )
{
	static CONST UINT esc[3] = { 0xFF1A, ESC_CH, ':' };	/* ：＼ */
	static CONST char *CONST kind[3] = { "文字", "数値", "仮身" };
	UB	*v = (UB *)mc_alloc(MC_VALUE_MAX + 8);
	INT	i, k;

	if ( v == NULL ) {
		b->fail = TRUE;
		return;
	}
	for ( k = 0; k < db->nf; k++ ) {
		ct_put(b, "＠");
		put_escaped(b, db->f[k].name, mc_len(db->f[k].name), TRUE, esc, 3);
		ct_put(b, "：");
		ct_put(b, kind[db->f[k].type]);
		ct_put(b, "：");
		{
			UB	d[16];
			INT	n = 0, x = db->f[k].max;

			do {
				d[n++] = (UB)( '0' + x % 10 );
				x /= 10;
			} while ( x > 0 );
			while ( n > 0 ) put_cp(b, full_width(d[--n]));
			ct_put(b, "：");
			n = 0;
			x = db->f[k].width;
			do {
				d[n++] = (UB)( '0' + x % 10 );
				x /= 10;
			} while ( x > 0 );
			while ( n > 0 ) put_cp(b, full_width(d[--n]));
		}
		ct_put(b, "\n");
	}
	ct_put(b, "＄\n");
	for ( i = 0; i < db->n; i++ ) {
		if ( !out_card(db, i, found_only) ) continue;
		for ( k = 0; k < db->nf; k++ ) {
			INT	n = value_text(db, db->c[i], k, v, MC_VALUE_MAX);

			if ( n <= 0 ) continue;
			ct_put(b, "＠");
			put_escaped(b, db->f[k].name, mc_len(db->f[k].name), TRUE, esc, 3);
			ct_put(b, "：");
			put_escaped(b, v, n, TRUE, esc, 3);
			ct_put(b, "\n");
		}
		ct_put(b, "＄\n");
	}
	mc_free(v);
}

EXPORT void mc_out_rows( CTBUF *b, CONST MCDB *db, INT from, INT to, BOOL found_only, UINT how )
{
	static CONST UINT esc[3] = { '"', ESC_CH, 0xFF02 };
	UB	*v = (UB *)mc_alloc(MC_VALUE_MAX + 8);
	UB	sep = ( how & MC_OUT_CSV ) ? ',' : '\t';
	INT	i, k, pos = 0;

	if ( v == NULL ) {
		b->fail = TRUE;
		return;
	}
	if ( how & MC_OUT_HEAD ) {
		for ( k = 0; k < db->nf; k++ ) {
			if ( k > 0 ) ct_putn(b, &sep, 1);
			if ( how & MC_OUT_QUOTE ) ct_put(b, "\"");
			put_escaped(b, db->f[k].name, mc_len(db->f[k].name), FALSE, ( how & MC_OUT_QUOTE ) ? esc : NULL,
				    ( how & MC_OUT_QUOTE ) ? 3 : 0);
			if ( how & MC_OUT_QUOTE ) ct_put(b, "\"");
		}
		ct_put(b, "\n");
	}
	for ( i = 0; i < db->n; i++ ) {
		if ( !out_card(db, i, found_only) ) continue;
		if ( from >= 0 && ( pos < from || pos > to ) ) {
			pos++;
			continue;
		}
		pos++;
		for ( k = 0; k < db->nf; k++ ) {
			INT	n = value_text(db, db->c[i], k, v, MC_VALUE_MAX);
			BOOL	q = (BOOL)( ( how & MC_OUT_QUOTE ) && db->f[k].type != MC_T_NUM );

			if ( k > 0 ) ct_putn(b, &sep, 1);
			if ( q ) ct_put(b, "\"");
			put_escaped(b, v, n, FALSE, q ? esc : NULL, q ? 3 : 0);
			if ( q ) ct_put(b, "\"");
		}
		ct_put(b, "\n");
	}
	mc_free(v);
}

EXPORT void mc_out_sheet( CXSHEET *s, CONST MCDB *db, BOOL found_only )
{
	UB	*v = (UB *)mc_alloc(MC_VALUE_MAX + 8);
	INT	i, k, row = 1;

	if ( v == NULL ) return;
	for ( k = 0; k < db->nf; k++ ) {
		CXCELL	*c = cx_make(s, k + 1, 1);

		if ( c != NULL ) (void)cx_set_text(c, db->f[k].name);
	}
	for ( i = 0; i < db->n; i++ ) {
		if ( !out_card(db, i, found_only) ) continue;
		row++;
		for ( k = 0; k < db->nf; k++ ) {
			INT	n = value_text(db, db->c[i], k, v, MC_VALUE_MAX);
			CXCELL	*c;

			if ( n <= 0 ) continue;
			c = cx_make(s, k + 1, row);
			if ( c != NULL ) (void)cx_set_text(c, v);
		}
	}
	cx_grow(s, db->nf > 0 ? db->nf : 1, row);
	mc_free(v);
}

EXPORT void mc_text_doc( CTBUF *b, CONST UB *t, INT n, CONST UB *name )
{
	INT	i, s = 0;

	ct_put(b, "<tad version=\"1.0\" encoding=\"UTF-8\"");
	if ( name != NULL && name[0] != 0 ) {
		ct_put(b, " filename=\"");
		ct_esc(b, name, mc_len(name));
		ct_put(b, "\"");
	}
	ct_put(b, ">\n<document>\n");
	for ( i = 0; i <= n; i++ ) {
		if ( i < n && t[i] != '\n' ) continue;
		if ( i == n && i == s ) break;
		ct_put(b, "<p>");
		{
			/* a tab as <tab/>, as the text editor writes it */
			INT	a = s, j;

			for ( j = s; j < i; j++ ) {
				if ( t[j] != '\t' ) continue;
				ct_esc(b, t + a, j - a);
				ct_put(b, "<tab/>");
				a = j + 1;
			}
			ct_esc(b, t + a, i - a);
		}
		ct_put(b, "</p>\n");
		s = i + 1;
	}
	ct_put(b, "</document>\n</tad>\n");
}

/* ---------------------------------------------------------------- reading */

LOCAL BOOL at_is( CONST UB *s, INT n, CONST char *w )
{
	INT	i;

	for ( i = 0; w[i] != 0; i++ ) {
		if ( i >= n || s[i] != (UB)w[i] ) return FALSE;
	}
	return TRUE;
}

EXPORT INT mc_doc_text( CONST UB *x, INT n, UB *out, INT max )
{
	static CONST char *CONST ent[5] = { "&amp;", "&lt;", "&gt;", "&quot;", "&apos;" };
	static CONST char ch[5] = { '&', '<', '>', '"', '\'' };
	INT	i, k = 0, para = 0;
	BOOL	in_doc = FALSE, any = FALSE;

	for ( i = 0; i < n && !any; i++ ) {
		if ( at_is(x + i, n - i, "<document") ) any = TRUE;
	}
	if ( !any ) in_doc = TRUE;	/* plain text, or a fragment without one */
	for ( i = 0; i < n && k < max - 4; ) {
		if ( x[i] == '<' && ( any || i + 1 < n ) ) {
			INT	e = i;

			while ( e < n && x[e] != '>' ) e++;
			if ( at_is(x + i, n - i, "<document") ) in_doc = TRUE;
			else if ( at_is(x + i, n - i, "</document") ) in_doc = FALSE;
			else if ( in_doc && at_is(x + i, n - i, "<tab") ) out[k++] = '\t';
			else if ( in_doc && ( at_is(x + i, n - i, "<p>") || at_is(x + i, n - i, "<p ") ) ) {
				if ( para++ > 0 ) out[k++] = '\n';
			} else if ( in_doc && at_is(x + i, n - i, "<br") ) out[k++] = '\n';
			i = e + 1;
			continue;
		}
		if ( !in_doc || x[i] == '\r' || ( any && x[i] == '\n' ) ) {
			i++;
			continue;
		}
		if ( x[i] == '&' ) {
			INT	e;

			for ( e = 0; e < 5 && !at_is(x + i, n - i, ent[e]); e++ ) ;
			if ( e < 5 ) {
				out[k++] = (UB)ch[e];
				i += mc_len((CONST UB *)ent[e]);
				continue;
			}
		}
		out[k++] = x[i++];
	}
	out[k] = 0;
	return k;
}

/* One line of t from *p_at: its bytes, the place moved past its end */
LOCAL INT line_of( CONST UB *t, INT n, INT *p_at, CONST UB **p_s )
{
	INT	a = *p_at, e = a;

	while ( e < n && t[e] != '\n' ) e++;
	*p_s = t + a;
	*p_at = ( e < n ) ? e + 1 : n;
	if ( e > a && t[e - 1] == '\r' ) e--;
	return e - a;
}

/*
 * The values of a line, split at sep (outside quotes); each value with
 * its quotes and the ＼ before a letter taken off, in buf, its start and length
 * in at[] and len[]. How many.
 */
LOCAL INT split( CONST UB *s, INT n, UINT sep, UB *buf, INT bmax, INT *at, INT *len, INT max )
{
	INT	i = 0, k = 0, o = 0;

	while ( k < max ) {
		BOOL	quoted = FALSE, open = FALSE;
		UINT	c;
		INT	l;

		at[k] = o;
		/* spaces before a quote are not the value's */
		{
			INT	j = i;

			while ( j < n && s[j] == ' ' ) j++;
			if ( j < n && s[j] == '"' ) {
				i = j + 1;
				quoted = open = TRUE;
			}
		}
		while ( i < n ) {
			l = mc_u8_get(s + i, n - i, &c);
			if ( l <= 0 ) break;
			if ( c == ESC_CH && i + l < n ) {
				/* ＼ and the letter after it: that letter, as it is */
				UINT	d;
				INT	dl = mc_u8_get(s + i + l, n - i - l, &d);

				if ( o + dl < bmax ) {
					m_copy(buf + o, s + i + l, dl);
					o += dl;
				}
				i += l + dl;
				continue;
			}
			if ( open && c == '"' ) {
				if ( i + 1 < n && s[i + 1] == '"' ) {
					if ( o < bmax ) buf[o++] = '"';
					i += 2;
					continue;
				}
				open = FALSE;
				i++;
				continue;
			}
			/* the last value there is room for takes the rest of the line */
			if ( !open && c == sep && k < max - 1 ) break;
			if ( o + l < bmax ) {
				m_copy(buf + o, s + i, l);
				o += l;
			}
			i += l;
		}
		(void)quoted;
		len[k] = o - at[k];
		k++;
		if ( i >= n ) break;
		i += mc_u8_get(s + i, n - i, &sep);	/* past the separator */
	}
	return k;
}

/* The field of a name, -1 none */
LOCAL INT field_named( CONST MCDB *db, CONST UB *s, INT n )
{
	INT	k, i;

	for ( k = 0; k < db->nf; k++ ) {
		CONST UB	*nm = db->f[k].name;

		if ( mc_len(nm) != n ) continue;
		for ( i = 0; i < n && nm[i] == s[i]; i++ ) ;
		if ( i == n ) return k;
	}
	return -1;
}

/* A name as a field may have it: of full width, at most 8 letters */
LOCAL void name_make( CONST UB *s, INT n, UB *out )
{
	CTBUF	b;
	INT	i = 0, k;
	UINT	c;

	ct_buf(&b, out, MC_NAME_MAX - 1);
	while ( i < n ) {
		INT	l = mc_u8_get(s + i, n - i, &c);

		if ( l <= 0 ) break;
		put_cp(&b, full_width(c));
		i += l;
	}
	out[b.n] = 0;
	k = mc_tron_fit(out, b.n, MC_NAME_LETTERS);
	out[k] = 0;
}

/* A field added to a database with none, as long as its longest value */
LOCAL void field_add( MCDB *db, CONST UB *name, INT n, INT type, INT max, INT width )
{
	MCFIELD	*f;
	INT	k;

	if ( db->nf >= MC_FIELD_MAX ) return;
	f = &db->f[db->nf];
	for ( k = 0; k < (INT)sizeof(*f); k++ ) ( (UB *)f )[k] = 0;
	name_make(name, n, f->name);
	if ( f->name[0] == 0 || field_named(db, f->name, mc_len(f->name)) >= 0 ) return;
	f->type = type;
	if ( type == MC_T_TEXT ) {
		f->max = ( max >= 1 && max <= MC_TEXT_MAX ) ? max : ( max > MC_TEXT_MAX ? MC_TEXT_MAX : 1 );
		f->face = MC_FACE_MINCHO;
	} else if ( type == MC_T_NUM ) {
		f->max = ( max >= 1 && max <= MC_NUM_MAX ) ? max : MC_NUM_MAX;
	} else {
		f->max = ( max >= 1 && max <= MC_LINK_LETTERS ) ? max : 20;
	}
	f->width = ( width >= 0 ) ? width : ( f->max < 20 ? f->max : 20 );
	if ( f->width > MC_WIDTH_MAX ) f->width = MC_WIDTH_MAX;
	db->nf++;
}

/* The fields grown on the cards there are, after fields were added */
LOCAL BOOL fields_grown( MCDB *db )
{
	INT	i, k;

	for ( i = 0; i < db->n; i++ ) {
		MCCARD	*c = db->c[i];

		if ( c->nv < db->nf ) {
			UB	**nv = (UB **)mc_alloc((INT)sizeof(UB *) * db->nf);

			if ( nv == NULL ) return FALSE;
			for ( k = 0; k < db->nf; k++ ) nv[k] = ( k < c->nv ) ? c->v[k] : NULL;
			mc_free(c->v);
			c->v = nv;
			c->nv = db->nf;
		}
	}
	return TRUE;
}

/* A value read in put in a field, made to its kind; a link field takes no text */
LOCAL void value_put( MCDB *db, MCCARD *c, INT f, CONST UB *s, INT n )
{
	if ( f < 0 || n <= 0 || db->f[f].type == MC_T_LINK ) return;
	if ( db->f[f].type == MC_T_NUM ) {
		UB	t[MC_NUM_MAX * 2 + 8];
		INT	k = mc_num_lead(s, n, t, sizeof(t));

		if ( k > 0 && k <= MC_NUM_MAX ) (void)mc_set(db, c, f, t, k);
		return;
	}
	(void)mc_set(db, c, f, s, n);
}

/* A database that was new -- no fields and one empty card -- loses that card to what is read */
LOCAL BOOL fresh( CONST MCDB *db )
{
	return (BOOL)( db->nf == 0 && db->n == 1 && mc_card_empty(db, 0) );
}

/* @形式 */
LOCAL INT in_at( MCDB *db, CONST UB *t, INT n, INT at )
{
	UB	*buf = (UB *)mc_alloc(MC_VALUE_MAX * 2 + 8);
	INT	p = 0, added = 0, cols[8], lens[8];
	BOOL	define = TRUE, design = (BOOL)( db->nf == 0 ), was_fresh = fresh(db);
	MCCARD	*card = NULL;

	if ( buf == NULL ) return E_NOMEM;
	while ( p < n ) {
		CONST UB	*s;
		INT		l = line_of(t, n, &p, &s), k;
		UINT		c;
		INT		cl;

		if ( l == 0 ) continue;
		cl = mc_u8_get(s, l, &c);
		if ( c == '$' || c == 0xFF04 ) {
			if ( define ) {
				define = FALSE;
				if ( design && !fields_grown(db) ) break;
			} else {
				card = NULL;
			}
			continue;
		}
		if ( c != '@' && c != 0xFF20 ) continue;
		k = split(s + cl, l - cl, 0xFF1A, buf, MC_VALUE_MAX * 2, cols, lens, define ? 4 : 2);
		if ( k < 2 && !( k == 1 && !define ) ) {
			/* a ':' of half width is taken too */
			k = split(s + cl, l - cl, ':', buf, MC_VALUE_MAX * 2, cols, lens, define ? 4 : 2);
		}
		if ( define ) {
			if ( design && k >= 2 ) {
				UB	kind[16];
				INT	type = MC_T_TEXT, max = 0, width = -1, j;

				for ( j = 0; j < lens[1] && j < 15; j++ ) kind[j] = buf[cols[1] + j];
				kind[j] = 0;
				if ( at_is(kind, j, "数値") ) type = MC_T_NUM;
				else if ( at_is(kind, j, "仮身") ) type = MC_T_LINK;
				if ( k >= 3 ) {
					UB	num[32];

					if ( mc_num_norm(buf + cols[2], lens[2], num, sizeof(num)) > 0 ) {
						for ( j = 0, max = 0; num[j] >= '0' && num[j] <= '9'; j++ ) max = max * 10 + ( num[j] - '0' );
					}
				}
				if ( k >= 4 ) {
					UB	num[32];

					if ( mc_num_norm(buf + cols[3], lens[3], num, sizeof(num)) > 0 ) {
						for ( j = 0, width = 0; num[j] >= '0' && num[j] <= '9'; j++ ) width = width * 10 + ( num[j] - '0' );
					}
				}
				field_add(db, buf + cols[0], lens[0], type, max > 0 ? max : 40, width);
			}
			continue;
		}
		if ( card == NULL ) {
			if ( was_fresh && db->n == 1 && mc_card_empty(db, 0) && added == 0 ) {
				card = db->c[0];
				at = 1;
			} else {
				card = mc_card_new(db, at++);
				if ( card == NULL ) break;
			}
			added++;
		}
		{
			UB	nm[MC_NAME_MAX];

			name_make(buf + cols[0], lens[0], nm);
			value_put(db, card, field_named(db, nm, mc_len(nm)), ( k >= 2 ) ? buf + cols[1] : NULL, ( k >= 2 ) ? lens[1] : 0);
		}
	}
	mc_free(buf);
	return added;
}

/* Values separated by sep, the names on the first line */
LOCAL INT in_rows( MCDB *db, CONST UB *t, INT n, INT at, UINT sep )
{
	INT	*cols, *lens, *map, *maxl, p = 0, k, nc, added = 0, i;
	BOOL	*num, design = (BOOL)( db->nf == 0 ), was_fresh = fresh(db);
	UB	*buf = (UB *)mc_alloc(MC_VALUE_MAX * 8 + 8);
	CONST UB *s;
	INT	l;

	cols = (INT *)mc_alloc((INT)sizeof(INT) * MC_FIELD_MAX * 6);
	if ( buf == NULL || cols == NULL ) {
		mc_free(buf);
		mc_free(cols);
		return E_NOMEM;
	}
	lens = cols + MC_FIELD_MAX;
	map = lens + MC_FIELD_MAX;
	maxl = map + MC_FIELD_MAX;
	num = (BOOL *)( maxl + MC_FIELD_MAX );
	do {
		l = line_of(t, n, &p, &s);
	} while ( l == 0 && p < n );
	nc = split(s, l, sep, buf, MC_VALUE_MAX * 8, cols, lens, MC_FIELD_MAX);
	if ( design ) {
		/* the kinds and sizes from every value of each column */
		INT	q = p;

		for ( k = 0; k < nc; k++ ) {
			maxl[k] = 1;
			num[k] = TRUE;
		}
		{
			BOOL	any[MC_FIELD_MAX];
			UB	*b2 = buf + MC_VALUE_MAX * 4;
			INT	c2[MC_FIELD_MAX], l2[MC_FIELD_MAX], m;

			for ( k = 0; k < nc; k++ ) any[k] = FALSE;
			while ( q < n ) {
				CONST UB	*s2;
				INT		ll = line_of(t, n, &q, &s2);

				if ( ll == 0 ) continue;
				m = split(s2, ll, sep, b2, MC_VALUE_MAX * 4, c2, l2, nc);
				for ( k = 0; k < m; k++ ) {
					INT	tl;

					if ( l2[k] == 0 ) continue;
					any[k] = TRUE;
					if ( mc_num_norm(b2 + c2[k], l2[k], NULL, 0) < 0 ) num[k] = FALSE;
					tl = mc_tron_fit(b2 + c2[k], l2[k], MC_TEXT_MAX * 2);
					tl = mc_letters(b2 + c2[k], tl);
					if ( tl > maxl[k] ) maxl[k] = tl;
				}
			}
			for ( k = 0; k < nc; k++ ) {
				if ( !any[k] ) num[k] = FALSE;
				field_add(db, buf + cols[k], lens[k], num[k] ? MC_T_NUM : MC_T_TEXT,
					  num[k] ? ( maxl[k] < MC_NUM_MAX ? maxl[k] : MC_NUM_MAX ) : maxl[k], -1);
			}
		}
		if ( !fields_grown(db) ) {
			mc_free(buf);
			mc_free(cols);
			return E_NOMEM;
		}
	}
	for ( k = 0; k < nc; k++ ) {
		UB	nm[MC_NAME_MAX];

		name_make(buf + cols[k], lens[k], nm);
		map[k] = field_named(db, nm, mc_len(nm));
	}
	while ( p < n ) {
		MCCARD	*c;

		l = line_of(t, n, &p, &s);
		if ( l == 0 ) continue;
		k = split(s, l, sep, buf, MC_VALUE_MAX * 8, cols, lens, nc);
		if ( was_fresh && added == 0 && db->n == 1 && mc_card_empty(db, 0) ) {
			c = db->c[0];
			at = 1;
		} else {
			c = mc_card_new(db, at++);
		}
		if ( c == NULL ) break;
		for ( i = 0; i < k; i++ ) value_put(db, c, map[i], buf + cols[i], lens[i]);
		added++;
	}
	mc_free(buf);
	mc_free(cols);
	return added;
}

EXPORT INT mc_in_text( MCDB *db, CONST UB *t, INT n, INT at, INT form )
{
	INT	p = 0, l = 0, i;
	CONST UB *s = t;
	UINT	c = 0;

	while ( p < n && l == 0 ) l = line_of(t, n, &p, &s);
	if ( l == 0 ) return 0;
	if ( form == MC_IN_AUTO ) {
		(void)mc_u8_get(s, l, &c);
		form = MC_IN_CSV;
		if ( c == '@' || c == 0xFF20 ) form = MC_IN_AT;
		for ( i = 0; i < l && form == MC_IN_CSV; i++ ) {
			if ( s[i] == '\t' ) form = MC_IN_TAB;
		}
	}
	if ( form == MC_IN_AT ) return in_at(db, t, n, at);
	return in_rows(db, t, n, at, ( form == MC_IN_TAB ) ? '\t' : ',');
}

EXPORT INT mc_in_rows( MCDB *db, CONST UB *t, INT n, INT at )
{
	UB	*buf = (UB *)mc_alloc(MC_VALUE_MAX * 8 + 8);
	INT	cols[MC_FIELD_MAX], lens[MC_FIELD_MAX], p = 0, added = 0, k, i;

	if ( buf == NULL ) return E_NOMEM;
	while ( p < n ) {
		CONST UB	*s;
		INT		l = line_of(t, n, &p, &s);
		MCCARD		*c;

		if ( l == 0 ) continue;
		k = split(s, l, '\t', buf, MC_VALUE_MAX * 8, cols, lens, db->nf);
		c = mc_card_new(db, at++);
		if ( c == NULL ) break;
		for ( i = 0; i < k; i++ ) value_put(db, c, i, buf + cols[i], lens[i]);
		added++;
	}
	mc_free(buf);
	return added;
}

EXPORT INT mc_in_db( MCDB *db, CONST MCDB *src, INT at )
{
	INT	map[MC_FIELD_MAX], k, i, added = 0;
	UB	*buf;
	BOOL	was_fresh = fresh(db);

	if ( db->nf == 0 ) {
		for ( k = 0; k < src->nf; k++ ) db->f[k] = src->f[k];
		db->nf = src->nf;
		if ( !fields_grown(db) ) return E_NOMEM;
	}
	buf = (UB *)mc_alloc(MC_VALUE_MAX + 8);
	if ( buf == NULL ) return E_NOMEM;
	for ( k = 0; k < src->nf; k++ ) map[k] = field_named(db, src->f[k].name, mc_len(src->f[k].name));
	for ( i = 0; i < src->n; i++ ) {
		MCCARD	*c;

		if ( was_fresh && added == 0 && db->n == 1 && mc_card_empty(db, 0) ) {
			c = db->c[0];
			at = 1;
		} else {
			c = mc_card_new(db, at++);
		}
		if ( c == NULL ) break;
		for ( k = 0; k < src->nf; k++ ) {
			CONST UB	*v = mc_get(src->c[i], k);
			INT		f = map[k], n;

			if ( f < 0 || v == NULL ) continue;
			if ( src->f[k].type == MC_T_LINK && db->f[f].type == MC_T_LINK ) {
				(void)mc_set(db, c, f, v, mc_len(v));
				continue;
			}
			n = mc_shown(src, src->c[i], k, buf, MC_VALUE_MAX);
			value_put(db, c, f, buf, n);
		}
		added++;
	}
	mc_free(buf);
	return added;
}

EXPORT INT mc_in_sheet( MCDB *db, CONST CXSHEET *s, INT at )
{
	CTBUF	b;
	UB	*mem;
	INT	max = 1024 * 1024, row, col, r;

	for ( ;; ) {
		mem = (UB *)mc_alloc(max);
		if ( mem == NULL ) return E_NOMEM;
		ct_buf(&b, mem, max);
		for ( row = 1; row <= s->maxrow; row++ ) {
			INT	last = 0;

			for ( col = 1; col <= s->maxcol; col++ ) {
				if ( cx_get(s, col, row) != NULL ) last = col;
			}
			if ( last == 0 ) continue;
			for ( col = 1; col <= last; col++ ) {
				CONST CXCELL	*c = cx_get(s, col, row);

				if ( col > 1 ) ct_put(&b, "\t");
				if ( c != NULL ) {
					CONST UB	*w = cx_shown(c);
					INT		k;

					for ( k = 0; w[k] != 0; k++ ) {
						UB	ch = ( w[k] == '\t' || w[k] == '\n' ) ? ' ' : w[k];

						ct_putn(&b, &ch, 1);
					}
				}
			}
			ct_put(&b, "\n");
		}
		if ( !b.fail || max >= 64 * 1024 * 1024 ) break;
		mc_free(mem);
		max *= 4;
	}
	r = b.fail ? E_LIMIT : mc_in_text(db, b.p, b.n, at, MC_IN_TAB);
	mc_free(mem);
	return r;
}
