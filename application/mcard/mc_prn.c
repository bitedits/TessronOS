/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	mc_prn.c
 *	The pages of 差し込み印刷 for 印刷 (design 11.18, 11.22.6): each
 *	page the merge makes (mc_merge.c) made a record of xmlTAD of its
 *	own, on the paper the 印刷設定 says, and handed to the caller, who
 *	puts it in the object the desktop prints from.
 *
 *	A page of text is a document with the paper and the margins written
 *	at its end, after anything the layout brought, so that they are the
 *	ones that hold. A page of a figure is a figure with the paper given
 *	ahead of it; its elements are already where they go on the sheet.
 *	The merge's sizes are dots at 96 to the inch; the paper is written in
 *	points, three to four dots.
 *
 *	No system call is made here: the kernel's test uses it as it is.
 */

#include "mc_db.h"

LOCAL BOOL at_word( CONST UB *s, INT n, CONST char *w )
{
	INT	i;

	for ( i = 0; w[i] != 0; i++ ) {
		if ( i >= n || s[i] != (UB)w[i] ) return FALSE;
	}
	return TRUE;
}

/* A page's own start and end, written round the merge's elements */
LOCAL void paper_put( CTBUF *b, CONST MCPSINK *ps, BOOL doc )
{
	ct_put(b, doc ? "<paper type=\"doc\" length=\"" : "<paper type=\"fig\" length=\"");
	ct_num(b, ( ps->h * 3 + 2 ) / 4);
	ct_put(b, "\" width=\"");
	ct_num(b, ( ps->w * 3 + 2 ) / 4);
	ct_put(b, "\" binding=\"0\" imposition=\"0\" top=\"");
	ct_num(b, ps->m[0] * 3 / 4);
	ct_put(b, "\" bottom=\"");
	ct_num(b, ps->m[1] * 3 / 4);
	ct_put(b, "\" left=\"");
	ct_num(b, ps->m[2] * 3 / 4);
	ct_put(b, "\" right=\"");
	ct_num(b, ps->m[3] * 3 / 4);
	ct_put(b, "\"/>\n<docmargin top=\"");
	ct_num(b, ps->m[0] * 3 / 4);
	ct_put(b, "\" bottom=\"");
	ct_num(b, ps->m[1] * 3 / 4);
	ct_put(b, "\" left=\"");
	ct_num(b, ps->m[2] * 3 / 4);
	ct_put(b, "\" right=\"");
	ct_num(b, ps->m[3] * 3 / 4);
	ct_put(b, "\"/>\n");
}

LOCAL void ps_start( void *arg, INT kind, INT w, INT h )
{
	MCPSINK	*ps = (MCPSINK *)arg;

	ps->kind = kind;
	ps->w = w;
	ps->h = h;
	ps->pages = 0;
}

LOCAL void ps_page( void *arg, CONST UB *body, INT n )
{
	MCPSINK	*ps = (MCPSINK *)arg;
	CTBUF	b;
	ER	er;

	if ( ps->er < E_OK ) {
		return;
	}
	ct_buf(&b, ps->mem, ps->max);
	ct_put(&b, "<tad version=\"1.0\" encoding=\"UTF-8\">\n");
	if ( ps->kind == MC_PG_FIG ) {
		paper_put(&b, ps, FALSE);
		ct_put(&b, "<figure>\n");
		ct_putn(&b, body, n);
		ct_put(&b, "</figure>\n</tad>\n");
	} else {
		INT	i = 0, from = 0;

		ct_put(&b, "<document>\n");
		paper_put(&b, ps, TRUE);
		/* the layout's own paper and margins left out: the 印刷設定's hold */
		while ( i < n ) {
			if ( body[i] == '<' && ( at_word(body + i, n - i, "<paper ")
					      || at_word(body + i, n - i, "<docmargin ") ) ) {
				INT	e = i;

				while ( e < n && body[e] != '>' ) e++;
				ct_putn(&b, body + from, i - from);
				i = from = ( e < n ) ? e + 1 : n;
				continue;
			}
			i++;
		}
		ct_putn(&b, body + from, n - from);
		ct_put(&b, "</document>\n</tad>\n");
	}
	if ( b.fail ) {
		ps->er = E_LIMIT;
		return;
	}
	er = ps->put(ps->arg, b.p, b.n);
	if ( er < E_OK ) {
		ps->er = er;
		return;
	}
	ps->pages++;
}

LOCAL void ps_end( void *arg )
{
	(void)arg;
}

EXPORT void mc_prn_sink( MCSINK *sink, MCPSINK *ps, CONST MCPRINT *p, UB *mem, INT max,
			 ER (*put)( void *arg, CONST UB *xml, INT n ), void *arg )
{
	INT	w, h;

	ps->put = put;
	ps->arg = arg;
	ps->mem = mem;
	ps->max = max;
	ps->er = E_OK;
	ps->pages = 0;
	ps->kind = MC_PG_DOC;
	mc_paper(p, &w, &h, ps->m);
	ps->w = w;
	ps->h = h;
	sink->arg = ps;
	sink->start = ps_start;
	sink->page = ps_page;
	sink->end = ps_end;
}
