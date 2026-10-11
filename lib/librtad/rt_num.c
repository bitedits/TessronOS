/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	rt_num.c
 *	Numbers and names of 実時間制御TAD (design 15.3)
 *
 *	The record writes its numbers as decimal text -- "0.7", "-0.5",
 *	"1500", "72/60" -- and the engine keeps them in millionths, read
 *	digit by digit, so that no float is needed and nothing is rounded
 *	twice. The shapes of a change over time (easeIn, cubic-bezier ...)
 *	are worked in 65536ths of the way.
 */

#include "rt_local.h"

/* ---------------------------------------------------------------- text */

EXPORT void rt_zero( void *p, SZ n )
{
	UB	*b = (UB *)p;

	while ( n-- > 0 ) *b++ = 0;
}

EXPORT INT rt_slen( CONST UB *s )
{
	INT	n = 0;

	if ( s == NULL ) return 0;
	while ( s[n] != 0 ) n++;
	return n;
}

EXPORT BOOL rt_same( CONST UB *a, CONST char *b )
{
	INT	i;

	if ( a == NULL || b == NULL ) return FALSE;
	for ( i = 0; b[i] != 0; i++ ) {
		if ( a[i] != (UB)b[i] ) return FALSE;
	}
	return (BOOL)( a[i] == 0 );
}

LOCAL UB lower( UB c )
{
	return ( c >= 'A' && c <= 'Z' ) ? (UB)( c - 'A' + 'a' ) : c;
}

EXPORT BOOL rt_same_ci( CONST UB *a, CONST char *b )
{
	INT	i;

	if ( a == NULL || b == NULL ) return FALSE;
	for ( i = 0; b[i] != 0; i++ ) {
		if ( lower(a[i]) != lower((UB)b[i]) ) return FALSE;
	}
	return (BOOL)( a[i] == 0 );
}

EXPORT BOOL rt_eq( CONST UB *a, CONST UB *b )
{
	INT	i;

	if ( a == NULL || b == NULL ) return FALSE;
	for ( i = 0; a[i] != 0; i++ ) {
		if ( a[i] != b[i] ) return FALSE;
	}
	return (BOOL)( b[i] == 0 );
}

EXPORT BOOL rt_is( CONST T_TADNODE *nd, CONST char *name )
{
	return (BOOL)( nd != NULL && nd->kind == TAD_ND_ELEM && rt_same(nd->name, name) );
}

EXPORT CONST UB *rt_attr( CONST T_TADNODE *nd, CONST char *name )
{
	return ( nd != NULL ) ? tad_attr(nd, name) : NULL;
}

/* The first child element of that name */
EXPORT CONST T_TADNODE *rt_child( CONST T_TADNODE *nd, CONST char *name )
{
	CONST T_TADNODE	*c;

	for ( c = ( nd != NULL ) ? nd->first : NULL; c != NULL; c = c->next ) {
		if ( rt_is(c, name) ) return c;
	}
	return NULL;
}

/* The words directly inside an element, the spaces round them left out */
EXPORT CONST UB *rt_text( CONST T_TADNODE *nd )
{
	CONST T_TADNODE	*c;
	CONST UB	*t;

	for ( c = ( nd != NULL ) ? nd->first : NULL; c != NULL; c = c->next ) {
		if ( c->kind == TAD_ND_TEXT && c->text != NULL ) {
			t = c->text;
			while ( *t == ' ' || *t == '\t' || *t == '\r' || *t == '\n' ) t++;
			if ( *t != 0 ) return t;
		}
	}
	return NULL;
}

/* ---------------------------------------------------------------- numbers */

EXPORT BOOL rt_num( CONST UB *s, RT_V *p_v )
{
	D	v = 0, frac = 0, scale = 1000000;
	BOOL	neg = FALSE, any = FALSE;
	INT	i = 0;

	if ( s == NULL ) return FALSE;
	while ( s[i] == ' ' || s[i] == '\t' ) i++;
	if ( s[i] == '-' || s[i] == '+' ) {
		neg = (BOOL)( s[i] == '-' );
		i++;
	}
	while ( s[i] >= '0' && s[i] <= '9' ) {
		if ( v < 1000000000000LL ) v = v * 10 + ( s[i] - '0' );
		i++;
		any = TRUE;
	}
	if ( s[i] == '.' ) {
		i++;
		while ( s[i] >= '0' && s[i] <= '9' ) {
			if ( scale > 1 ) {
				scale /= 10;
				frac += ( s[i] - '0' ) * scale;
			}
			i++;
			any = TRUE;
		}
	}
	if ( !any ) return FALSE;
	v = v * 1000000 + frac;
	*p_v = neg ? -v : v;
	return TRUE;
}

EXPORT RT_V rt_val( CONST UB *s, RT_V dflt )
{
	RT_V	v;

	return rt_num(s, &v) ? v : dflt;
}

EXPORT INT rt_int( CONST UB *s, INT dflt )
{
	RT_V	v;
	INT	i = 0;

	if ( s == NULL ) return dflt;
	while ( s[i] == ' ' ) i++;
	if ( s[i] == '0' && ( s[i + 1] == 'x' || s[i + 1] == 'X' ) ) {
		INT	n = 0, k = i + 2;
		BOOL	any = FALSE;

		for ( ;; k++ ) {
			UB	c = s[k];

			if ( c >= '0' && c <= '9' ) n = n * 16 + ( c - '0' );
			else if ( c >= 'a' && c <= 'f' ) n = n * 16 + ( c - 'a' + 10 );
			else if ( c >= 'A' && c <= 'F' ) n = n * 16 + ( c - 'A' + 10 );
			else break;
			any = TRUE;
		}
		return any ? n : dflt;
	}
	if ( !rt_num(s, &v) ) return dflt;
	return (INT)( v / 1000000 );
}

EXPORT BOOL rt_bool( CONST UB *s, BOOL dflt )
{
	if ( s == NULL ) return dflt;
	if ( rt_same_ci(s, "true") || rt_same(s, "1") || rt_same_ci(s, "yes") ) return TRUE;
	if ( rt_same_ci(s, "false") || rt_same(s, "0") || rt_same_ci(s, "no") ) return FALSE;
	return dflt;
}

/* a * n / d without losing what a / d leaves */
EXPORT D rt_muldiv( D a, D n, D d )
{
	if ( d == 0 ) return 0;
	return ( a / d ) * n + ( a % d ) * n / d;
}

LOCAL D gcd( D a, D b )
{
	if ( a < 0 ) a = -a;
	if ( b < 0 ) b = -b;
	while ( b != 0 ) {
		D	t = a % b;

		a = b;
		b = t;
	}
	return ( a == 0 ) ? 1 : a;
}

EXPORT BOOL rt_ratio( CONST UB *s, D *p_n, D *p_d )
{
	RT_V	a, b;
	INT	i;
	D	g;

	if ( s == NULL ) return FALSE;
	for ( i = 0; s[i] != 0 && s[i] != '/'; i++ ) ;
	if ( !rt_num(s, &a) ) return FALSE;
	b = RT_ONE;
	if ( s[i] == '/' && ( !rt_num(s + i + 1, &b) || b == 0 ) ) return FALSE;
	if ( b < 0 ) {
		a = -a;
		b = -b;
	}
	g = gcd(a, b);
	*p_n = a / g;
	*p_d = b / g;
	return TRUE;
}

/* ---------------------------------------------------------------- easing */

#define Q	65536

EXPORT BOOL rt_ease_parse( CONST UB *s, RT_EASE *e )
{
	e->kind = RT_EASE_LINEAR;
	e->x1 = e->y1 = e->x2 = e->y2 = 0;
	if ( s == NULL || rt_same_ci(s, "linear") || rt_same(s, "0") ) return TRUE;
	if ( rt_same_ci(s, "spline") || rt_same(s, "1") ) { e->kind = RT_EASE_SPLINE; return TRUE; }
	if ( rt_same_ci(s, "easeIn") || rt_same_ci(s, "ease-in") ) { e->kind = RT_EASE_IN; return TRUE; }
	if ( rt_same_ci(s, "easeOut") || rt_same_ci(s, "ease-out") ) { e->kind = RT_EASE_OUT; return TRUE; }
	if ( rt_same_ci(s, "easeInOut") || rt_same_ci(s, "ease-in-out") || rt_same_ci(s, "ease") ) {
		e->kind = RT_EASE_INOUT;
		return TRUE;
	}
	if ( rt_same_ci(s, "step-start") ) { e->kind = RT_EASE_STEP_S; return TRUE; }
	if ( rt_same_ci(s, "step-end") ) { e->kind = RT_EASE_STEP_E; return TRUE; }
	if ( s[0] == 'c' && s[1] == 'u' && s[2] == 'b' && s[3] == 'i' && s[4] == 'c' ) {
		/* cubic-bezier(x1,y1,x2,y2) */
		RT_V	v[4];
		INT	i = 0, k;

		while ( s[i] != 0 && s[i] != '(' ) i++;
		if ( s[i] != '(' ) return FALSE;
		i++;
		for ( k = 0; k < 4; k++ ) {
			while ( s[i] == ' ' ) i++;
			if ( !rt_num(s + i, &v[k]) ) return FALSE;
			while ( s[i] != 0 && s[i] != ',' && s[i] != ')' ) i++;
			if ( s[i] == ',' ) i++;
		}
		e->kind = RT_EASE_BEZIER;
		e->x1 = (INT)( v[0] * Q / RT_ONE );
		e->y1 = (INT)( v[1] * Q / RT_ONE );
		e->x2 = (INT)( v[2] * Q / RT_ONE );
		e->y2 = (INT)( v[3] * Q / RT_ONE );
		if ( e->x1 < 0 ) e->x1 = 0;
		if ( e->x1 > Q ) e->x1 = Q;
		if ( e->x2 < 0 ) e->x2 = 0;
		if ( e->x2 > Q ) e->x2 = Q;
		return TRUE;
	}
	return FALSE;
}

/* One coordinate of a cubic Bezier from 0 to 1 through a and b, at s */
LOCAL D bez( D a, D b, D s )
{
	D	r = Q - s;

	/* 3 r^2 s a + 3 r s^2 b + s^3, all in 65536ths */
	return ( 3 * r * r / Q * s / Q * a + 3 * r * s / Q * s / Q * b ) / Q + s * s / Q * s / Q;
}

EXPORT INT rt_ease( CONST RT_EASE *e, INT p )
{
	D	t;

	if ( p <= 0 ) return ( e != NULL && e->kind == RT_EASE_STEP_S && p == 0 ) ? 0 : 0;
	if ( p >= Q ) return Q;
	t = p;
	switch ( ( e != NULL ) ? e->kind : RT_EASE_LINEAR ) {
	case RT_EASE_IN:
		return (INT)( t * t / Q );
	case RT_EASE_OUT:
		return (INT)( t * ( 2 * Q - t ) / Q );
	case RT_EASE_INOUT:
		if ( t < Q / 2 ) return (INT)( 2 * t * t / Q );
		return (INT)( -Q + ( 4 * Q - 2 * t ) * t / Q );
	case RT_EASE_SPLINE:
		/* the curve through two points with the slopes of their neighbours
		   taken as the line itself: -t^3 + 1.5 t^2 + 0.5 t */
		return (INT)( -( t * t / Q * t / Q ) + 3 * t * t / Q / 2 + t / 2 );
	case RT_EASE_STEP_S:
		return Q;
	case RT_EASE_STEP_E:
		return 0;
	case RT_EASE_BEZIER: {
		D	lo = 0, hi = Q, s = p;
		INT	i;

		for ( i = 0; i < 24; i++ ) {
			D	x;

			s = ( lo + hi ) / 2;
			x = bez(e->x1, e->x2, s);
			if ( x < t ) lo = s;
			else hi = s;
		}
		return (INT)bez(e->y1, e->y2, ( lo + hi ) / 2);
	}
	default:
		return p;
	}
}

/* ---------------------------------------------------------------- notes */

EXPORT INT rt_pitch( CONST UB *s )
{
	static CONST INT	semi[7] = { 9, 11, 0, 2, 4, 5, 7 };	/* A B C D E F G */
	INT			i = 0, n, oct, neg = 0;
	UB			c;

	if ( s == NULL ) return -1;
	while ( s[i] == ' ' ) i++;
	if ( s[i] >= '0' && s[i] <= '9' ) {
		n = rt_int(s + i, -1);
		return ( n >= 0 && n <= 127 ) ? n : -1;
	}
	c = lower(s[i]);
	if ( c < 'a' || c > 'g' ) return -1;
	n = semi[c - 'a'];
	i++;
	for ( ;; ) {
		if ( s[i] == '#' ) { n++; i++; }
		else if ( s[i] == 'b' ) { n--; i++; }
		else if ( s[i] == 0xE2 && s[i + 1] == 0x99 && s[i + 2] == 0xAF ) { n++; i += 3; }	/* ♯ */
		else if ( s[i] == 0xE2 && s[i + 1] == 0x99 && s[i + 2] == 0xAD ) { n--; i += 3; }	/* ♭ */
		else break;
	}
	if ( s[i] == '-' ) {
		neg = 1;
		i++;
	}
	if ( s[i] < '0' || s[i] > '9' ) return -1;
	oct = 0;
	while ( s[i] >= '0' && s[i] <= '9' ) oct = oct * 10 + ( s[i++] - '0' );
	if ( neg ) oct = -oct;
	n += ( oct + 1 ) * 12;
	return ( n >= 0 && n <= 127 ) ? n : -1;
}

/* ---------------------------------------------------------------- Base64 */

LOCAL INT b64( UB c )
{
	if ( c >= 'A' && c <= 'Z' ) return c - 'A';
	if ( c >= 'a' && c <= 'z' ) return c - 'a' + 26;
	if ( c >= '0' && c <= '9' ) return c - '0' + 52;
	if ( c == '+' || c == '-' ) return 62;
	if ( c == '/' || c == '_' ) return 63;
	return -1;
}

EXPORT INT rt_base64( CONST UB *s, UB *out, INT max )
{
	UW	acc = 0;
	INT	bits = 0, n = 0, v;

	if ( s == NULL ) return -1;
	for ( ; *s != 0 && *s != '<'; s++ ) {
		if ( *s == '=' ) break;
		v = b64(*s);
		if ( v < 0 ) {
			if ( *s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' ) continue;
			return -1;
		}
		acc = ( acc << 6 ) | (UW)v;
		bits += 6;
		if ( bits >= 8 ) {
			bits -= 8;
			if ( out != NULL ) {
				if ( n >= max ) return -1;
				out[n] = (UB)( acc >> bits );
			}
			n++;
		}
	}
	return n;
}

/* ---------------------------------------------------------------- operators */

LOCAL CONST struct {
	CONST char	*name;
	UINT		code;
} op_tab[] = {
	{ "add", 0x8000 }, { "sub", 0x8001 }, { "mul", 0x8002 }, { "div", 0x8003 }, { "mod", 0x8004 },
	{ "lt", 0x8010 }, { "gt", 0x8011 }, { "le", 0x8012 }, { "ge", 0x8013 }, { "eq", 0x8014 },
	{ "ne", 0x8015 },
	{ "and", 0x8020 }, { "or", 0x8021 }, { "not", 0x8030 }, { "neg", 0x8031 },
	{ "lt0", 0x8040 }, { "gt0", 0x8041 }, { "le0", 0x8042 }, { "ge0", 0x8043 }, { "eq0", 0x8044 },
	{ "ne0", 0x8045 },
	{ "dup", 0x8050 }, { "swap", 0x8051 }, { "pick", 0x8052 }
};

#define NOPS	( (INT)( sizeof(op_tab) / sizeof(op_tab[0]) ) )

EXPORT UINT rt_op_code( CONST UB *name )
{
	INT	i;

	for ( i = 0; i < NOPS; i++ ) {
		if ( rt_same(name, op_tab[i].name) ) return op_tab[i].code;
	}
	return 0;
}

EXPORT CONST char *rt_op_name( UINT code )
{
	INT	i;

	for ( i = 0; i < NOPS; i++ ) {
		if ( op_tab[i].code == code ) return op_tab[i].name;
	}
	return NULL;
}

/* ---------------------------------------------------------------- chance */

EXPORT UINT rt_rand( RT *rt )
{
	UINT	x = rt->seed;

	if ( x == 0 ) x = 0x2545F491U;
	x ^= x << 13;
	x ^= x >> 17;
	x ^= x << 5;
	rt->seed = x;
	return x;
}

/* 0 .. n-1, from the high bits, which change most */
EXPORT INT rt_rand_n( RT *rt, INT n )
{
	if ( n <= 1 ) return 0;
	return (INT)( ( (UD)rt_rand(rt) * (UD)n ) >> 32 );
}

EXPORT void rt_seed( RT *rt, UINT seed )
{
	rt->seed = ( seed != 0 ) ? seed : 0x2545F491U;
}
