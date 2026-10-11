/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	mc_rx.c
 *	マイクロカード: regular expressions, wild letters, 異形字ゆらぎ同一視
 *	and the order of letters (design 11.18)
 *
 *	A pattern is read into a graph of nodes, each followed by the next of
 *	its sequence; a group holds the sequences it chooses between. What
 *	is to be done when a sequence ends -- go on after the group, or
 *	count one more of a repeat -- is carried down the calls, so a match
 *	needs nothing but where it is. A repeat of one letter is matched in
 *	a loop, taking as many as it can and giving them back one at a time;
 *	other repeats and groups go one call deeper for each time, and a
 *	match that takes too many steps or calls fails rather than run on.
 *
 *	A regular expression has . * + ? | ^ $ \ ( ) and the classes [ ],
 *	[a-z], [^ ] and [:名前:] of the character sets; a simple one (≒)
 *	has only * (any letters) ? (one letter) | and \. Both read
 *	&Txxyyyy; as the TRON character of plane xx, code yyyy. Both find a
 *	match anywhere in the text.
 *
 *	異形字ゆらぎ同一視 compares the letters each made the one it stands
 *	for: capitals small, katakana hiragana, full width letters of ASCII
 *	plain, half width katakana full width with their voiced marks joined,
 *	and a variant of a kanji -- an old form, a form of China or Taiwan, a
 *	compatibility ideograph -- the one of the table (mc_var.c).
 *
 *	The order of letters is the order of their TRON codes: the system's
 *	script first, in it the A zone (JIS X 0208 and X 0213's first plane,
 *	in JIS order, ASCII as row 3) before the B, C and D zones, then the
 *	other planes. A long sound mark after a kana is ordered as the vowel
 *	it lengthens (コート as コオト).
 */

#include "mc_db.h"
#include <ts/bpk.h>

#define RX_NODES	512		/* nodes of one pattern */
#define RX_RANGES	256		/* ranges of all its classes */
#define RX_STEPS	400000		/* steps of one search */
#define RX_DEPTH	1000		/* calls one match goes deeper */
#define RX_INF		0x7FFFFFFF
#define RX_NOCHAR	0x110001	/* a letter no text holds */
#define RX_NAMED	0xFF000000U	/* a range that is a character set: the sets in its low bits */

#define N_CHAR		1
#define N_ANY		2
#define N_CLASS		3		/* a: first range, b: how many; neg */
#define N_BOL		4
#define N_EOL		5
#define N_GROUP		6		/* sub: its sequences, joined by alt */
#define N_SEQ		7		/* sub: the first node of one sequence */
#define N_REP		8		/* sub: what; min, max */

typedef struct rxn {
	UB		kind;
	BOOL		neg;
	UINT		a;
	INT		b;
	INT		min, max;
	struct rxn	*sub, *alt, *next;
} RXN;

struct mcrx {
	RXN	node[RX_NODES];
	INT	nn;
	UINT	lo[RX_RANGES], hi[RX_RANGES];
	INT	nr;
	RXN	*start;
	UINT	flags;
	BOOL	bol;			/* it can match only at the start */
};

/* ---------------------------------------------------------------- folding */

/* Half width katakana from ｦ (FF66) to ﾝ (FF9D), as full width */
LOCAL CONST UH half_kana[] = {
	0x30F2, 0x30A1, 0x30A3, 0x30A5, 0x30A7, 0x30A9, 0x30E3, 0x30E5, 0x30E7, 0x30C3,
	0x30FC, 0x30A2, 0x30A4, 0x30A6, 0x30A8, 0x30AA, 0x30AB, 0x30AD, 0x30AF, 0x30B1,
	0x30B3, 0x30B5, 0x30B7, 0x30B9, 0x30BB, 0x30BD, 0x30BF, 0x30C1, 0x30C4, 0x30C6,
	0x30C8, 0x30CA, 0x30CB, 0x30CC, 0x30CD, 0x30CE, 0x30CF, 0x30D2, 0x30D5, 0x30D8,
	0x30DB, 0x30DE, 0x30DF, 0x30E0, 0x30E1, 0x30E2, 0x30E4, 0x30E6, 0x30E8, 0x30E9,
	0x30EA, 0x30EB, 0x30EC, 0x30ED, 0x30EF, 0x30F3
};

/* A letter's width made the usual: full width ASCII plain, half width kana full */
LOCAL UINT plain_width( UINT c )
{
	if ( c >= 0xFF01 && c <= 0xFF5E ) return c - 0xFEE0;
	if ( c >= 0xFF66 && c <= 0xFF9D ) return half_kana[c - 0xFF66];
	switch ( c ) {
	case 0xFF61:	return 0x3002;
	case 0xFF62:	return 0x300C;
	case 0xFF63:	return 0x300D;
	case 0xFF64:	return 0x3001;
	case 0xFF65:	return 0x30FB;
	case 0xFF9E:	return 0x309B;
	case 0xFF9F:	return 0x309C;
	default:	break;
	}
	return c;
}

LOCAL UINT var_of( UINT c )
{
	INT	lo = 0, hi = mc_var_n - 1;

	while ( lo <= hi ) {
		INT	mid = ( lo + hi ) / 2;

		if ( mc_var_tab[mid][0] == c ) return mc_var_tab[mid][1];
		if ( mc_var_tab[mid][0] < c ) lo = mid + 1;
		else hi = mid - 1;
	}
	return c;
}

EXPORT UINT mc_fold( UINT c )
{
	c = plain_width(c);
	if ( c == 0x3000 ) return ' ';
	if ( c >= 0x30A1 && c <= 0x30F6 ) return c - 0x60;	/* katakana as hiragana */
	if ( c == 0x30FD || c == 0x30FE ) return c - 0x60;
	if ( c >= 'A' && c <= 'Z' ) return c + 0x20;
	if ( c >= 0xC0 && c <= 0xDE && c != 0xD7 ) return c + 0x20;
	if ( c >= 0x0391 && c <= 0x03A9 && c != 0x03A2 ) return c + 0x20;
	if ( c >= 0x0410 && c <= 0x042F ) return c + 0x20;
	if ( c >= 0x0400 && c <= 0x040F ) return c + 0x50;
	if ( c == 0x2212 || c == 0x2010 || c == 0x2011 ) return '-';
	if ( c == 0x301C ) return '~';
	return var_of(c);
}

/* A hiragana with its voiced (or half voiced) mark, 0 when it has none */
LOCAL UINT voiced( UINT h, BOOL half )
{
	if ( half ) {
		if ( h == 0x306F || h == 0x3072 || h == 0x3075 || h == 0x3078 || h == 0x307B ) return h + 2;
		return 0;
	}
	if ( h == 0x3046 ) return 0x3094;
	if ( ( h >= 0x304B && h <= 0x3062 && ( h & 1 ) == 1 ) || h == 0x3064 || h == 0x3066 || h == 0x3068 ) return h + 1;
	if ( h == 0x306F || h == 0x3072 || h == 0x3075 || h == 0x3078 || h == 0x307B ) return h + 1;
	return 0;
}

/* ---------------------------------------------------------------- the order of letters */

EXPORT UINT mc_sort_key( UINT c, UINT prev )
{
	UH	t[2];
	INT	plane = 1, k;
	UINT	code, z, hi, lo;

	c = plain_width(c);
	if ( c == 0x30FC ) {
		/* a long sound: the vowel of the kana before it */
		UINT	p = plain_width(prev), h = ( p >= 0x30A1 && p <= 0x30F6 ) ? p - 0x60 : p;
		static CONST UH vowel_of[] = {
			/* ぁ あ ぃ い ぅ う ぇ え ぉ お か が き ぎ く ぐ け げ こ ご */
			0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4,
			/* さ ざ し じ す ず せ ぜ そ ぞ た だ ち ぢ っ つ づ て で と ど */
			0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 0, 0, 1, 1, 2, 2, 2, 3, 3, 4, 4,
			/* な に ぬ ね の は ば ぱ ひ び ぴ ふ ぶ ぷ へ べ ぺ ほ ぼ ぽ */
			0, 1, 2, 3, 4, 0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4,
			/* ま み む め も ゃ や ゅ ゆ ょ よ ら り る れ ろ ゎ わ ゐ ゑ を ん ゔ */
			0, 1, 2, 3, 4, 0, 0, 2, 2, 4, 4, 0, 1, 2, 3, 4, 0, 0, 1, 3, 4, 5, 2
		};
		static CONST UH vowel[5] = { 0x3042, 0x3044, 0x3046, 0x3048, 0x304A };

		if ( h >= 0x3041 && h <= 0x3094 && vowel_of[h - 0x3041] < 5 ) {
			c = vowel[vowel_of[h - 0x3041]];
			if ( p >= 0x30A1 && p <= 0x30F6 ) c += 0x60;
		}
	}
	if ( c < 0x20 ) return c;
	if ( c > 0xFFFF ) return 0x2000000U + c;
	k = bpk_ucs_tron(c, t, 2, &plane);
	if ( k <= 0 ) return 0x2000000U + c;
	code = t[k - 1];
	hi = code >> 8;
	lo = code & 0xFF;
	z = ( hi < 0x80 ) ? ( ( lo < 0x80 ) ? 0 : 2 ) : ( ( lo < 0x80 ) ? 1 : 3 );
	return ( (UINT)plane << 20 ) | ( z << 16 ) | code;
}

EXPORT INT mc_charset( UINT c )
{
	UH	t[2];
	INT	plane = 1, k;
	UINT	code, hi, lo;

	if ( c < 0x80 ) return MC_CS_JIS12;
	if ( c > 0xFFFF ) return ( c >= 0x20000 && c <= 0x3FFFF ) ? MC_CS_TAIWAN : MC_CS_OTHER;
	k = bpk_ucs_tron(c, t, 2, &plane);
	if ( k <= 0 ) return MC_CS_OTHER;
	code = t[k - 1];
	hi = code >> 8;
	lo = code & 0xFF;
	if ( plane == 1 ) {
		if ( hi < 0x80 && lo < 0x80 ) {
			INT	row = (INT)hi - 0x20, cell = (INT)lo - 0x20;

			/* JIS X 0208's kanji are rows 16 to 84 (84 to its sixth cell); X 0213 adds the rest */
			if ( row >= 9 && row <= 15 ) return MC_CS_JIS3;
			if ( row >= 85 || ( row == 84 && cell > 6 ) ) return MC_CS_JIS3;
			return MC_CS_JIS12;
		}
		if ( hi >= 0x80 && lo < 0x80 ) return ( hi <= 0xA0 ) ? MC_CS_JIS4 : MC_CS_SUPP;
		if ( hi < 0x80 ) return MC_CS_CHINA;
		return MC_CS_KOREA;
	}
	if ( ( c >= 0x4E00 && c <= 0x9FFF ) || ( c >= 0x3400 && c <= 0x4DBF ) || ( c >= 0xF900 && c <= 0xFAFF ) ) {
		return MC_CS_TAIWAN;
	}
	return MC_CS_OTHER;
}

/* ---------------------------------------------------------------- letters as numbers */

/* A text as its letters, and with 'fold' each made the one it stands for; how many */
LOCAL INT letters_of( CONST UB *s, INT n, BOOL fold, UINT *t, UINT *k )
{
	INT	i = 0, m = 0;
	UINT	c;

	while ( i < n ) {
		INT	l = mc_u8_get(s + i, n - i, &c);

		if ( l <= 0 ) break;
		i += l;
		if ( fold && m > 0 && ( c == 0xFF9E || c == 0xFF9F || c == 0x3099 || c == 0x309A ) ) {
			UINT	v = voiced(k[m - 1], (BOOL)( c == 0xFF9F || c == 0x309A ));

			if ( v != 0 ) {
				k[m - 1] = v;
				continue;
			}
		}
		t[m] = c;
		k[m] = fold ? mc_fold(c) : c;
		m++;
	}
	return m;
}

/* ---------------------------------------------------------------- reading a pattern */

typedef struct {
	CONST UINT	*p;
	INT		n, at;
	struct mcrx	*rx;
	BOOL		bad;
} RD;

LOCAL RXN *node_new( RD *r, UB kind )
{
	RXN	*nd;
	INT	i;

	if ( r->rx->nn >= RX_NODES ) {
		r->bad = TRUE;
		return NULL;
	}
	nd = &r->rx->node[r->rx->nn++];
	for ( i = 0; i < (INT)sizeof(*nd); i++ ) ( (UB *)nd )[i] = 0;
	nd->kind = kind;
	return nd;
}

LOCAL INT peek( RD *r )
{
	return ( r->at < r->n ) ? (INT)r->p[r->at] : -1;
}

LOCAL INT hexv( UINT c )
{
	if ( c >= '0' && c <= '9' ) return (INT)( c - '0' );
	if ( c >= 'a' && c <= 'f' ) return (INT)( c - 'a' + 10 );
	if ( c >= 'A' && c <= 'F' ) return (INT)( c - 'A' + 10 );
	return -1;
}

/* &Txxyyyy; at the place read: the letter, and the place moved past it; 0 when it is not one */
LOCAL UINT tron_at( RD *r )
{
	INT	i, v[6], plane;
	UINT	code, u;

	if ( r->at + 9 >= r->n + 1 || r->p[r->at] != '&' || r->at + 9 > r->n ) return 0;
	if ( r->p[r->at + 1] != 'T' && r->p[r->at + 1] != 't' ) return 0;
	for ( i = 0; i < 6; i++ ) {
		v[i] = hexv(r->p[r->at + 2 + i]);
		if ( v[i] < 0 ) return 0;
	}
	if ( r->p[r->at + 8] != ';' ) return 0;
	r->at += 9;
	plane = v[0] * 16 + v[1];
	code = (UINT)( ( v[2] << 12 ) | ( v[3] << 8 ) | ( v[4] << 4 ) | v[5] );
	u = bpk_tron_ucs(code, &plane);
	if ( u == 0 || u == 0x3013 ) return RX_NOCHAR;	/* a character Unicode does not have */
	return u;
}

LOCAL UINT lit( RD *r, UINT c )
{
	return ( r->rx->flags & MC_RX_FOLD ) ? mc_fold(c) : c;
}

LOCAL BOOL range_add( RD *r, UINT lo, UINT hi )
{
	if ( r->rx->nr >= RX_RANGES ) {
		r->bad = TRUE;
		return FALSE;
	}
	r->rx->lo[r->rx->nr] = lo;
	r->rx->hi[r->rx->nr] = hi;
	r->rx->nr++;
	return TRUE;
}

/* [:名前:] at the place read: the sets it names, the place moved past it; 0 when it is not one */
LOCAL UINT set_name( RD *r )
{
	static CONST struct {
		CONST char	*name;
		UINT		sets;
	} names[] = {
		{ "日本基本",	1U << MC_CS_JIS12 },
		{ "日本補助",	( 1U << MC_CS_SUPP ) | ( 1U << MC_CS_JIS3 ) | ( 1U << MC_CS_JIS4 ) },
		{ "JIS第1第2",	1U << MC_CS_JIS12 },
		{ "JIS第3",	1U << MC_CS_JIS3 },
		{ "JIS第4",	1U << MC_CS_JIS4 },
		{ "中国",	1U << MC_CS_CHINA },
		{ "韓国",	1U << MC_CS_KOREA },
		{ "台湾",	1U << MC_CS_TAIWAN },
		{ "各国",	1U << MC_CS_OTHER },
		{ "GT",		1U << 30 },		/* sets Unicode does not have: nothing matches */
		{ "大漢和",	1U << 30 }
	};
	INT	k, i, e;

	if ( r->at + 1 >= r->n || r->p[r->at] != '[' || r->p[r->at + 1] != ':' ) return 0;
	for ( e = r->at + 2; e + 1 < r->n && !( r->p[e] == ':' && r->p[e + 1] == ']' ); e++ ) ;
	if ( e + 1 >= r->n ) return 0;
	for ( k = 0; k < (INT)( sizeof(names) / sizeof(names[0]) ); k++ ) {
		CONST UB	*w = (CONST UB *)names[k].name;
		INT		wn = mc_len(w), at = 0;
		BOOL		ok = TRUE;

		for ( i = r->at + 2; i < e && ok; i++ ) {
			UINT	c;
			INT	l = mc_u8_get(w + at, wn - at, &c);

			if ( l <= 0 || c != r->p[i] ) ok = FALSE;
			at += l;
		}
		if ( ok && at == wn ) {
			r->at = e + 2;
			return names[k].sets;
		}
	}
	r->bad = TRUE;
	return 0;
}

LOCAL RXN *read_class( RD *r )
{
	RXN	*nd = node_new(r, N_CLASS);
	BOOL	first = TRUE;

	if ( nd == NULL ) return NULL;
	nd->a = (UINT)r->rx->nr;
	if ( peek(r) == '^' ) {
		nd->neg = TRUE;
		r->at++;
	}
	for ( ;; ) {
		INT	c = peek(r);
		UINT	lo, hi, sets, t;

		if ( c < 0 ) {
			r->bad = TRUE;
			return NULL;
		}
		if ( c == ']' && !first ) {
			r->at++;
			break;
		}
		first = FALSE;
		if ( c == '[' && ( sets = set_name(r) ) != 0 ) {
			if ( !range_add(r, RX_NAMED | sets, RX_NAMED | sets) ) return NULL;
			continue;
		}
		if ( r->bad ) return NULL;
		if ( c == '&' && ( t = tron_at(r) ) != 0 ) {
			lo = t;
		} else if ( c == '\\' && r->at + 1 < r->n ) {
			lo = r->p[r->at + 1];
			r->at += 2;
		} else {
			lo = (UINT)c;
			r->at++;
		}
		hi = lo;
		if ( peek(r) == '-' && r->at + 1 < r->n && r->p[r->at + 1] != ']' ) {
			r->at++;
			if ( peek(r) == '&' && ( t = tron_at(r) ) != 0 ) {
				hi = t;
			} else if ( peek(r) == '\\' && r->at + 1 < r->n ) {
				hi = r->p[r->at + 1];
				r->at += 2;
			} else {
				hi = (UINT)peek(r);
				r->at++;
			}
			if ( hi < lo ) {
				r->bad = TRUE;
				return NULL;
			}
		}
		if ( ( r->rx->flags & MC_RX_FOLD ) && lo == hi ) lo = hi = mc_fold(lo);
		else if ( r->rx->flags & MC_RX_FOLD ) {
			/* a range: each end as it stands for, when that keeps it a range */
			UINT	fl = mc_fold(lo), fh = mc_fold(hi);

			if ( fl <= fh ) {
				lo = fl;
				hi = fh;
			}
		}
		if ( !range_add(r, lo, hi) ) return NULL;
	}
	nd->b = r->rx->nr - (INT)nd->a;
	return nd;
}

LOCAL RXN *read_choice( RD *r );

LOCAL RXN *read_atom( RD *r )
{
	BOOL	simple = (BOOL)( ( r->rx->flags & MC_RX_SIMPLE ) != 0 );
	INT	c = peek(r);
	RXN	*nd;
	UINT	t;

	if ( c == '&' && ( t = tron_at(r) ) != 0 ) {
		nd = node_new(r, N_CHAR);
		if ( nd != NULL ) nd->a = lit(r, t);
		return nd;
	}
	r->at++;
	if ( c == '\\' ) {
		if ( r->at >= r->n ) {
			r->bad = TRUE;
			return NULL;
		}
		nd = node_new(r, N_CHAR);
		if ( nd != NULL ) nd->a = lit(r, r->p[r->at++]);
		return nd;
	}
	if ( simple ) {
		if ( c == '*' ) {
			RXN	*any = node_new(r, N_ANY);

			nd = node_new(r, N_REP);
			if ( nd == NULL || any == NULL ) return NULL;
			nd->sub = any;
			nd->min = 0;
			nd->max = RX_INF;
			return nd;
		}
		if ( c == '?' ) return node_new(r, N_ANY);
		nd = node_new(r, N_CHAR);
		if ( nd != NULL ) nd->a = lit(r, (UINT)c);
		return nd;
	}
	switch ( c ) {
	case '(':
		nd = read_choice(r);
		if ( nd == NULL || peek(r) != ')' ) {
			r->bad = TRUE;
			return NULL;
		}
		r->at++;
		return nd;
	case '[':
		return read_class(r);
	case '.':
		return node_new(r, N_ANY);
	case '^':
		return node_new(r, N_BOL);
	case '$':
		return node_new(r, N_EOL);
	case ')':
	case '*':
	case '+':
	case '?':
		r->bad = TRUE;
		return NULL;
	default:
		nd = node_new(r, N_CHAR);
		if ( nd != NULL ) nd->a = lit(r, (UINT)c);
		return nd;
	}
}

LOCAL RXN *read_piece( RD *r )
{
	RXN	*a = read_atom(r);

	while ( a != NULL && !( r->rx->flags & MC_RX_SIMPLE ) ) {
		INT	c = peek(r);
		RXN	*q;

		if ( c != '*' && c != '+' && c != '?' ) break;
		if ( a->kind == N_BOL || a->kind == N_EOL ) {
			r->bad = TRUE;
			return NULL;
		}
		r->at++;
		q = node_new(r, N_REP);
		if ( q == NULL ) return NULL;
		q->sub = a;
		q->min = ( c == '+' ) ? 1 : 0;
		q->max = ( c == '?' ) ? 1 : RX_INF;
		a = q;
	}
	return a;
}

LOCAL RXN *read_seq( RD *r )
{
	RXN	*head = NULL, *tail = NULL;

	while ( !r->bad ) {
		INT	c = peek(r);
		RXN	*p;

		if ( c < 0 || c == '|' || ( c == ')' && !( r->rx->flags & MC_RX_SIMPLE ) ) ) break;
		p = read_piece(r);
		if ( p == NULL ) break;
		if ( tail == NULL ) head = p;
		else tail->next = p;
		tail = p;
	}
	return head;
}

LOCAL RXN *read_choice( RD *r )
{
	RXN	*g = node_new(r, N_GROUP), *last = NULL;

	if ( g == NULL ) return NULL;
	for ( ;; ) {
		RXN	*s = node_new(r, N_SEQ);

		if ( s == NULL ) return NULL;
		s->sub = read_seq(r);
		if ( r->bad ) return NULL;
		if ( last == NULL ) g->sub = s;
		else last->alt = s;
		last = s;
		if ( peek(r) != '|' ) break;
		r->at++;
	}
	return g;
}

EXPORT ER mc_rx_make( CONST UB *pat, INT n, UINT flags, MCRX **p_rx )
{
	struct mcrx	*rx;
	UINT		*p;
	RD		r;
	INT		i = 0, m = 0;

	*p_rx = NULL;
	rx = (struct mcrx *)mc_alloc(sizeof(struct mcrx));
	p = (UINT *)mc_alloc((INT)sizeof(UINT) * ( n + 1 ));
	if ( rx == NULL || p == NULL ) {
		mc_free(rx);
		mc_free(p);
		return E_NOMEM;
	}
	while ( i < n ) {
		UINT	c;
		INT	l = mc_u8_get(pat + i, n - i, &c);

		if ( l <= 0 ) break;
		p[m++] = c;
		i += l;
	}
	rx->nn = 0;
	rx->nr = 0;
	rx->flags = flags;
	r.p = p;
	r.n = m;
	r.at = 0;
	r.rx = rx;
	r.bad = FALSE;
	rx->start = read_choice(&r);
	if ( !r.bad && r.at < r.n ) r.bad = TRUE;	/* a ')' with no '(' */
	mc_free(p);
	if ( r.bad || rx->start == NULL ) {
		mc_free(rx);
		return ( rx->nn >= RX_NODES ) ? E_LIMIT : E_PAR;
	}
	/* a pattern each of whose ways begins with ^ is tried at the start only */
	{
		RXN	*s;

		rx->bol = TRUE;
		for ( s = rx->start->sub; s != NULL; s = s->alt ) {
			if ( s->sub == NULL || s->sub->kind != N_BOL ) rx->bol = FALSE;
		}
	}
	*p_rx = rx;
	return E_OK;
}

EXPORT void mc_rx_free( MCRX *rx )
{
	mc_free(rx);
}

/* ---------------------------------------------------------------- matching */

typedef struct {
	CONST struct mcrx *rx;
	CONST UINT	*t, *k;		/* the letters, and as compared */
	INT		n;
	INT		steps, depth;
	BOOL		over;
} MATCH;

#define K_NEXT		0		/* go on with nd, then up */
#define K_REP		1		/* one more of the repeat nd is done */

typedef struct kont {
	INT		kind;
	CONST RXN	*nd;
	CONST struct kont *up;
	INT		cnt, start;
} KONT;

LOCAL BOOL in_class( CONST MATCH *m, CONST RXN *nd, INT pos )
{
	UINT	c = m->k[pos];
	INT	i;
	BOOL	in = FALSE;

	for ( i = (INT)nd->a; i < (INT)nd->a + nd->b && !in; i++ ) {
		UINT	lo = m->rx->lo[i];

		if ( ( lo & RX_NAMED ) == RX_NAMED ) {
			in = (BOOL)( ( ( lo & ~RX_NAMED ) & ( 1U << mc_charset(m->t[pos]) ) ) != 0 );
		} else {
			in = (BOOL)( c >= lo && c <= m->rx->hi[i] );
		}
	}
	return nd->neg ? (BOOL)!in : in;
}

LOCAL BOOL one( CONST MATCH *m, CONST RXN *nd, INT pos )
{
	if ( pos >= m->n ) return FALSE;
	switch ( nd->kind ) {
	case N_CHAR:	return (BOOL)( m->k[pos] == nd->a );
	case N_ANY:	return TRUE;
	case N_CLASS:	return in_class(m, nd, pos);
	default:	return FALSE;
	}
}

LOCAL INT walk( MATCH *m, CONST RXN *nd, INT pos, CONST KONT *k );

LOCAL INT rep( MATCH *m, CONST RXN *r, INT pos, INT cnt, CONST KONT *after )
{
	CONST RXN	*s = r->sub;
	INT		x;

	if ( ( s->kind == N_CHAR || s->kind == N_ANY || s->kind == N_CLASS ) && s->next == NULL ) {
		/* one letter repeated: as many as match, given back one at a time */
		INT	most = 0, i;

		while ( pos + most < m->n && cnt + most < r->max && one(m, s, pos + most) ) most++;
		for ( i = most; i >= 0; i-- ) {
			if ( cnt + i < r->min ) break;
			x = walk(m, NULL, pos + i, after);
			if ( x >= 0 || m->over ) return x;
		}
		return -1;
	}
	if ( cnt < r->max ) {
		KONT	kk;

		if ( ++m->depth > RX_DEPTH ) {
			m->over = TRUE;
			return -1;
		}
		kk.kind = K_REP;
		kk.nd = r;
		kk.up = after;
		kk.cnt = cnt;
		kk.start = pos;
		x = walk(m, s, pos, &kk);
		m->depth--;
		if ( x >= 0 || m->over ) return x;
	}
	if ( cnt >= r->min ) return walk(m, NULL, pos, after);
	return -1;
}

LOCAL INT walk( MATCH *m, CONST RXN *nd, INT pos, CONST KONT *k )
{
	for ( ;; ) {
		if ( ++m->steps > RX_STEPS ) {
			m->over = TRUE;
			return -1;
		}
		if ( nd == NULL ) {
			if ( k == NULL ) return pos;
			if ( k->kind == K_NEXT ) {
				nd = k->nd;
				k = k->up;
				continue;
			}
			/* a pass of a repeat that took nothing goes no further */
			if ( pos == k->start ) return -1;
			return rep(m, k->nd, pos, k->cnt + 1, k->up);
		}
		switch ( nd->kind ) {
		case N_CHAR:
		case N_ANY:
		case N_CLASS:
			if ( !one(m, nd, pos) ) return -1;
			pos++;
			nd = nd->next;
			continue;
		case N_BOL:
			if ( pos != 0 ) return -1;
			nd = nd->next;
			continue;
		case N_EOL:
			if ( pos != m->n ) return -1;
			nd = nd->next;
			continue;
		case N_GROUP:
			{
				KONT		kk;
				CONST RXN	*s;
				INT		x;

				if ( ++m->depth > RX_DEPTH ) {
					m->over = TRUE;
					return -1;
				}
				kk.kind = K_NEXT;
				kk.nd = nd->next;
				kk.up = k;
				kk.cnt = 0;
				kk.start = 0;
				for ( s = nd->sub; s != NULL; s = s->alt ) {
					x = walk(m, s->sub, pos, &kk);
					if ( x >= 0 || m->over ) {
						m->depth--;
						return x;
					}
				}
				m->depth--;
				return -1;
			}
		case N_REP:
			{
				KONT	kk;

				kk.kind = K_NEXT;
				kk.nd = nd->next;
				kk.up = k;
				kk.cnt = 0;
				kk.start = 0;
				return rep(m, nd, pos, 0, &kk);
			}
		default:
			return -1;
		}
	}
}

EXPORT BOOL mc_rx_find( CONST MCRX *rx, CONST UB *s, INT n )
{
	UINT	*t, *k;
	MATCH	m;
	INT	from, found = -1;

	if ( rx == NULL ) return FALSE;
	t = (UINT *)mc_alloc((INT)sizeof(UINT) * ( n + 1 ) * 2);
	if ( t == NULL ) return FALSE;
	k = t + n + 1;
	m.rx = rx;
	m.t = t;
	m.k = k;
	m.n = letters_of(s, n, (BOOL)( ( rx->flags & MC_RX_FOLD ) != 0 ), t, k);
	m.steps = 0;
	m.over = FALSE;
	for ( from = 0; from <= m.n && found < 0 && !m.over; from++ ) {
		m.depth = 0;
		found = walk(&m, rx->start, from, NULL);
		if ( rx->bol ) break;
	}
	mc_free(t);
	return (BOOL)( found >= 0 );
}

/* ---------------------------------------------------------------- wild letters */

EXPORT BOOL mc_wild( CONST UB *pat, INT pn, CONST UB *s, INT n )
{
	INT	pi = 0, si = 0, star_p = -1, star_s = 0;

	while ( si < n ) {
		UINT	pc = 0, sc;
		INT	pl = ( pi < pn ) ? mc_u8_get(pat + pi, pn - pi, &pc) : 0;
		INT	sl = mc_u8_get(s + si, n - si, &sc);

		if ( pl > 0 && pc == '*' ) {
			star_p = pi + pl;
			star_s = si;
			pi += pl;
			continue;
		}
		if ( pl > 0 && ( pc == '?' || pc == sc ) ) {
			pi += pl;
			si += sl;
			continue;
		}
		if ( star_p >= 0 ) {
			/* the last * takes one letter more */
			UINT	c;

			star_s += mc_u8_get(s + star_s, n - star_s, &c);
			si = star_s;
			pi = star_p;
			continue;
		}
		return FALSE;
	}
	while ( pi < pn && pat[pi] == '*' ) pi++;
	return (BOOL)( pi == pn );
}
