/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cs_dic.c
 *	文字検索: the dictionary and the two searches (design 11.19)
 *
 *	The dictionary is looked at where it lies in memory. Its entries
 *	are in the order of the code points, so a character is found by
 *	halving; a search goes through them all, which for some 34,000
 *	entries takes a few milliseconds, and sorts what it found by the
 *	place each entry has in the order of readings.
 *
 *	A kanji's elements are a list of the characters it is made of and
 *	how many times each occurs, at every level (林, and the two 木 in it,
 *	are both elements of 森). An element given in a condition is met by
 *	a kanji that has it, or one of its variants, among its elements; a
 *	kanji that is an element of nothing is met when all its own
 *	elements are there. The elements of the first level say which
 *	kanji are made of the elements given and no others.
 */

#include "cs_dic.h"

#define HEAD_SIZE	64
#define ENT_SIZE	20
#define ELEM_SIZE	8

#define MAX_TERM	16
#define MAX_ALT		12
#define MAX_READ	48		/* bytes of a reading */
#define MAX_TOKEN	64		/* characters of a condition */

LOCAL UINT rd32( CONST UB *p )
{
	return (UINT)p[0] | ( (UINT)p[1] << 8 ) | ( (UINT)p[2] << 16 ) | ( (UINT)p[3] << 24 );
}

LOCAL UH rd16( CONST UB *p )
{
	return (UH)( p[0] | ( p[1] << 8 ) );
}

/* ---------------------------------------------------------------- the dictionary */

EXPORT ER cd_open( CDIC *d, CONST void *rec, SZ size )
{
	CONST UB	*b = (CONST UB *)rec;
	CONST char	*magic = "TSCHDIC1";
	UINT		n, eo, bo, bs, lo, ln, jo;
	INT		i;

	if ( d == NULL || b == NULL || size < HEAD_SIZE ) return E_OBJ;
	for ( i = 0; i < 8; i++ ) {
		if ( b[i] != (UB)magic[i] ) return E_OBJ;
	}
	n = rd32(b + 8);
	eo = rd32(b + 12);
	bo = rd32(b + 16);
	bs = rd32(b + 20);
	lo = rd32(b + 24);
	ln = rd32(b + 28);
	jo = rd32(b + 32);
	if ( ( eo & 3 ) != 0 || (SZ)eo + (SZ)n * ENT_SIZE > size || (SZ)bo + (SZ)bs > size
	  || (SZ)lo + (SZ)ln * ELEM_SIZE > size || (SZ)jo + ( 94 * 94 + 7 ) / 8 > size || n >= 65536 ) {
		return E_OBJ;
	}
	d->base = b;
	d->size = size;
	d->ent = (CONST CDENT *)( b + eo );
	d->n = (INT)n;
	d->data = b + bo;
	d->dsize = bs;
	d->elem = b + lo;
	d->nelem = (INT)ln;
	d->jis208 = b + jo;
	d->src = rd32(b + 36);
	return E_OK;
}

EXPORT CONST CDENT *cd_find( CONST CDIC *d, UINT cp )
{
	INT	lo = 0, hi = d->n - 1;

	while ( lo <= hi ) {
		INT	mid = ( lo + hi ) / 2;
		UINT	c = d->ent[mid].cp;

		if ( c == cp ) return &d->ent[mid];
		if ( c < cp ) lo = mid + 1;
		else hi = mid - 1;
	}
	return NULL;
}

EXPORT INT cd_field( CONST CDIC *d, CONST CDENT *e, INT tag, CONST UB **p_at )
{
	UINT	at;

	if ( e == NULL ) return -1;
	at = e->data;
	while ( at + 3 <= d->dsize && d->data[at] != 0 ) {
		INT	len = rd16(d->data + at + 1);

		if ( at + 3 + (UINT)len > d->dsize ) break;
		if ( d->data[at] == tag ) {
			*p_at = d->data + at + 3;
			return len;
		}
		at += 3 + (UINT)len;
	}
	return -1;
}

EXPORT UINT cd_elem( CONST CDIC *d, INT i, UINT *p_shown, INT *p_strokes )
{
	CONST UB	*p;

	if ( i < 0 || i >= d->nelem ) return 0;
	p = d->elem + i * ELEM_SIZE;
	if ( p_shown != NULL ) *p_shown = rd16(p + 4);
	if ( p_strokes != NULL ) *p_strokes = p[6];
	return rd32(p);
}

EXPORT INT cd_strokes( CONST CDIC *d, UINT cp )
{
	CONST CDENT	*e = cd_find(d, cp);
	INT		i, s;

	if ( e != NULL && e->strokes != 0 ) return e->strokes;
	for ( i = 0; i < d->nelem; i++ ) {
		if ( cd_elem(d, i, NULL, &s) == cp ) return s;
	}
	return 0;
}

/* The 214 radicals of the 康熙字典, as the ideographs they are */
LOCAL CONST char radicals[] =
	"一丨丶丿乙亅二亠人儿入八冂冖冫几凵刀力勹匕匚匸十卜卩厂厶又口囗土士夂夊夕大女子宀寸小尢尸屮山巛工己巾干"
	"幺广廴廾弋弓彐彡彳心戈戶手支攴文斗斤方无日曰月木欠止歹殳毋比毛氏气水火爪父爻爿片牙牛犬玄玉瓜瓦甘生用田"
	"疋疒癶白皮皿目矛矢石示禸禾穴立竹米糸缶网羊羽老而耒耳聿肉臣自至臼舌舛舟艮色艸虍虫血行衣襾見角言谷豆豕豸"
	"貝赤走足身車辛辰辵邑酉釆里金長門阜隶隹雨靑非面革韋韭音頁風飛食首香馬骨高髟鬥鬯鬲鬼魚鳥鹵鹿麥麻黃黍黑黹"
	"黽鼎鼓鼠鼻齊齒龍龜龠";

EXPORT void cd_radical( INT n, char *s )
{
	INT	i;

	s[0] = 0;
	if ( n < 1 || n > 214 ) return;
	for ( i = 0; i < 3; i++ ) s[i] = radicals[( n - 1 ) * 3 + i];
	s[3] = 0;
}

/* ---------------------------------------------------------------- UTF-8 */

EXPORT INT cs_utf8_next( CONST UB *s, UINT *p_cp )
{
	UINT	c = s[0];
	INT	n, i;

	if ( c == 0 ) return 0;
	if ( c < 0x80 ) {
		*p_cp = c;
		return 1;
	}
	n = ( c >= 0xF0 ) ? 4 : ( c >= 0xE0 ) ? 3 : ( c >= 0xC0 ) ? 2 : 0;
	if ( n == 0 ) {
		*p_cp = 0xFFFD;
		return 1;
	}
	*p_cp = c & ( 0x3FU >> ( n - 1 ) );
	for ( i = 1; i < n; i++ ) {
		if ( ( s[i] & 0xC0 ) != 0x80 ) {
			*p_cp = 0xFFFD;
			return i;
		}
		*p_cp = ( *p_cp << 6 ) | ( s[i] & 0x3F );
	}
	return n;
}

EXPORT INT cs_utf8_put( UINT cp, char *o )
{
	if ( cp < 0x80 ) {
		o[0] = (char)cp;
		return 1;
	}
	if ( cp < 0x800 ) {
		o[0] = (char)( 0xC0 | ( cp >> 6 ) );
		o[1] = (char)( 0x80 | ( cp & 0x3F ) );
		return 2;
	}
	if ( cp < 0x10000 ) {
		o[0] = (char)( 0xE0 | ( cp >> 12 ) );
		o[1] = (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
		o[2] = (char)( 0x80 | ( cp & 0x3F ) );
		return 3;
	}
	o[0] = (char)( 0xF0 | ( cp >> 18 ) );
	o[1] = (char)( 0x80 | ( ( cp >> 12 ) & 0x3F ) );
	o[2] = (char)( 0x80 | ( ( cp >> 6 ) & 0x3F ) );
	o[3] = (char)( 0x80 | ( cp & 0x3F ) );
	return 4;
}

/*
 * A condition's characters as they are compared: full width letters,
 * digits and signs as ASCII, the ideographic space as a space, the
 * ideographic full stop as a full stop, the wave dashes as ~
 */
LOCAL UINT plain( UINT c )
{
	if ( c >= 0xFF01 && c <= 0xFF5D ) return c - 0xFF01 + 0x21;
	if ( c == 0xFF5E || c == 0x301C ) return '~';
	if ( c == 0x3000 ) return ' ';
	if ( c == 0x3002 ) return '.';
	if ( c == 0x2212 || c == 0x2010 || c == 0x2015 ) return '-';
	if ( c >= 'a' && c <= 'z' ) return c - 0x20;
	return c;
}

LOCAL BOOL is_hira( UINT c )
{
	return (BOOL)( ( c >= 0x3041 && c <= 0x3096 ) || c == 0x30FC || c == 0x309D || c == 0x309E );
}

LOCAL BOOL is_kata( UINT c )
{
	return (BOOL)( c >= 0x30A1 && c <= 0x30FA );
}

LOCAL BOOL is_digit( UINT c )
{
	return (BOOL)( c >= '0' && c <= '9' );
}

/* The characters of s (UTF-8) as compared, at most max; how many */
LOCAL INT decode( CONST char *s, UINT *out, INT max )
{
	CONST UB	*p = (CONST UB *)s;
	INT		n = 0, k;
	UINT		c;

	while ( n < max && ( k = cs_utf8_next(p, &c) ) > 0 ) {
		out[n++] = plain(c);
		p += k;
	}
	return n;
}

/* ---------------------------------------------------------------- 漢字検索 */

#define TM_READ		1		/* a reading */
#define TM_ELEM		2		/* an element (x), n times (0: any), less y */
#define TM_NSTROKE	3		/* an element of n strokes */
#define TM_RANGE	4		/* the strokes of the whole, lo..hi */
#define TM_EXACT	5

typedef struct {
	INT	kind;
	UINT	x, y;
	INT	n, lo, hi;
	UB	read[MAX_READ];
	INT	nread;
} TERM;

/* The elements like a katakana: the radicals and parts it looks like */
typedef struct {
	UH	kana;
	UH	like[3];
} KANALIKE;

LOCAL CONST KANALIKE kana_like[] = {
	{ 0x30A4, { 0x4EBB, 0x30A4, 0 } },		/* イ 亻 */
	{ 0x30CE, { 0x4E3F, 0x30CE, 0 } },		/* ノ 丿 */
	{ 0x30CF, { 0x516B, 0x30CF, 0 } },		/* ハ 八 */
	{ 0x30E8, { 0x5F50, 0x30E8, 0 } },		/* ヨ 彐 */
	{ 0x30E0, { 0x5382, 0x30E0, 0 } },		/* ム 厶 */
	{ 0x30C8, { 0x535C, 0, 0 } },			/* ト 卜 */
	{ 0x30ED, { 0x53E3, 0, 0 } },			/* ロ 口 */
	{ 0x30A8, { 0x5DE5, 0, 0 } },			/* エ 工 */
	{ 0x30AB, { 0x529B, 0, 0 } },			/* カ 力 */
	{ 0x30BF, { 0x5915, 0, 0 } },			/* タ 夕 */
	{ 0x30CB, { 0x4E8C, 0, 0 } },			/* ニ 二 */
	{ 0x30D2, { 0x5315, 0, 0 } },			/* ヒ 匕 */
	{ 0x30E1, { 0x4E42, 0, 0 } },			/* メ 乂 */
	{ 0x30AF, { 0x52F9, 0, 0 } },			/* ク 勹 */
	{ 0x30CC, { 0x53C8, 0, 0 } },			/* ヌ 又 */
	{ 0x30CD, { 0x793B, 0, 0 } },			/* ネ 礻 */
	{ 0x30EA, { 0x5202, 0, 0 } },			/* リ 刂 */
	{ 0x30B7, { 0x6C35, 0, 0 } },			/* シ 氵 */
	{ 0x30BD, { 0x4E37, 0, 0 } },			/* ソ 丷 */
	{ 0x30B5, { 0x8279, 0, 0 } },			/* サ 艹 */
	{ 0x30EB, { 0x513F, 0, 0 } },			/* ル 儿 */
	{ 0x30EF, { 0x5196, 0, 0 } },			/* ワ 冖 */
	{ 0x30A6, { 0x5B80, 0, 0 } },			/* ウ 宀 */
	{ 0x30DF, { 0x5F61, 0, 0 } },			/* ミ 彡 */
	{ 0x30DE, { 0x30DE, 0, 0 } },			/* マ */
	{ 0x30E6, { 0x30E6, 0, 0 } },			/* ユ */
	{ 0x30CA, { 0x30CA, 0, 0 } },			/* ナ */
	{ 0x30C1, { 0x5343, 0, 0 } },			/* チ 千 */
	{ 0x30C6, { 0x4E8D, 0, 0 } },			/* テ 亍 */
	{ 0x30DB, { 0x6728, 0, 0 } },			/* ホ 木 */
	{ 0, { 0, 0, 0 } }
};

/* What an element given in a condition may be: itself, its variants, what a katakana looks like */
LOCAL INT alts( CONST CDIC *d, UINT x, UINT *a )
{
	CONST UB	*p;
	INT		n = 0, len, i;

	if ( is_kata(x) ) {
		for ( i = 0; kana_like[i].like[0] != 0; i++ ) {
			if ( kana_like[i].kana == x ) {
				INT	k;

				for ( k = 0; k < 3 && kana_like[i].like[k] != 0; k++ ) a[n++] = kana_like[i].like[k];
				return n;
			}
		}
		a[n++] = x;
		return n;
	}
	a[n++] = x;
	len = cd_field(d, cd_find(d, x), CD_T_VARS, &p);
	for ( i = 0; i + 4 <= len && n < MAX_ALT; i += 4 ) {
		UINT	v = rd32(p + i);
		INT	kind = (INT)( v >> 24 );

		if ( kind >= CD_V_ITAI && kind <= CD_V_NEW ) a[n++] = v & 0xFFFFFF;
	}
	return n;
}

/* How many times an element occurs in a list of elements (CD_T_ELEMS) */
LOCAL INT count_in( CONST UB *el, INT len, UINT x )
{
	INT	i;

	for ( i = 0; i + 4 <= len; i += 4 ) {
		UINT	v = rd32(el + i);

		if ( ( v >> 8 ) == x ) return (INT)( v & 0xFF );
	}
	return 0;
}

/* Whether every element of a's list is in t's */
LOCAL BOOL all_in( CONST UB *a, INT na, CONST UB *t, INT nt )
{
	INT	i;

	if ( na <= 0 ) return FALSE;
	for ( i = 0; i + 4 <= na; i += 4 ) {
		if ( count_in(t, nt, rd32(a + i) >> 8) == 0 ) return FALSE;
	}
	return TRUE;
}

/* Whether a kanji t (its elements el) has element x, n times (0: once or more) */
LOCAL BOOL has_elem( CONST CDIC *d, CONST CDENT *t, CONST UB *el, INT nel, UINT x, INT n )
{
	UINT	a[MAX_ALT];
	INT	na = alts(d, x, a), i;

	for ( i = 0; i < na; i++ ) {
		INT	c;

		if ( a[i] == t->cp ) {
			if ( n <= 1 ) return TRUE;
			continue;
		}
		c = count_in(el, nel, a[i]);
		if ( n >= 2 ) {
			if ( c == n ) return TRUE;
			continue;
		}
		if ( c >= 1 ) return TRUE;
		{
			CONST CDENT	*e = cd_find(d, a[i]);
			CONST UB	*ae;
			INT		nae;

			/* a kanji that is an element of nothing: all its own elements */
			if ( e != NULL && ( e->flags & CD_F_ELEM ) == 0
			  && ( nae = cd_field(d, e, CD_T_ELEMS, &ae) ) > 0 && all_in(ae, nae, el, nel) ) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* Whether x is among the alternatives of an element term, or is one of their elements */
LOCAL BOOL given( CONST CDIC *d, CONST TERM *tm, INT nt, UINT x, BOOL deep )
{
	INT	i, k;

	for ( i = 0; i < nt; i++ ) {
		UINT	a[MAX_ALT];
		INT	na;

		if ( tm[i].kind != TM_ELEM || tm[i].y != 0 ) continue;
		na = alts(d, tm[i].x, a);
		for ( k = 0; k < na; k++ ) {
			CONST UB	*el;
			INT		nel;

			if ( a[k] == x ) return TRUE;
			if ( deep && ( nel = cd_field(d, cd_find(d, a[k]), CD_T_ELEMS, &el) ) > 0
			  && count_in(el, nel, x) > 0 ) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

/* Whether a reading of the entry is r: 音 as it is, 訓 with or without its okurigana */
LOCAL BOOL has_reading( CONST CDIC *d, CONST CDENT *e, CONST UB *r, INT nr )
{
	CONST UB	*p;
	INT		len, i, k;

	len = cd_field(d, e, CD_T_ON, &p);
	for ( i = 0; i < len; ) {
		for ( k = i; k < len && p[k] != ' '; k++ ) ;
		if ( k - i == nr ) {
			INT	j;

			for ( j = 0; j < nr && p[i + j] == r[j]; j++ ) ;
			if ( j == nr ) return TRUE;
		}
		i = k + 1;
	}
	len = cd_field(d, e, CD_T_KUN, &p);
	for ( i = 0; i < len; ) {
		UB	whole[MAX_READ], stem[MAX_READ];
		INT	nw = 0, ns = 0, m, j;
		BOOL	dot = FALSE;

		for ( k = i; k < len && p[k] != ' '; k++ ) ;
		/* the reading with the marks of a prefix or suffix and the okurigana's dot taken out, and its stem */
		for ( m = i; m < k && nw < MAX_READ; m++ ) {
			if ( p[m] == '-' ) continue;
			if ( p[m] == '.' ) {
				dot = TRUE;
				continue;
			}
			whole[nw++] = p[m];
			if ( !dot ) stem[ns++] = p[m];
		}
		if ( nw == nr ) {
			for ( j = 0; j < nr && whole[j] == r[j]; j++ ) ;
			if ( j == nr ) return TRUE;
		}
		if ( dot && ns == nr ) {
			for ( j = 0; j < nr && stem[j] == r[j]; j++ ) ;
			if ( j == nr ) return TRUE;
		}
		i = k + 1;
	}
	return FALSE;
}

/* A number at s[*p_i], read and passed: its value, -1 for none */
LOCAL INT number( CONST UINT *s, INT n, INT *p_i )
{
	INT	v = -1;

	while ( *p_i < n && is_digit(s[*p_i]) ) {
		v = ( v < 0 ? 0 : v * 10 ) + (INT)( s[*p_i] - '0' );
		if ( v > 999 ) v = 999;
		( *p_i )++;
	}
	return v;
}

/* One condition, as one or more terms: how many, -1 when it cannot be read */
LOCAL INT term_of( CONST UINT *s, INT n, TERM *tm, INT room )
{
	INT	i = 0, k = 0, v, m;

	if ( room <= 0 ) return 0;
	for ( v = 0; v < (INT)sizeof(TERM); v++ ) ( (UB *)tm )[v] = 0;
	if ( s[0] == '=' ) {
		i = 1;
		v = number(s, n, &i);
		if ( v < 0 ) return -1;
		tm->kind = TM_RANGE;
		tm->lo = tm->hi = v;
		if ( i == n ) return 1;
		if ( s[i] == '+' && i + 1 < n && s[i + 1] == '-' ) {
			i += 2;
			m = number(s, n, &i);
			tm->lo = v - m;
			tm->hi = v + m;
		} else if ( s[i] == 0xB1 ) {		/* ± */
			i++;
			m = number(s, n, &i);
			tm->lo = v - m;
			tm->hi = v + m;
		} else if ( s[i] == '+' ) {
			i++;
			m = number(s, n, &i);
			tm->hi = v + m;
		} else if ( s[i] == '-' ) {
			i++;
			m = number(s, n, &i);
			tm->lo = v - m;
		} else if ( s[i] == '~' ) {
			i++;
			m = number(s, n, &i);
			tm->hi = m;
		} else {
			return -1;
		}
		if ( m < 0 || i != n || tm->hi < tm->lo ) return -1;
		if ( tm->lo < 0 ) tm->lo = 0;
		return 1;
	}
	if ( n == 1 && s[0] == '.' ) {
		tm->kind = TM_EXACT;
		return 1;
	}
	if ( is_digit(s[0]) ) {
		v = number(s, n, &i);
		if ( i != n ) return -1;
		tm->kind = TM_NSTROKE;
		tm->n = v;
		return 1;
	}
	/* a reading: hiragana, or katakana of more than one letter */
	for ( i = 0; i < n && ( is_hira(s[i]) || ( n > 1 && is_kata(s[i]) ) ); i++ ) ;
	if ( i == n ) {
		INT	o = 0;

		tm->kind = TM_READ;
		for ( i = 0; i < n && o + 4 < MAX_READ; i++ ) {
			UINT	c = ( is_kata(s[i]) && s[i] <= 0x30F6 ) ? s[i] - 0x60 : s[i];

			o += cs_utf8_put(c, (char *)tm->read + o);
		}
		tm->nread = o;
		return 1;
	}
	/* elements: 漢字, 漢字×n, 漢字*n, 漢字1-漢字2, one after another */
	for ( i = 0; i < n && k < room; ) {
		TERM	*t = &tm[k];

		if ( k > 0 ) {
			for ( v = 0; v < (INT)sizeof(TERM); v++ ) ( (UB *)t )[v] = 0;
		}
		if ( s[i] < 0x80 ) return -1;
		t->kind = TM_ELEM;
		t->x = s[i++];
		if ( i < n && ( s[i] == 0xD7 || s[i] == '*' ) ) {
			i++;
			t->n = number(s, n, &i);
			if ( t->n < 1 ) return -1;
		} else if ( i < n && s[i] == '-' ) {
			i++;
			if ( i >= n ) return -1;
			t->y = s[i++];
		}
		k++;
	}
	return ( i == n ) ? k : -1;
}

LOCAL INT parse( CONST char *q, TERM *tm )
{
	UINT	s[MAX_TOKEN * 4];
	INT	n = decode(q, s, MAX_TOKEN * 4), i = 0, nt = 0, r;

	while ( i < n ) {
		INT	b;

		while ( i < n && s[i] == ' ' ) i++;
		b = i;
		while ( i < n && s[i] != ' ' ) i++;
		if ( i == b ) break;
		if ( i - b > MAX_TOKEN ) return CS_E_SYNTAX;
		r = term_of(s + b, i - b, tm + nt, MAX_TERM - nt);
		if ( r < 0 ) return CS_E_SYNTAX;
		nt += r;
		if ( nt >= MAX_TERM ) break;
	}
	return ( nt == 0 ) ? CS_E_EMPTY : nt;
}

/* The elements of x's first level (or all of them), less those that y is */
LOCAL INT less( CONST CDIC *d, UINT x, UINT y, UINT *out, INT max )
{
	CONST CDENT	*e = cd_find(d, x);
	CONST UB	*p;
	UINT		a[MAX_ALT];
	INT		na = alts(d, y, a), len, i, k, n = 0, step = 4;
	BOOL		top = TRUE;

	len = cd_field(d, e, CD_T_TOP, &p);
	if ( len <= 0 ) {
		len = cd_field(d, e, CD_T_ELEMS, &p);
		top = FALSE;
	}
	for ( i = 0; i + step <= len && n < max; i += step ) {
		UINT	c = top ? rd32(p + i) : ( rd32(p + i) >> 8 );

		for ( k = 0; k < na && a[k] != c; k++ ) ;
		if ( k == na ) out[n++] = c;
	}
	return n;
}

LOCAL BOOL meets( CONST CDIC *d, CONST CDENT *t, CONST TERM *tm, INT nt )
{
	CONST UB	*el;
	INT		nel = cd_field(d, t, CD_T_ELEMS, &el), i, k;
	BOOL		exact = FALSE;

	if ( nel < 0 ) nel = 0;
	for ( i = 0; i < nt; i++ ) {
		CONST TERM	*m = &tm[i];

		switch ( m->kind ) {
		case TM_READ:
			if ( !has_reading(d, t, m->read, m->nread) ) return FALSE;
			break;
		case TM_RANGE:
			if ( t->strokes < m->lo || t->strokes > m->hi ) return FALSE;
			break;
		case TM_NSTROKE:
			for ( k = 0; k + 4 <= nel; k += 4 ) {
				if ( cd_strokes(d, rd32(el + k) >> 8) == m->n ) break;
			}
			if ( k + 4 > nel ) return FALSE;
			break;
		case TM_ELEM:
			if ( m->y != 0 ) {
				UINT	r[24];
				INT	nr = less(d, m->x, m->y, r, 24);

				if ( nr == 0 ) {
					if ( t->cp != m->x ) return FALSE;
					break;
				}
				for ( k = 0; k < nr; k++ ) {
					if ( !has_elem(d, t, el, nel, r[k], 0) ) return FALSE;
				}
			} else if ( !has_elem(d, t, el, nel, m->x, m->n) ) {
				return FALSE;
			}
			break;
		case TM_EXACT:
			exact = TRUE;
			break;
		default:
			break;
		}
	}
	if ( exact ) {
		CONST UB	*tp;
		INT		ntp = cd_field(d, t, CD_T_TOP, &tp);

		if ( ntp > 0 ) {
			/* its first level: every element one of those given */
			for ( k = 0; k + 4 <= ntp; k += 4 ) {
				if ( !given(d, tm, nt, rd32(tp + k), FALSE) ) return FALSE;
			}
		} else {
			for ( k = 0; k + 4 <= nel; k += 4 ) {
				if ( !given(d, tm, nt, rd32(el + k) >> 8, TRUE) ) return FALSE;
			}
		}
	}
	return TRUE;
}

/* The set a character is shown as */
EXPORT UINT cs_set_of( CONST CDIC *d, UINT cp )
{
	CONST CDENT	*e = cd_find(d, cp);
	UB		m = ( e != NULL ) ? e->sets : 0;

	if ( m & ( CD_M_X0208 | CD_M_X0213_1 ) ) return CS_SET_JIS;
	if ( m & CD_M_X0212 ) return CS_SET_HOJO;
	if ( m & CD_M_X0213_2 ) return CS_SET_JIS;
	if ( m & CD_M_GB ) return CS_SET_GB;
	if ( m & CD_M_KS ) return CS_SET_KS;
	return ( e != NULL ) ? CS_SET_UCS : 0;
}

/* What was found, in the order of the entries' places: entry numbers to code points */
LOCAL void ordered( CONST CDIC *d, UINT *out, INT n )
{
	INT	gap, i, j;

	/* each is the entry's place << 16 | its number, so the numbers sort as the places */
	for ( gap = n / 2; gap > 0; gap /= 2 ) {
		for ( i = gap; i < n; i++ ) {
			UINT	v = out[i];

			for ( j = i; j >= gap && out[j - gap] > v; j -= gap ) out[j] = out[j - gap];
			out[j] = v;
		}
	}
	for ( i = 0; i < n; i++ ) out[i] = d->ent[out[i] & 0xFFFF].cp;
}

EXPORT INT cs_kanji( CONST CDIC *d, CONST char *q, UINT sets, UINT *out, INT max, INT *p_total )
{
	TERM	tm[MAX_TERM];
	INT	nt = parse(q, tm), i, n = 0, total = 0;

	if ( p_total != NULL ) *p_total = 0;
	if ( nt < 0 ) return nt;
	for ( i = 0; i < d->n; i++ ) {
		CONST CDENT	*t = &d->ent[i];

		if ( ( t->flags & CD_F_KANJI ) == 0 ) continue;
		if ( ( cs_set_of(d, t->cp) & sets ) == 0 ) continue;
		if ( !meets(d, t, tm, nt) ) continue;
		total++;
		if ( n < max ) out[n++] = ( (UINT)t->rank << 16 ) | (UINT)i;
	}
	ordered(d, out, n);
	if ( p_total != NULL ) *p_total = total;
	return n;
}

/* ---------------------------------------------------------------- 記号検索 */

/* Whether the words of f (a space or - between) have w */
LOCAL BOOL has_word( CONST UB *f, INT nf, CONST UB *w, INT nw )
{
	INT	i = 0, k, j;

	while ( i < nf ) {
		for ( k = i; k < nf && f[k] != ' ' && f[k] != '-'; k++ ) ;
		if ( k - i == nw ) {
			for ( j = 0; j < nw && f[i + j] == w[j]; j++ ) ;
			if ( j == nw ) return TRUE;
		}
		i = k + 1;
	}
	return FALSE;
}

LOCAL BOOL holds( CONST UB *f, INT nf, CONST UB *w, INT nw )
{
	INT	i, j;

	for ( i = 0; i + nw <= nf; i++ ) {
		for ( j = 0; j < nw && f[i + j] == w[j]; j++ ) ;
		if ( j == nw ) return TRUE;
	}
	return FALSE;
}

/* The words of s, as compared (ASCII in capitals), apart by a 0; how many */
LOCAL INT words( CONST char *s, UB *buf, INT max, INT *p_len )
{
	UINT	c[MAX_TOKEN * 4];
	INT	n = decode(s, c, MAX_TOKEN * 4), i, o = 0, nw = 0;
	BOOL	in = FALSE;

	for ( i = 0; i < n && o + 5 < max; i++ ) {
		if ( c[i] == ' ' ) {
			if ( in ) buf[o++] = 0;
			in = FALSE;
			continue;
		}
		if ( !in ) nw++;
		in = TRUE;
		o += cs_utf8_put(c[i], (char *)buf + o);
	}
	if ( in ) buf[o++] = 0;
	*p_len = o;
	return nw;
}

EXPORT INT cs_symbol( CONST CDIC *d, CONST char *name, CONST char *parts, INT script, INT cls,
		      UINT sets, UINT *out, INT max, INT *p_total )
{
	UB	nw[256], pw[256];
	INT	nn, np, ln, lp, i, n = 0, total = 0;

	if ( p_total != NULL ) *p_total = 0;
	nn = words(( name != NULL ) ? name : "", nw, sizeof(nw), &ln);
	np = words(( parts != NULL ) ? parts : "", pw, sizeof(pw), &lp);
	if ( nn == 0 && np == 0 && script == 0 && cls == 0 ) return CS_E_EMPTY;
	for ( i = 0; i < d->n; i++ ) {
		CONST CDENT	*t = &d->ent[i];
		CONST UB	*f, *g;
		INT		nf, ng, k, at;
		BOOL		ok = TRUE;

		if ( ( t->flags & CD_F_KANJI ) != 0 ) continue;
		if ( script != 0 && t->script != script ) continue;
		if ( cls != 0 && t->cls != cls ) continue;
		if ( ( cs_set_of(d, t->cp) & sets ) == 0 ) continue;
		if ( nn > 0 ) {
			nf = cd_field(d, t, CD_T_NAME, &f);
			ng = cd_field(d, t, CD_T_WORDS, &g);
			for ( k = 0, at = 0; k < nn && ok; k++ ) {
				INT	len = 0;

				while ( nw[at + len] != 0 ) len++;
				ok = (BOOL)( ( nf > 0 && has_word(f, nf, nw + at, len) )
					  || ( ng > 0 && has_word(g, ng, nw + at, len) ) );
				at += len + 1;
			}
		}
		if ( ok && np > 0 ) {
			nf = cd_field(d, t, CD_T_KEYS, &f);
			for ( k = 0, at = 0; k < np && ok; k++ ) {
				INT	len = 0;

				while ( pw[at + len] != 0 ) len++;
				ok = (BOOL)( nf > 0 && holds(f, nf, pw + at, len) );
				at += len + 1;
			}
		}
		if ( !ok ) continue;
		total++;
		if ( n < max ) out[n++] = ( (UINT)t->rank << 16 ) | (UINT)i;
	}
	ordered(d, out, n);
	if ( p_total != NULL ) *p_total = total;
	(void)ln;
	(void)lp;
	return n;
}
