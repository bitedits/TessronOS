/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cs_code.c
 *	文字検索: the character sets and the codes (design 11.19)
 *
 *	The sets are TRON code's first plane as lib/libbpk lays it out:
 *	in zone A JIS X 0208 with JIS X 0213's first plane in its gaps, in
 *	zone B JIS X 0213's second plane (first byte 87..A0, its rows 1,
 *	3-5, 8, 12-15 and 78-94 in turn) and JIS X 0212 (A1..ED, rows 1..77),
 *	in zone C GB 2312 and in zone D KS X 1001 (from B7), both laid 126
 *	to a first byte. Planes 16 and 17 are the Unicode BMP.
 *
 *	A character's TRON code is libbpk's, except that a character that
 *	only GB 2312 or KS X 1001 has is given its place there rather than
 *	in plane 16 or 17. Shift_JIS is JIS X 0213's (Shift_JIS-2004) and
 *	EUC the EUC of each set.
 */

#include "cs_dic.h"
#include <ts/bpk.h>

#define PLANE_BMP	16
#define PLANE_SPAN	44032
#define PACK		( 94 * 94 )

LOCAL CONST UB p2_rows[] = { 1, 3, 4, 5, 8, 12, 13, 14, 15, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87,
			     88, 89, 90, 91, 92, 93, 94 };
#define NP2	( (INT)sizeof(p2_rows) )

/* The place of a row of JIS X 0213's second plane among zone B's first bytes, -1 none */
LOCAL INT p2_index( INT row )
{
	INT	i;

	for ( i = 0; i < NP2; i++ ) {
		if ( p2_rows[i] == row ) return i;
	}
	return -1;
}

/* What a table's entry holds: how many characters, into out */
LOCAL INT entry( UINT v, UINT out[2] )
{
	if ( v == 0 ) return 0;
	if ( v >= 0xD800 && v <= 0xDFFF ) {
		INT	n = (INT)( v - 0xD800 );

		if ( n >= bpk_tron_nmulti ) return 0;
		out[0] = bpk_tron_multi[n][0];
		out[1] = bpk_tron_multi[n][1];
		return ( out[1] != 0 ) ? 2 : 1;
	}
	out[0] = v;
	return 1;
}

LOCAL BOOL jis208_has( CONST CDIC *d, INT row, INT cell )
{
	INT	i = ( row - 1 ) * 94 + cell - 1;

	if ( d == NULL || d->jis208 == NULL ) return (BOOL)( row <= 84 );
	return (BOOL)( ( d->jis208[i >> 3] & ( 1 << ( i & 7 ) ) ) != 0 );
}

EXPORT INT cs_row_cells( INT set, INT plane, INT row )
{
	(void)plane;
	(void)row;
	return ( set == CS_UCS ) ? 256 : ( set == CS_TRON ) ? 94 + 126 : 94;
}

EXPORT INT cs_rows( INT set, INT plane, INT *p_first )
{
	*p_first = 1;
	switch ( set ) {
	case CS_JIS:	return 94;
	case CS_HOJO:	return 77;
	case CS_GB:
	case CS_KS:	return 94;
	case CS_UCS:
		*p_first = 0;
		return 256;
	case CS_TRON:
		*p_first = 0x21;
		return ( plane == 1 ) ? 0xFD - 0x21 + 1 : 0xFD - 0x21 + 1;
	default:
		return 0;
	}
}

/* The TRON code of the place along a row: the first 94 are 21..7E, the rest 80..FD */
LOCAL UINT tron_lo( INT cell )
{
	return ( cell < 94 ) ? (UINT)( 0x21 + cell ) : (UINT)( 0x80 + cell - 94 );
}

EXPORT INT cs_cell( CONST CDIC *d, INT set, INT plane, INT row, INT cell, UINT out[2], INT *p_kind )
{
	INT	n = 0, kind = CS_K_NONE, i;

	out[0] = out[1] = 0;
	switch ( set ) {
	case CS_JIS:
		if ( row < 1 || row > 94 || cell < 1 || cell > 94 ) break;
		if ( plane == 2 ) {
			i = p2_index(row);
			if ( i < 0 ) break;
			n = entry(bpk_tron_zb[i * 94 + cell - 1], out);
			kind = CS_K_JIS4;
		} else {
			n = entry(bpk_jis_char[( row - 1 ) * 94 + cell - 1], out);
			kind = jis208_has(d, row, cell) ? CS_K_JIS12 : CS_K_JIS3;
		}
		break;
	case CS_HOJO:
		if ( row < 1 || row > 77 || cell < 1 || cell > 94 ) break;
		n = entry(bpk_tron_zb[( 0xA0 + row - 0x87 ) * 94 + cell - 1], out);
		kind = CS_K_HOJO;
		break;
	case CS_GB:
	case CS_KS:
		if ( row < 1 || row > 94 || cell < 1 || cell > 94 ) break;
		n = entry(( set == CS_GB ) ? bpk_tron_zc[( row - 1 ) * 94 + cell - 1]
					   : bpk_tron_zd[( row - 1 ) * 94 + cell - 1], out);
		kind = ( set == CS_GB ) ? CS_K_GB : CS_K_KS;
		break;
	case CS_UCS: {
		UINT		cp = (UINT)( row * 256 + cell );
		CONST CDENT	*e;

		if ( row < 0 || row > 255 || cell < 0 || cell > 255 ) break;
		e = ( d != NULL ) ? cd_find(d, cp) : NULL;
		if ( e != NULL && cs_set_of(d, cp) == CS_SET_UCS ) {
			out[0] = cp;
			n = 1;
			kind = CS_K_UCS;
		}
		break;
	}
	case CS_TRON: {
		INT	pl = plane;
		UINT	u[2];
		INT	r;

		if ( row < 0x21 || row > 0xFD || ( row > 0x7E && row < 0x80 ) || cell < 0 || cell >= 94 + 126 ) break;
		r = bpk_tron_chars(( (UINT)row << 8 ) | tron_lo(cell), &pl, u);
		if ( r == BPK_TRON_NOUCS ) {
			kind = CS_K_OTHER;
		} else if ( r > 0 && u[0] >= 0x20 ) {
			out[0] = u[0];
			out[1] = ( r == 2 ) ? u[1] : 0;
			n = r;
			kind = cs_kind_of(d, u[0]);
		}
		break;
	}
	default:
		break;
	}
	if ( p_kind != NULL ) *p_kind = kind;
	return n;
}

EXPORT INT cs_kind_of( CONST CDIC *d, UINT cp )
{
	CONST CDENT	*e = ( d != NULL ) ? cd_find(d, cp) : NULL;
	UB		m = ( e != NULL ) ? e->sets : 0;

	if ( m & CD_M_X0208 ) return CS_K_JIS12;
	if ( m & CD_M_X0213_1 ) return CS_K_JIS3;
	if ( m & CD_M_X0212 ) return CS_K_HOJO;
	if ( m & CD_M_X0213_2 ) return CS_K_JIS4;
	if ( m & CD_M_GB ) return CS_K_GB;
	if ( m & CD_M_KS ) return CS_K_KS;
	return ( e != NULL || cp < 0x10000 ) ? CS_K_UCS : CS_K_OTHER;
}

/* The place in a packed table (zone C or D) of a character, -1 none */
LOCAL INT packed_find( const UH *t, UINT cp )
{
	INT	i;

	for ( i = 0; i < PACK; i++ ) {
		if ( t[i] == cp ) return i;
	}
	return -1;
}

EXPORT ER cs_tron( UINT cp, INT *p_plane, UINT *p_code )
{
	UH	out[4];
	INT	plane = 1, n = bpk_ucs_tron(cp, out, 4, &plane), i;

	if ( n <= 0 ) return E_PAR;
	/* the national sets before the BMP's planes */
	if ( plane >= PLANE_BMP && cp >= 0x80 ) {
		INT	at = packed_find(bpk_tron_zc, cp);

		if ( at >= 0 ) {
			*p_plane = 1;
			*p_code = ( (UINT)( 0x21 + at / 126 ) << 8 ) | (UINT)( 0x80 + at % 126 );
			return E_OK;
		}
		at = packed_find(bpk_tron_zd, cp);
		if ( at >= 0 ) {
			*p_plane = 1;
			*p_code = ( (UINT)( 0xB7 + at / 126 ) << 8 ) | (UINT)( 0x80 + at % 126 );
			return E_OK;
		}
	}
	for ( i = 0; i < n && out[i] >= 0xFE00; i++ ) ;
	if ( i >= n ) return E_PAR;
	*p_plane = plane;
	*p_code = out[i];
	return E_OK;
}

EXPORT UINT cs_from_tron( INT plane, UINT code )
{
	INT	pl = plane;
	UINT	u[2];
	INT	r = bpk_tron_chars(code, &pl, u);

	return ( r > 0 && u[0] >= 0x20 ) ? u[0] : 0;
}

LOCAL CONST char hex[] = "0123456789ABCDEF";

LOCAL INT put_hex( char *s, UINT v, INT digits )
{
	INT	i;

	for ( i = digits - 1; i >= 0; i-- ) {
		s[i] = hex[v & 0xF];
		v >>= 4;
	}
	return digits;
}

LOCAL INT put_dec( char *s, INT v, INT digits )
{
	char	t[12];
	INT	n = 0, i;

	if ( v < 0 ) v = 0;
	do {
		t[n++] = (char)( '0' + v % 10 );
		v /= 10;
	} while ( v > 0 && n < 11 );
	while ( n < digits ) t[n++] = '0';
	for ( i = 0; i < n; i++ ) s[i] = t[n - 1 - i];
	return n;
}

EXPORT void cs_tron_text( UINT cp, char *s )
{
	INT	plane = 1, k = 0;
	UINT	code = 0;

	s[0] = 0;
	if ( cs_tron(cp, &plane, &code) < E_OK ) return;
	s[k++] = '&';
	s[k++] = 'T';
	k += put_hex(s + k, ( 0xFE21 + (UINT)plane - 1 ) & 0xFF, 2);
	k += put_hex(s + k, code, 4);
	s[k++] = ';';
	s[k] = 0;
}

/* A TRON code as the place of a set */
LOCAL BOOL place_of_tron( INT plane, UINT code, CSPLACE *p )
{
	UINT	hi = code >> 8, lo = code & 0xFF;

	p->tplane = plane;
	p->tcode = code;
	if ( plane >= PLANE_BMP ) {
		p->set = CS_UCS;
		p->kind = CS_K_UCS;
		p->plane = 0;
		p->row = 0;
		p->cell = 0;
		return TRUE;
	}
	if ( plane != 1 ) return FALSE;
	if ( hi >= 0x21 && hi <= 0x7E && lo >= 0x21 && lo <= 0x7E ) {
		p->set = CS_JIS;
		p->plane = 1;
		p->row = (INT)hi - 0x20;
		p->cell = (INT)lo - 0x20;
		return TRUE;
	}
	if ( hi >= 0x87 && hi <= 0xA0 && lo >= 0x21 && lo <= 0x7E ) {
		p->set = CS_JIS;
		p->kind = CS_K_JIS4;
		p->plane = 2;
		p->row = p2_rows[hi - 0x87];
		p->cell = (INT)lo - 0x20;
		return TRUE;
	}
	if ( hi >= 0xA1 && hi <= 0xED && lo >= 0x21 && lo <= 0x7E ) {
		p->set = CS_HOJO;
		p->kind = CS_K_HOJO;
		p->plane = 0;
		p->row = (INT)hi - 0xA0;
		p->cell = (INT)lo - 0x20;
		return TRUE;
	}
	if ( lo >= 0x80 && lo <= 0xFD ) {
		INT	at;

		if ( hi >= 0x21 && hi <= 0x7E ) {
			at = (INT)( hi - 0x21 ) * 126 + (INT)( lo - 0x80 );
			p->set = CS_GB;
			p->kind = CS_K_GB;
		} else if ( hi >= 0xB7 && hi <= 0xFD ) {
			at = (INT)( hi - 0xB7 ) * 126 + (INT)( lo - 0x80 );
			p->set = CS_KS;
			p->kind = CS_K_KS;
		} else {
			return FALSE;
		}
		if ( at >= PACK ) return FALSE;
		p->plane = 0;
		p->row = at / 94 + 1;
		p->cell = at % 94 + 1;
		return TRUE;
	}
	return FALSE;
}

/* The same set and place already listed */
LOCAL BOOL listed( CONST CSPLACE *out, INT n, INT set, INT plane, INT row, INT cell )
{
	INT	i;

	for ( i = 0; i < n; i++ ) {
		if ( out[i].set == set && out[i].plane == plane && out[i].row == row && out[i].cell == cell ) {
			return TRUE;
		}
	}
	return FALSE;
}

LOCAL void add( CSPLACE *out, INT *p_n, INT max, INT set, INT kind, INT plane, INT row, INT cell )
{
	CSPLACE	*p;

	if ( *p_n >= max || listed(out, *p_n, set, plane, row, cell) ) return;
	p = &out[( *p_n )++];
	p->set = set;
	p->kind = kind;
	p->plane = plane;
	p->row = row;
	p->cell = cell;
	p->tplane = 0;
	p->tcode = 0;
}

EXPORT INT cs_places( CONST CDIC *d, UINT cp, CSPLACE *out, INT max )
{
	INT	n = 0, plane, i;
	UINT	code;

	if ( max <= 0 ) return 0;
	if ( cs_tron(cp, &plane, &code) >= E_OK ) {
		CSPLACE	p;

		p.kind = CS_K_OTHER;
		if ( place_of_tron(plane, code, &p) ) {
			if ( p.set == CS_JIS && p.plane == 1 ) {
				p.kind = jis208_has(d, p.row, p.cell) ? CS_K_JIS12 : CS_K_JIS3;
			}
			if ( p.set == CS_UCS ) p.cell = (INT)cp;
			if ( cp >= 0x20 && cp < 0x7F ) {
				p.set = CS_UCS;		/* ASCII: plane 1's row 3 */
				p.kind = CS_K_UCS;
				p.plane = p.row = 0;
				p.cell = (INT)cp;
			}
			out[n++] = p;
		}
	}
	/* the other sets that have it */
	for ( i = 0; i < PACK && n < max; i++ ) {
		UINT	u[2];

		if ( entry(bpk_jis_char[i], u) == 1 && u[0] == cp ) {
			add(out, &n, max, CS_JIS, jis208_has(d, i / 94 + 1, i % 94 + 1) ? CS_K_JIS12 : CS_K_JIS3,
			    1, i / 94 + 1, i % 94 + 1);
		}
		if ( bpk_tron_zc[i] == cp ) add(out, &n, max, CS_GB, CS_K_GB, 0, i / 94 + 1, i % 94 + 1);
		if ( bpk_tron_zd[i] == cp ) add(out, &n, max, CS_KS, CS_K_KS, 0, i / 94 + 1, i % 94 + 1);
	}
	for ( i = 0; i < ( 0xED - 0x87 + 1 ) * 94 && n < max; i++ ) {
		UINT	u[2];
		INT	hi = 0x87 + i / 94;

		if ( entry(bpk_tron_zb[i], u) != 1 || u[0] != cp ) continue;
		if ( hi <= 0xA0 ) {
			add(out, &n, max, CS_JIS, CS_K_JIS4, 2, p2_rows[hi - 0x87], i % 94 + 1);
		} else {
			add(out, &n, max, CS_HOJO, CS_K_HOJO, 0, hi - 0xA0, i % 94 + 1);
		}
	}
	/* the TRON code of each, as the first has it */
	for ( i = 0; i < n; i++ ) {
		CSPLACE	*p = &out[i];

		if ( i == 0 && p->tplane != 0 ) continue;
		switch ( p->set ) {
		case CS_JIS:
			if ( p->plane == 1 ) {
				p->tplane = 1;
				p->tcode = ( (UINT)( p->row + 0x20 ) << 8 ) | (UINT)( p->cell + 0x20 );
			} else {
				p->tplane = 1;
				p->tcode = ( (UINT)( 0x87 + p2_index(p->row) ) << 8 ) | (UINT)( p->cell + 0x20 );
			}
			break;
		case CS_HOJO:
			p->tplane = 1;
			p->tcode = ( (UINT)( 0xA0 + p->row ) << 8 ) | (UINT)( p->cell + 0x20 );
			break;
		case CS_GB:
		case CS_KS: {
			INT	at = ( p->row - 1 ) * 94 + p->cell - 1;

			p->tplane = 1;
			p->tcode = ( (UINT)( ( p->set == CS_GB ? 0x21 : 0xB7 ) + at / 126 ) << 8 )
				 | (UINT)( 0x80 + at % 126 );
			break;
		}
		default:
			break;
		}
	}
	return n;
}

EXPORT void cs_place_text( CONST CSPLACE *p, char *s )
{
	INT	k = 0;

	switch ( p->set ) {
	case CS_JIS:
		k += put_dec(s + k, p->plane, 1);
		s[k++] = '-';
		/* fall through */
	case CS_HOJO:
	case CS_GB:
	case CS_KS:
		k += put_dec(s + k, p->row, 2);
		s[k++] = '-';
		k += put_dec(s + k, p->cell, 2);
		break;
	case CS_UCS:
		s[k++] = 'U';
		s[k++] = '+';
		k += put_hex(s + k, (UINT)p->cell, ( p->cell > 0xFFFF ) ? 5 : 4);
		break;
	case CS_TRON:
		k += put_dec(s + k, p->tplane, 1);
		s[k++] = '-';
		k += put_hex(s + k, p->tcode, 4);
		break;
	default:
		break;
	}
	s[k] = 0;
}

EXPORT UINT cs_sjis( CONST CSPLACE *p )
{
	INT	row = p->row, cell = p->cell;
	UINT	s1, s2;

	if ( p->set != CS_JIS || row < 1 || row > 94 || cell < 1 || cell > 94 ) return 0;
	if ( p->plane == 1 ) {
		s1 = ( row <= 62 ) ? (UINT)( ( row + 0x101 ) / 2 ) : (UINT)( ( row + 0x181 ) / 2 );
	} else if ( row >= 78 ) {
		s1 = (UINT)( ( row + 0x19B ) / 2 );
	} else if ( p2_index(row) >= 0 ) {
		s1 = (UINT)( ( row + 0x1DF ) / 2 - ( row / 8 ) * 3 );
	} else {
		return 0;
	}
	if ( row & 1 ) {
		s2 = (UINT)( cell + 0x3F + ( ( cell >= 64 ) ? 1 : 0 ) );
	} else {
		s2 = (UINT)( cell + 0x9E );
	}
	return ( s1 << 8 ) | s2;
}

EXPORT UINT cs_euc( CONST CSPLACE *p )
{
	UINT	v = ( (UINT)( p->row + 0xA0 ) << 8 ) | (UINT)( p->cell + 0xA0 );

	switch ( p->set ) {
	case CS_JIS:
		return ( p->plane == 2 ) ? ( 0x8F0000U | v ) : v;
	case CS_HOJO:
		return 0x8F0000U | v;
	case CS_GB:
	case CS_KS:
		return v;
	default:
		return 0;
	}
}

/* ---------------------------------------------------------------- reading a code */

LOCAL INT hexval( UINT c )
{
	if ( c >= '0' && c <= '9' ) return (INT)( c - '0' );
	if ( c >= 'A' && c <= 'F' ) return (INT)( c - 'A' + 10 );
	if ( c >= 'a' && c <= 'f' ) return (INT)( c - 'a' + 10 );
	if ( c >= 0xFF10 && c <= 0xFF19 ) return (INT)( c - 0xFF10 );
	if ( c >= 0xFF21 && c <= 0xFF26 ) return (INT)( c - 0xFF21 + 10 );
	if ( c >= 0xFF41 && c <= 0xFF46 ) return (INT)( c - 0xFF41 + 10 );
	return -1;
}

/* The numbers of s, decimal or hexadecimal, at most 3; how many */
LOCAL INT numbers( CONST char *s, BOOL hexa, INT *v )
{
	CONST UB	*p = (CONST UB *)s;
	INT		n = 0, k, d;
	BOOL		in = FALSE;
	UINT		c;

	while ( ( k = cs_utf8_next(p, &c) ) > 0 ) {
		p += k;
		d = hexval(c);
		if ( d >= 0 && ( hexa || d < 10 ) ) {
			if ( !in ) {
				if ( n == 3 ) return -1;
				v[n++] = 0;
			}
			in = TRUE;
			v[n - 1] = v[n - 1] * ( hexa ? 16 : 10 ) + d;
			if ( v[n - 1] > 0xFFFFFF ) return -1;
		} else if ( c == ' ' || c == '-' || c == ',' || c == 0x3000 || c == 0xFF0D || c == 0x30FC || c == 0x2212 ) {
			in = FALSE;
		} else {
			return -1;
		}
	}
	return n;
}

EXPORT BOOL cs_parse_code( INT set, CONST char *s, INT *p_plane, INT *p_row, INT *p_cell )
{
	INT	v[3], n;

	while ( *s == ' ' ) s++;
	if ( set == CS_UCS ) {
		if ( ( s[0] == 'U' || s[0] == 'u' ) && s[1] == '+' ) s += 2;
		n = numbers(s, TRUE, v);
		if ( n != 1 || v[0] > 0xFFFF ) return FALSE;
		*p_plane = 0;
		*p_row = v[0] >> 8;
		*p_cell = v[0] & 0xFF;
		return TRUE;
	}
	if ( set == CS_TRON ) {
		UINT	code;

		if ( s[0] == '&' && ( s[1] == 'T' || s[1] == 't' ) ) {
			char	t[16];
			INT	i;

			for ( i = 0; i < 6 && s[2 + i] != 0 && s[2 + i] != ';'; i++ ) t[i] = s[2 + i];
			t[i] = 0;
			if ( i != 6 || numbers(t, TRUE, v) != 1 ) return FALSE;
			*p_plane = (INT)( ( ( v[0] >> 16 ) & 0xFF ) - 0x21 + 1 );
			code = (UINT)( v[0] & 0xFFFF );
		} else {
			/* the plane in decimal, then the code in hexadecimal */
			char	t[8];
			INT	i;

			for ( i = 0; i < 7 && s[i] >= '0' && s[i] <= '9'; i++ ) t[i] = s[i];
			t[i] = 0;
			if ( i == 0 || s[i] == 0 || numbers(t, FALSE, v) != 1 ) return FALSE;
			*p_plane = v[0];
			n = numbers(s + i, TRUE, v);
			if ( n != 1 ) return FALSE;
			code = (UINT)v[0];
		}
		if ( *p_plane < 1 || *p_plane > 0xDE || ( code >> 8 ) < 0x21 || ( code >> 8 ) > 0xFD ) return FALSE;
		*p_row = (INT)( code >> 8 );
		code &= 0xFF;
		if ( code >= 0x21 && code <= 0x7E ) *p_cell = (INT)code - 0x21;
		else if ( code >= 0x80 && code <= 0xFD ) *p_cell = (INT)code - 0x80 + 94;
		else return FALSE;
		return TRUE;
	}
	n = numbers(s, FALSE, v);
	if ( n <= 0 ) return FALSE;
	*p_plane = 1;
	*p_cell = 1;
	if ( set == CS_JIS && n == 3 ) {
		*p_plane = v[0];
		*p_row = v[1];
		*p_cell = v[2];
	} else if ( n == 2 ) {
		*p_row = v[0];
		*p_cell = v[1];
	} else if ( n == 1 ) {
		*p_row = v[0];
	} else {
		return FALSE;
	}
	if ( set != CS_JIS ) *p_plane = 0;
	if ( *p_row < 1 || *p_row > ( set == CS_HOJO ? 77 : 94 ) || *p_cell < 1 || *p_cell > 94 ) return FALSE;
	if ( set == CS_JIS && ( *p_plane < 1 || *p_plane > 2 ) ) return FALSE;
	return TRUE;
}
