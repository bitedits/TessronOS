/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	kp_eng.c
 *	かな入力練習: the questions, the judging of each key and the scores
 *	(design 11.14)
 */

#include "kp_eng.h"

#define CP_LP		0x0028		/* ( */
#define CP_RP		0x0029		/* ) */
#define CP_WLP		0xFF08		/* （ */
#define CP_WRP		0xFF09		/* ） */
#define CP_DAKU		0x309B		/* ゛ */
#define CP_HANDAKU	0x309C		/* ゜ */
#define CP_SMALL_TU	0x3063		/* っ */
#define CP_N		0x3093		/* ん */

/* ---------------------------------------------------------------- small things */

LOCAL void zero( void *p, INT n )
{
	UB	*b = (UB *)p;

	while ( n-- > 0 ) *b++ = 0;
}

LOCAL INT cp_len( CONST UH *s )
{
	INT	n = 0;

	while ( s[n] != 0 ) n++;
	return n;
}

/* What \s matches in a JavaScript pattern, and what trim() takes away */
LOCAL BOOL is_ws( UINT c )
{
	return (BOOL)( c == 0x09 || c == 0x0A || c == 0x0B || c == 0x0C || c == 0x0D || c == 0x20
		    || c == 0xA0 || c == 0x1680 || ( c >= 0x2000 && c <= 0x200A ) || c == 0x2028
		    || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF );
}

/* One letter of UTF-8 at s[*p_i], and *p_i moved past it */
LOCAL UINT utf8_get( CONST UB *s, INT n, INT *p_i )
{
	INT	i = *p_i;
	UINT	c = s[i++];

	if ( c >= 0xF0 && i + 2 < n + 1 ) {
		c = ( ( c & 0x07 ) << 18 ) | ( ( s[i] & 0x3F ) << 12 ) | ( ( s[i + 1] & 0x3F ) << 6 ) | ( s[i + 2] & 0x3F );
		i += 3;
	} else if ( c >= 0xE0 && i + 1 < n + 1 ) {
		c = ( ( c & 0x0F ) << 12 ) | ( ( s[i] & 0x3F ) << 6 ) | ( s[i + 1] & 0x3F );
		i += 2;
	} else if ( c >= 0xC0 && i < n + 1 ) {
		c = ( ( c & 0x1F ) << 6 ) | ( s[i] & 0x3F );
		i += 1;
	}
	if ( i > n ) i = n;
	*p_i = i;
	return c;
}

LOCAL INT utf8_put( UINT c, UB *o )
{
	if ( c < 0x80 ) {
		o[0] = (UB)c;
		return 1;
	}
	if ( c < 0x800 ) {
		o[0] = (UB)( 0xC0 | ( c >> 6 ) );
		o[1] = (UB)( 0x80 | ( c & 0x3F ) );
		return 2;
	}
	if ( c < 0x10000 ) {
		o[0] = (UB)( 0xE0 | ( c >> 12 ) );
		o[1] = (UB)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
		o[2] = (UB)( 0x80 | ( c & 0x3F ) );
		return 3;
	}
	o[0] = (UB)( 0xF0 | ( c >> 18 ) );
	o[1] = (UB)( 0x80 | ( ( c >> 12 ) & 0x3F ) );
	o[2] = (UB)( 0x80 | ( ( c >> 6 ) & 0x3F ) );
	o[3] = (UB)( 0x80 | ( c & 0x3F ) );
	return 4;
}

EXPORT INT kp_utf8( CONST UH *cp, INT n, UB *out, INT max )
{
	INT	i, k = 0;
	UB	b[4];

	for ( i = 0; i < n && cp[i] != 0; i++ ) {
		INT	m = utf8_put(cp[i], b), j;

		if ( k + m >= max ) break;
		for ( j = 0; j < m; j++ ) out[k++] = b[j];
	}
	out[k] = 0;
	return k;
}

EXPORT void kp_fixed( INT v, INT places, char *out )
{
	char	t[16];
	INT	n = 0, k = 0, i;
	BOOL	neg = (BOOL)( v < 0 );

	if ( neg ) v = -v;
	do {
		t[n++] = (char)( '0' + v % 10 );
		v /= 10;
	} while ( v > 0 || n <= places );
	if ( neg ) out[k++] = '-';
	for ( i = n - 1; i >= 0; i-- ) {
		out[k++] = t[i];
		if ( i == places && places > 0 ) out[k++] = '.';
	}
	out[k] = 0;
}

/* a / b rounded half up, for a >= 0, b > 0 */
LOCAL INT div_round( D a, D b )
{
	return (INT)( ( a * 2 + b ) / ( b * 2 ) );
}

/* ---------------------------------------------------------------- the questions */

EXPORT void kp_init( KPENG *e )
{
	zero(e, sizeof(*e));
	e->pick_count = KP_PICK;
}

LOCAL BOOL name_is( CONST UB *s, INT n, INT at, CONST char *w )
{
	INT	i;

	for ( i = 0; w[i] != 0; i++ ) {
		if ( at + i >= n || s[at + i] != (UB)w[i] ) return FALSE;
	}
	return TRUE;
}

/* An entity at s[*p_i] ('&'): its letter, *p_i past it; 0 when it is none */
LOCAL UINT entity( CONST UB *s, INT n, INT *p_i )
{
	INT	i = *p_i + 1, end;
	UINT	v = 0;

	for ( end = i; end < n && end - i < 10 && s[end] != ';'; end++ ) ;
	if ( end >= n || s[end] != ';' ) return 0;
	if ( name_is(s, n, i, "lt;") ) v = '<';
	else if ( name_is(s, n, i, "gt;") ) v = '>';
	else if ( name_is(s, n, i, "amp;") ) v = '&';
	else if ( name_is(s, n, i, "quot;") ) v = '"';
	else if ( name_is(s, n, i, "apos;") ) v = '\'';
	else if ( s[i] == '#' ) {
		INT	k = i + 1;

		if ( k < end && ( s[k] == 'x' || s[k] == 'X' ) ) {
			for ( k++; k < end; k++ ) {
				UB	c = s[k];

				v = v * 16 + ( ( c >= '0' && c <= '9' ) ? c - '0' : ( c >= 'a' && c <= 'f' ) ? c - 'a' + 10
					      : ( c >= 'A' && c <= 'F' ) ? c - 'A' + 10 : 0 );
			}
		} else {
			for ( ; k < end; k++ ) v = v * 10 + ( s[k] - '0' );
		}
	} else {
		return 0;
	}
	*p_i = end + 1;
	return v;
}

/* A paragraph's text, trimmed, added to the questions when it is not empty */
LOCAL void add_question( KPENG *e, CONST UB *t, INT n )
{
	INT	a = 0, b = n, i, j;

	/* blanks off both ends, letter by letter */
	while ( a < b ) {
		i = a;
		if ( !is_ws(utf8_get(t, b, &i)) ) break;
		a = i;
	}
	while ( b > a ) {
		for ( j = b - 1; j > a && ( t[j] & 0xC0 ) == 0x80; j-- ) ;
		i = j;
		if ( !is_ws(utf8_get(t, b, &i)) ) break;
		b = j;
	}
	if ( b <= a || e->nq >= KP_Q_MAX || e->pool_n + ( b - a ) > KP_POOL ) return;
	e->q_at[e->nq] = e->pool_n;
	e->q_len[e->nq] = b - a;
	for ( i = a; i < b; i++ ) e->pool[e->pool_n++] = t[i];
	e->nq++;
}

/*
 * Every element named p, and the text inside it as textContent has it:
 * the text of what is nested in it too, the entities read.
 */
EXPORT INT kp_questions( KPENG *e, CONST UB *x, INT n )
{
	static UB	t[4096];
	INT		i = 0, k;

	e->nq = 0;
	e->pool_n = 0;
	while ( x != NULL && i < n ) {
		if ( x[i] != '<' || i + 2 >= n || x[i + 1] != 'p'
		  || !( x[i + 2] == '>' || x[i + 2] == '/' || x[i + 2] == ' ' || x[i + 2] == '\t'
		     || x[i + 2] == '\n' || x[i + 2] == '\r' ) ) {
			i++;
			continue;
		}
		/* the start tag; <p/> holds nothing */
		for ( ; i < n && x[i] != '>'; i++ ) ;
		if ( i < n && x[i - 1] == '/' ) {
			i++;
			continue;
		}
		i++;
		k = 0;
		while ( i < n ) {
			if ( x[i] == '<' ) {
				if ( name_is(x, n, i, "</p>") || name_is(x, n, i, "</p ") ) break;
				if ( name_is(x, n, i, "<!--") ) {
					for ( i += 4; i < n && !name_is(x, n, i, "-->"); i++ ) ;
					i += 3;
					continue;
				}
				if ( name_is(x, n, i, "<![CDATA[") ) {
					for ( i += 9; i < n && !name_is(x, n, i, "]]>"); i++ ) {
						if ( k < (INT)sizeof(t) - 1 ) t[k++] = x[i];
					}
					i += 3;
					continue;
				}
				for ( ; i < n && x[i] != '>'; i++ ) ;
				i++;
				continue;
			}
			if ( x[i] == '&' ) {
				INT	at = i;
				UINT	c = entity(x, n, &at);

				if ( c != 0 ) {
					UB	b[4];
					INT	m = utf8_put(c, b), j;

					for ( j = 0; j < m && k < (INT)sizeof(t) - 1; j++ ) t[k++] = b[j];
					i = at;
					continue;
				}
			}
			if ( k < (INT)sizeof(t) - 1 ) t[k++] = x[i];
			i++;
		}
		add_question(e, t, k);
		for ( ; i < n && x[i] != '>'; i++ ) ;
		i++;
	}
	if ( e->nq == 0 ) add_question(e, (CONST UB *)"こんにちは", 15);
	e->npick = e->nq;
	for ( k = 0; k < e->nq; k++ ) e->pick[k] = k;
	return e->nq;
}

LOCAL BOOL is_open( UH c )
{
	return (BOOL)( c == CP_LP || c == CP_WLP );
}

LOCAL BOOL is_close( UH c )
{
	return (BOOL)( c == CP_RP || c == CP_WRP );
}

LOCAL void seg_add( KPENG *e, INT d0, INT d1, INT r0, INT r1 )
{
	INT	i;

	if ( e->nseg < KP_SEG_MAX ) {
		KPSEG	*s = &e->seg[e->nseg++];

		s->d0 = d0;
		s->d1 = d1;
		s->r0 = r0;
		s->r1 = r1;
		e->ph[e->nph].nseg++;
	}
	/* what is typed: the reading, or the word itself */
	if ( r1 > r0 ) {
		for ( i = r0; i < r1 && e->nin < KP_IN_MAX; i++ ) e->in[e->nin++] = e->q[i];
	} else {
		for ( i = d0; i < d1 && e->nin < KP_IN_MAX; i++ ) e->in[e->nin++] = e->q[i];
	}
}

/*
 * A phrase, q[a..b): each "word(reading)" a word with its reading, and
 * what is between them a word without one. A word is one or more
 * letters that are not an opening parenthesis; the reading one or more
 * that are not a closing one.
 */
LOCAL void phrase_add( KPENG *e, INT a, INT b )
{
	INT		last = a, st;
	KPPHRASE	*p;

	if ( e->nph >= KP_PH_MAX ) return;
	p = &e->ph[e->nph];
	p->seg0 = e->nseg;
	p->nseg = 0;
	p->in0 = e->nin;
	for ( ;; ) {
		INT	op = -1, cl = -1;

		for ( st = last; st < b; st++ ) {
			if ( is_open(e->q[st]) ) continue;
			for ( op = st + 1; op < b && !is_open(e->q[op]); op++ ) ;
			if ( op >= b ) {
				st = b;
				break;
			}
			for ( cl = op + 1; cl < b && !is_close(e->q[cl]); cl++ ) ;
			if ( cl < b && cl > op + 1 ) break;
		}
		if ( st >= b ) break;
		if ( st > last ) seg_add(e, last, st, 0, 0);
		seg_add(e, st, op, op + 1, cl);
		last = cl + 1;
	}
	if ( last < b ) seg_add(e, last, b, 0, 0);
	p->nin = e->nin - p->in0;
	e->nph++;
}

EXPORT void kp_load( KPENG *e, INT index )
{
	INT	i = 0, n, at, len, a;

	if ( index < 0 || index >= e->npick ) return;
	e->cq = index;
	at = e->q_at[e->pick[index]];
	len = e->q_len[e->pick[index]];
	e->nqcp = 0;
	while ( i < len && e->nqcp < KP_QCP ) {
		e->q[e->nqcp++] = (UH)utf8_get(e->pool + at, len, &i);
	}
	e->nseg = 0;
	e->nph = 0;
	e->nin = 0;
	n = e->nqcp;
	for ( i = 0; i < n; ) {
		while ( i < n && is_ws(e->q[i]) ) i++;
		if ( i >= n ) break;
		a = i;
		while ( i < n && !is_ws(e->q[i]) ) i++;
		phrase_add(e, a, i);
	}
	e->cph = 0;
	e->cch = 0;
	e->nrbuf = 0;
}

/* ---------------------------------------------------------------- the layout */

LOCAL INT rev_find( CONST KPENG *e, UH cp )
{
	INT	i;

	for ( i = 0; i < e->nrev; i++ ) {
		if ( e->rev[i].cp == cp ) return i;
	}
	return -1;
}

LOCAL void rev_set( KPENG *e, CONST UH *cp, UINT usage, INT shift )
{
	INT	i;

	if ( cp[0] == 0 || cp[1] != 0 ) return;		/* only a single letter is looked up */
	i = rev_find(e, cp[0]);
	if ( i < 0 ) {
		if ( e->nrev >= KP_REV_MAX ) return;
		i = e->nrev++;
	}
	e->rev[i].cp = cp[0];
	e->rev[i].usage = (UH)usage;
	e->rev[i].shift = shift;
}

EXPORT void kp_set_map( KPENG *e, INT mode, CONST KBKANAMAP *map )
{
	INT	i;

	e->mode = mode;
	e->map = map;
	e->nrev = 0;
	e->maxcombo = 0;
	e->pending = 0;
	e->nrbuf = 0;
	if ( map == NULL || mode != KP_M_KANA ) return;
	for ( i = 0; i < map->n; i++ ) {
		CONST KBKANA	*k = &map->k[i];

		rev_set(e, k->cp[KB_PLAIN], k->usage, KP_SH_NONE);
		rev_set(e, k->cp[KB_LEFT], k->usage, KP_SH_LEFT);
		rev_set(e, k->cp[KB_RIGHT], k->usage, KP_SH_RIGHT);
	}
	for ( i = 0; i < map->nc; i++ ) {
		INT	n = cp_len(map->c[i].cp);

		if ( n > e->maxcombo ) e->maxcombo = n;
	}
}

/* The chord that types in[0..n) exactly: the last the layout gives for it */
LOCAL CONST KBCOMBO *combo_of( CONST KPENG *e, CONST UH *in, INT n )
{
	INT	i, k;

	if ( e->map == NULL ) return NULL;
	for ( i = e->map->nc - 1; i >= 0; i-- ) {
		CONST UH	*c = e->map->c[i].cp;

		for ( k = 0; k < n && c[k] == in[k]; k++ ) ;
		if ( k == n && c[n] == 0 ) return &e->map->c[i];
	}
	return NULL;
}

/*
 * A kana and its mark: ぱ is は and ゜ (the handakuten), が か and ゛.
 * 0 for a kana with neither.
 */
LOCAL UH base_of( UH c, UH *p_mark )
{
	*p_mark = 0;
	/* ぱぴぷぺぽ, パピプペポ: is_ha */
	if ( ( c >= 0x3071 && c <= 0x307D && ( c - 0x3071 ) % 3 == 0 )
	  || ( c >= 0x30D1 && c <= 0x30DD && ( c - 0x30D1 ) % 3 == 0 ) ) {
		*p_mark = CP_HANDAKU;
		return (UH)( c - 2 );
	}
	/* が..ぢ: every other from か */
	if ( ( c >= 0x304C && c <= 0x3062 && ( c - 0x304C ) % 2 == 0 )
	  || ( c >= 0x30AC && c <= 0x30C2 && ( c - 0x30AC ) % 2 == 0 ) ) {
		*p_mark = CP_DAKU;
		return (UH)( c - 1 );
	}
	/* づでど */
	if ( c == 0x3065 || c == 0x3067 || c == 0x3069 || c == 0x30C5 || c == 0x30C7 || c == 0x30C9 ) {
		*p_mark = CP_DAKU;
		return (UH)( c - 1 );
	}
	/* ばびぶべぼ */
	if ( ( c >= 0x3070 && c <= 0x307C && ( c - 0x3070 ) % 3 == 0 )
	  || ( c >= 0x30D0 && c <= 0x30DC && ( c - 0x30D0 ) % 3 == 0 ) ) {
		*p_mark = CP_DAKU;
		return (UH)( c - 1 );
	}
	if ( c == 0x3094 ) {
		*p_mark = CP_DAKU;
		return 0x3046;		/* ゔ */
	}
	if ( c == 0x30F4 ) {
		*p_mark = CP_DAKU;
		return 0x30A6;		/* ヴ */
	}
	return 0;
}

/*
 * Whether a kana is typed as its kana and its mark: one with the
 * handakuten always (the layouts put none of them on a key), one with
 * the dakuten only where the layout does not put it on a key. Only
 * when the mark is on a key of the layout.
 */
LOCAL BOOL composed( CONST KPENG *e, UH c, UH *p_base, UH *p_mark )
{
	UH	b = base_of(c, p_mark);

	*p_base = b;
	if ( b == 0 ) return FALSE;
	if ( *p_mark == CP_HANDAKU ) return (BOOL)( rev_find(e, CP_HANDAKU) >= 0 );
	return (BOOL)( rev_find(e, c) < 0 && rev_find(e, CP_DAKU) >= 0 && rev_find(e, b) >= 0 );
}

/* What the keys to come type at the start of the reading in[0..n) */
typedef struct {
	BOOL		combo;
	CONST KBCOMBO	*cb;
	UH		usage;
	INT		shift;
	INT		len;			/* letters of the reading it types */
	BOOL		comp;			/* a kana then its mark */
} EXPECT;

LOCAL BOOL expect_at( CONST KPENG *e, CONST UH *in, INT n, EXPECT *x )
{
	UH	ch, base, mark;
	BOOL	hand;
	INT	len, i;

	zero(x, sizeof(*x));
	if ( n <= 0 ) return FALSE;
	ch = in[0];
	hand = (BOOL)( composed(e, ch, &base, &mark) && mark == CP_HANDAKU );
	if ( !hand ) {
		for ( len = e->maxcombo; len >= 1; len-- ) {
			CONST KBCOMBO	*cb;

			if ( len > n ) continue;
			cb = combo_of(e, in, len);
			if ( cb != NULL ) {
				x->combo = TRUE;
				x->cb = cb;
				x->len = len;
				return TRUE;
			}
		}
	}
	i = rev_find(e, ch);
	if ( i >= 0 ) {
		x->usage = e->rev[i].usage;
		x->shift = e->rev[i].shift;
		x->len = 1;
		return TRUE;
	}
	if ( composed(e, ch, &base, &mark) ) {
		i = rev_find(e, base);
		if ( i >= 0 ) {
			x->usage = e->rev[i].usage;
			x->shift = e->rev[i].shift;
			x->len = 1;
			x->comp = TRUE;
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------------------------------------------------------------- moving on */

LOCAL void stop_clock( KPENG *e, UD now )
{
	e->end = ( now != 0 ) ? now : 1;
}

/* After letters were typed right: the phrase, the question, the practice done */
LOCAL void advance( KPENG *e, UD now, KPRES *r )
{
	if ( e->cch < e->ph[e->cph].nin ) return;
	r->phrase_done = TRUE;
	e->cph++;
	if ( e->cph >= e->nph ) {
		if ( e->cq + 1 < e->npick ) {
			kp_load(e, e->cq + 1);
			r->question_done = TRUE;
		} else {
			r->finished = TRUE;
			e->completed = TRUE;
			e->practising = FALSE;
			stop_clock(e, now);
		}
	} else {
		e->cch = 0;
	}
}

EXPORT void kp_start( KPENG *e, UINT (*rnd)( void ), UD now )
{
	INT	pool[KP_Q_MAX], np = e->nq, count, i, k;

	for ( i = 0; i < np; i++ ) pool[i] = i;
	count = ( e->pick_count < np ) ? e->pick_count : np;
	e->npick = 0;
	for ( i = 0; i < count; i++ ) {
		INT	at = (INT)( ( rnd != NULL ? rnd() : 0 ) % (UINT)np );

		e->pick[e->npick++] = pool[at];
		for ( k = at; k < np - 1; k++ ) pool[k] = pool[k + 1];
		np--;
	}
	e->correct = e->wrong = e->left = e->right = e->shift = e->keys = 0;
	e->dist = 0;
	e->last = 0;
	e->pending = 0;
	kp_load(e, 0);
	e->practising = TRUE;
	e->completed = FALSE;
	e->started = TRUE;
	e->start = now;
	e->end = 0;
}

EXPORT void kp_stop( KPENG *e )
{
	e->practising = FALSE;
	e->nrbuf = 0;
}

/* ---------------------------------------------------------------- romaji */

typedef struct {
	char	w[KB_ROMA_LEN];
	INT	len;				/* letters of the reading it types */
} WAY;

/* Hiragana for a katakana, the signs left as they are */
LOCAL UH hira( UH c )
{
	return ( c >= 0x30A1 && c <= 0x30F6 ) ? (UH)( c - 0x60 ) : c;
}

LOCAL BOOL way_has( CONST WAY *w, INT n, CONST char *s )
{
	INT	i, k;

	for ( i = 0; i < n; i++ ) {
		for ( k = 0; s[k] != 0 && w[i].w[k] == s[k]; k++ ) ;
		if ( s[k] == 0 && w[i].w[k] == 0 ) return TRUE;
	}
	return FALSE;
}

LOCAL INT way_add( WAY *w, INT n, INT max, CONST char *s, INT len )
{
	INT	k;

	if ( n >= max || way_has(w, n, s) ) return n;
	for ( k = 0; s[k] != 0 && k < KB_ROMA_LEN - 1; k++ ) w[n].w[k] = s[k];
	w[n].w[k] = 0;
	w[n].len = len;
	return n + 1;
}

/* The table's ways for in[0..n) taken as one kana (len 1) or a kana and a small one (2) */
LOCAL INT table_ways( CONST UH *in, INT n, INT len, WAY *w, INT nw, INT max )
{
	INT	i, k;

	if ( len > n ) return nw;
	for ( i = 0; i < kb_nroma; i++ ) {
		CONST KBROMA	*r = &kb_roma[i];

		if ( r->cp[0] != hira(in[0]) ) continue;
		if ( len == 2 ? ( r->cp[1] != hira(in[1]) ) : ( r->cp[1] != 0 ) ) continue;
		for ( k = 0; k < KB_ROMA_WAYS && r->way[k][0] != 0; k++ ) nw = way_add(w, nw, max, r->way[k], len);
	}
	return nw;
}

LOCAL BOOL is_vowel( char c )
{
	return (BOOL)( c == 'a' || c == 'i' || c == 'u' || c == 'e' || c == 'o' );
}

/*
 * Every way the reading in[0..n) may begin: a kana and a small one
 * spelt as one, the kana alone, っ by the letter the next kana starts
 * with doubled, ん by a single n before what does not start with a
 * vowel, y or n.
 */
LOCAL INT ways( CONST UH *in, INT n, WAY *w, INT max )
{
	WAY	nx[16];
	INT	nw = 0, nn, i;

	if ( n <= 0 ) return 0;
	if ( in[0] == CP_SMALL_TU && n > 1 && in[1] != CP_SMALL_TU ) {
		nn = table_ways(in + 1, n - 1, 2, nx, 0, 16);
		nn = table_ways(in + 1, n - 1, 1, nx, nn, 16);
		for ( i = 0; i < nn; i++ ) {
			char	c[2];

			c[0] = nx[i].w[0];
			c[1] = 0;
			if ( c[0] >= 'a' && c[0] <= 'z' && !is_vowel(c[0]) && c[0] != 'n' ) nw = way_add(w, nw, max, c, 1);
		}
	}
	nw = table_ways(in, n, 2, w, nw, max);
	nw = table_ways(in, n, 1, w, nw, max);
	if ( hira(in[0]) == CP_N && n > 1 ) {
		BOOL	single = TRUE;

		nn = table_ways(in + 1, n - 1, 2, nx, 0, 16);
		nn = table_ways(in + 1, n - 1, 1, nx, nn, 16);
		for ( i = 0; i < nn; i++ ) {
			char	c = nx[i].w[0];

			if ( is_vowel(c) || c == 'y' || c == 'n' ) single = FALSE;
		}
		if ( nn > 0 && single ) nw = way_add(w, nw, max, "n", 1);
	}
	return nw;
}

/* Whether s (n letters) starts w */
LOCAL BOOL starts( CONST char *w, CONST char *s, INT n )
{
	INT	k;

	for ( k = 0; k < n; k++ ) {
		if ( w[k] != s[k] ) return FALSE;
	}
	return TRUE;
}

LOCAL INT slen( CONST char *s )
{
	INT	n = 0;

	while ( s[n] != 0 ) n++;
	return n;
}

/* The way shown for what is to come: the first that goes on from what is typed */
LOCAL CONST WAY *way_shown( CONST WAY *w, INT nw, CONST char *typed, INT nt )
{
	INT	i;

	for ( i = 0; i < nw; i++ ) {
		if ( slen(w[i].w) >= nt && starts(w[i].w, typed, nt) ) return &w[i];
	}
	return ( nw > 0 ) ? &w[0] : NULL;
}

LOCAL void roma_key( KPENG *e, UINT usage, UD now, KPRES *r )
{
	char	c = kb_roma_char(usage), buf[KB_ROMA_LEN * 2];
	WAY	w[24];
	INT	nw, i, tries;

	if ( c == 0 ) return;				/* a key that types no letter */
	r->has_char = TRUE;
	r->cp[0] = (UH)c;
	r->cp[1] = 0;
	for ( tries = 0; tries < 2; tries++ ) {
		CONST KPPHRASE	*p = &e->ph[e->cph];
		BOOL		exact = FALSE, longer = FALSE;
		INT		len = 0, nb = e->nrbuf;

		nw = ways(e->in + p->in0 + e->cch, p->nin - e->cch, w, 24);
		for ( i = 0; i < nb; i++ ) buf[i] = e->rbuf[i];
		buf[nb++] = c;
		buf[nb] = 0;
		for ( i = 0; i < nw; i++ ) {
			INT	wl = slen(w[i].w);

			if ( wl < nb || !starts(w[i].w, buf, nb) ) continue;
			if ( wl == nb ) {
				if ( !exact ) len = w[i].len;
				exact = TRUE;
			} else {
				longer = TRUE;
			}
		}
		if ( exact || longer ) {
			r->correct = 1;
			if ( exact && !longer ) {
				e->nrbuf = 0;
				e->cch += len;
				e->correct += len;
				advance(e, now, r);
			} else {
				for ( i = 0; i < nb && i < (INT)sizeof(e->rbuf) - 1; i++ ) e->rbuf[i] = buf[i];
				e->nrbuf = i;
			}
			return;
		}
		/* what was typed already spells a kana: that kana, and this letter for the next */
		len = 0;
		for ( i = 0; i < nw && e->nrbuf > 0; i++ ) {
			if ( slen(w[i].w) == e->nrbuf && starts(w[i].w, e->rbuf, e->nrbuf) ) {
				len = w[i].len;
				break;
			}
		}
		if ( len == 0 ) break;
		e->nrbuf = 0;
		e->cch += len;
		e->correct += len;
		advance(e, now, r);
		if ( !e->practising ) {
			r->correct = 1;			/* the last kana was what the letters before spelt */
			return;
		}
	}
	r->correct = 0;
	e->wrong++;
}

/* ---------------------------------------------------------------- a key */

EXPORT void kp_key( KPENG *e, UINT usage, BOOL lsh, BOOL rsh, CONST KBSET *held,
		    CONST KBLAYOUT *kb, UD now, KPRES *r )
{
	CONST KPPHRASE	*p;
	CONST UH	*in, *typed;
	EXPECT		x;
	UH		ch, base, mark;
	INT		n, k;

	zero(r, sizeof(*r));
	if ( !e->practising || e->completed || e->cph >= e->nph ) return;
	p = &e->ph[e->cph];
	in = e->in + p->in0 + e->cch;
	n = p->nin - e->cch;
	if ( e->mode == KP_M_KANA ) {
		if ( !expect_at(e, in, n, &x) ) return;
	} else if ( n <= 0 ) {
		return;
	}

	/* every key counts towards the keys, the hands and the distance */
	e->keys++;
	k = ( e->map != NULL ) ? kb_hand(e->map, usage) : 0;
	if ( k == 1 ) e->left++;
	else if ( k == 2 ) e->right++;
	if ( kb != NULL && e->last != 0 ) {
		INT	x0, y0, x1, y1;

		if ( kb_key_middle(kb, e->last, &x0, &y0) && kb_key_middle(kb, usage, &x1, &y1) ) {
			D	dx = x1 - x0, dy = y1 - y0;

			e->dist += (D)kb_sqrt((UD)( dx * dx + dy * dy ));
		}
	}
	e->last = usage;

	if ( e->mode == KP_M_ROMAJI ) {
		roma_key(e, usage, now, r);
		return;
	}

	/* a chord */
	if ( x.combo ) {
		BOOL	all = TRUE, mine = FALSE;

		for ( k = 0; k < x.cb->nkey; k++ ) {
			if ( x.cb->keys[k] == usage ) mine = TRUE;
			if ( held == NULL || !KB_IN(held, x.cb->keys[k]) ) all = FALSE;
		}
		if ( !mine ) {
			e->wrong++;
			r->combo_wrong = TRUE;
			return;
		}
		for ( k = 0; k < x.cb->nkey; k++ ) r->combo[k] = x.cb->keys[k];
		r->ncombo = x.cb->nkey;
		if ( !all ) {
			r->correct = -1;
			r->partial = TRUE;
			return;
		}
		e->correct++;
		e->cch += x.len;
		r->correct = 1;
		for ( k = 0; k < x.len && k < KB_CP - 1; k++ ) r->cp[k] = in[k];
		r->has_char = TRUE;
		advance(e, now, r);
		return;
	}

	/* one key, on the plane the shifts choose */
	typed = NULL;
	if ( lsh ) typed = kb_kana_of(e->map, usage, KB_LEFT);
	if ( typed == NULL && rsh ) typed = kb_kana_of(e->map, usage, KB_RIGHT);
	if ( typed == NULL ) typed = kb_kana_of(e->map, usage, KB_PLAIN);
	if ( typed == NULL ) return;
	for ( k = 0; typed[k] != 0 && k < KB_CP - 1; k++ ) r->cp[k] = typed[k];
	r->has_char = TRUE;
	ch = in[0];

	if ( typed[0] == ch && typed[1] == 0 ) {
		INT	i = rev_find(e, ch);

		e->correct++;
		if ( i >= 0 && e->rev[i].shift != KP_SH_NONE ) e->shift++;
		e->cch++;
		e->pending = 0;
		r->correct = 1;
		advance(e, now, r);
		return;
	}
	/* a kana and then its mark */
	if ( composed(e, ch, &base, &mark) ) {
		if ( typed[0] == base && typed[1] == 0 && e->pending == 0 ) {
			e->pending = ch;
			r->correct = -1;
			r->partial = TRUE;
			return;
		}
		if ( e->pending == ch && typed[0] == mark && typed[1] == 0 ) {
			INT	i = rev_find(e, mark);

			e->correct++;
			if ( mark == CP_HANDAKU || ( i >= 0 && e->rev[i].shift != KP_SH_NONE ) ) e->shift++;
			e->cch++;
			e->pending = 0;
			r->correct = 1;
			r->cp[0] = ch;
			r->cp[1] = 0;
			advance(e, now, r);
			return;
		}
	}
	e->pending = 0;
	e->wrong++;
	r->correct = 0;
}

/* ---------------------------------------------------------------- the keys to come */

LOCAL INT next_put( KPNEXT *o, INT n, INT max, UINT usage, INT shift, UH cp )
{
	if ( n >= max ) return n;
	zero(&o[n], sizeof(o[n]));
	o[n].usage = (UH)usage;
	o[n].shift = shift;
	o[n].cp[0] = cp;
	return n + 1;
}

LOCAL INT next_roma( KPENG *e, INT count, KPNEXT *out )
{
	WAY	w[24];
	INT	nout = 0, ph = e->cph, at = e->cch, nt = e->nrbuf;
	CONST char *typed = e->rbuf;

	while ( nout < count && ph < e->nph ) {
		CONST KPPHRASE	*p = &e->ph[ph];
		CONST WAY	*s;
		INT		nw, k;

		if ( at >= p->nin ) {
			ph++;
			at = 0;
			nt = 0;
			continue;
		}
		nw = ways(e->in + p->in0 + at, p->nin - at, w, 24);
		s = way_shown(w, nw, typed, nt);
		if ( s == NULL ) break;
		for ( k = nt; s->w[k] != 0 && nout < count; k++ ) {
			nout = next_put(out, nout, count, kb_roma_key(s->w[k]), KP_SH_NONE, (UH)s->w[k]);
		}
		at += s->len;
		nt = 0;
	}
	return nout;
}

EXPORT INT kp_next( KPENG *e, INT count, KPNEXT *out )
{
	INT	i, nout = 0, ph, at;

	if ( !e->practising || e->completed ) return 0;
	if ( count > KP_LOOK_MAX * 2 ) count = KP_LOOK_MAX * 2;
	if ( e->mode == KP_M_ROMAJI ) return next_roma(e, count, out);
	ph = e->cph;
	at = e->cch;
	for ( i = 0; i < count; i++ ) {
		CONST KPPHRASE	*p;
		CONST UH	*in;
		INT		n, len, r;
		BOOL		matched = FALSE;
		UH		ch, base, mark;

		if ( ph >= e->nph ) break;
		if ( at >= e->ph[ph].nin ) {
			ph++;
			if ( ph >= e->nph ) break;
			at = 0;
		}
		p = &e->ph[ph];
		in = e->in + p->in0 + at;
		n = p->nin - at;

		for ( len = e->maxcombo; len >= 1 && !matched; len-- ) {
			CONST KBCOMBO	*cb;

			if ( len > n ) continue;
			cb = combo_of(e, in, len);
			if ( cb != NULL && nout < count ) {
				INT	k;

				zero(&out[nout], sizeof(out[nout]));
				for ( k = 0; k < cb->nkey; k++ ) out[nout].keys[k] = cb->keys[k];
				out[nout].nkey = cb->nkey;
				for ( k = 0; k < len && k < KB_CP - 1; k++ ) out[nout].cp[k] = in[k];
				nout++;
				at += len;
				matched = TRUE;
			}
		}
		if ( matched ) continue;

		ch = in[0];
		if ( composed(e, ch, &base, &mark) ) {
			BOOL	here = (BOOL)( ph == e->cph && at == e->cch );

			if ( here && e->pending == ch ) {
				r = rev_find(e, mark);
				if ( r >= 0 ) nout = next_put(out, nout, count, e->rev[r].usage, e->rev[r].shift, mark);
			} else {
				r = rev_find(e, base);
				if ( r >= 0 ) nout = next_put(out, nout, count, e->rev[r].usage, e->rev[r].shift, base);
				if ( i + 1 < count ) {
					r = rev_find(e, mark);
					if ( r >= 0 ) nout = next_put(out, nout, count, e->rev[r].usage, e->rev[r].shift, mark);
					i++;
				}
			}
			at++;
		} else {
			r = rev_find(e, ch);
			if ( r >= 0 ) nout = next_put(out, nout, count, e->rev[r].usage, e->rev[r].shift, ch);
			at++;
		}
	}
	return nout;
}

EXPORT INT kp_expected_len( KPENG *e )
{
	CONST KPPHRASE	*p;
	EXPECT		x;
	INT		n;

	if ( !e->practising || e->completed || e->cph >= e->nph ) return 1;
	p = &e->ph[e->cph];
	n = p->nin - e->cch;
	if ( n <= 0 ) return 1;
	if ( e->mode == KP_M_ROMAJI ) {
		WAY		w[24];
		INT		nw = ways(e->in + p->in0 + e->cch, n, w, 24);
		CONST WAY	*s = way_shown(w, nw, e->rbuf, e->nrbuf);

		return ( s != NULL ) ? s->len : 1;
	}
	return expect_at(e, e->in + p->in0 + e->cch, n, &x) ? x.len : 1;
}

/* ---------------------------------------------------------------- the scores */

EXPORT void kp_score( KPENG *e, UD now, KPSCORE *s )
{
	D	ms = 0;
	INT	total = e->correct + e->wrong;

	zero(s, sizeof(*s));
	if ( e->started ) {
		UD	end = ( e->end != 0 ) ? e->end : now;

		ms = ( end > e->start ) ? (D)( end - e->start ) : 0;
	}
	s->speed = ( ms > 0 ) ? div_round((D)e->correct * 60000, ms) : 0;
	s->accuracy = ( total > 0 ) ? div_round((D)e->correct * 1000, total) : 1000;
	s->left = e->left;
	s->right = e->right;
	s->shift_ratio = ( e->correct > 0 ) ? div_round((D)e->shift * 1000, e->correct) : 0;
	s->distance = ( e->correct > 0 ) ? div_round(e->dist * 100, (D)e->correct * KB_UNIT) : 0;
	s->total = div_round(e->dist * 10, KB_UNIT);
	s->correct = e->correct;
	s->wrong = e->wrong;
	s->keys = e->keys;
	s->seconds = div_round(ms, 1000);
}

EXPORT void kp_progress( KPENG *e, INT *p_cur, INT *p_total )
{
	*p_cur = e->cq + 1;
	*p_total = e->npick;
}
