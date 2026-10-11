/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cu_eng.c
 *	電卓: what each key does (design 11.17)
 *
 *	The operations waiting are two stacks, as an expression is read
 *	with ranks: the left operands (val) and the operations with the
 *	opening brackets among them (op). Between two keys there are as
 *	many operands as operations other than brackets, the operand of
 *	each just before it, so that the waiting expression can be written
 *	out in order. An operation pressed pushes the number in hand and
 *	first does those waiting of at least its rank (a power, which
 *	groups to the right, only those above it).
 */

#include "cu_eng.h"

/* ---------------------------------------------------------------- small things */

LOCAL void zero( void *p, INT n )
{
	UB	*b = (UB *)p;

	while ( n-- > 0 ) *b++ = 0;
}

LOCAL INT rank( CUENG *g, INT key )
{
	if ( !g->sci ) return 1;
	switch ( key ) {
	case CU_K_ADD:
	case CU_K_SUB:	return 1;
	case CU_K_MUL:
	case CU_K_DIV:	return 2;
	default:	return 3;
	}
}

LOCAL void fail( CUENG *g, INT er )
{
	g->err = er;
	g->nval = 0;
	g->nop = 0;
	g->typing = FALSE;
	g->want = FALSE;
	g->lastop = 0;
	cu_zero(&g->x);
}

/* The number typed, as a number */
LOCAL void typed( CUENG *g, CUDEC *v )
{
	char	s[CU_ENTRY + 2];
	INT	i, n = 0;

	if ( g->eneg ) s[n++] = '-';
	for ( i = 0; i < g->nent; i++ ) s[n++] = g->ent[i];
	if ( g->nent == 0 ) s[n++] = '0';
	if ( cu_parse(v, s, n, NULL) < CU_OK ) cu_zero(v);
}

EXPORT void cu_value( CUENG *g, CUDEC *v )
{
	if ( g->typing ) {
		typed(g, v);
	} else {
		*v = g->x;
	}
}

/* The number in hand taken: what was typed becomes the number shown */
LOCAL void take( CUENG *g, CUDEC *v )
{
	cu_value(g, v);
	g->x = *v;
	g->typing = FALSE;
}

LOCAL void hundred( CUDEC *h )
{
	cu_int(h, 100);
}

/* ---------------------------------------------------------------- the operations */

LOCAL INT power( CUENG *g, CUDEC *r, CONST CUDEC *a, CONST CUDEC *b )
{
	D	n;

	if ( cu_to_int(b, &n) >= CU_OK && n >= -99999 && n <= 99999 ) {
		return cu_ipow(r, a, n);
	}
	if ( cu_is_zero(a) ) {
		if ( b->neg ) return CU_EDIV0;
		cu_zero(r);
		return CU_OK;
	}
	if ( a->neg ) {
		return CU_EDOM;			/* a negative number to a fraction */
	}
	if ( g->fn == NULL ) {
		return CU_EDOM;
	}
	*r = *a;
	return g->fn(CU_K_POW, r, b, g->deg);
}

LOCAL INT apply( CUENG *g, INT op, CUDEC *r, CONST CUDEC *a, CONST CUDEC *b )
{
	switch ( op ) {
	case CU_K_ADD:	return cu_add(r, a, b);
	case CU_K_SUB:	return cu_sub(r, a, b);
	case CU_K_MUL:	return cu_mul(r, a, b);
	case CU_K_DIV:	return cu_div(r, a, b);
	case CU_K_POW:	return power(g, r, a, b);
	default:	return CU_ESYN;
	}
}

/* The last operation waiting done: its two operands become its result */
LOCAL INT reduce( CUENG *g )
{
	INT	op = g->op[--g->nop];
	CUDEC	*a = &g->val[g->nval - 2], *b = &g->val[g->nval - 1], r;
	INT	er = apply(g, op, &r, a, b);

	if ( er < CU_OK ) return er;
	g->lastop = op;
	g->lastv = ( op == CU_K_MUL ) ? *a : *b;
	g->nval--;
	g->val[g->nval - 1] = r;
	return CU_OK;
}

LOCAL void operation( CUENG *g, INT key )
{
	CUDEC	v;
	INT	er;

	/* pressed again with no number between: the other one is meant */
	if ( g->want && g->nop > 0 && g->op[g->nop - 1] != CU_K_LP ) {
		g->nop--;
		g->x = g->val[--g->nval];
		g->typing = FALSE;
	}
	if ( g->nval >= CU_DEPTH - 1 || g->nop >= CU_DEPTH - 1 ) {
		return;
	}
	take(g, &v);
	g->val[g->nval++] = v;
	while ( g->nop > 0 && g->op[g->nop - 1] != CU_K_LP ) {
		INT	top = rank(g, g->op[g->nop - 1]), now = rank(g, key);

		if ( top < now || ( top == now && key == CU_K_POW && g->sci ) ) break;
		er = reduce(g);
		if ( er < CU_OK ) {
			fail(g, er);
			return;
		}
	}
	g->op[g->nop++] = key;
	g->x = g->val[g->nval - 1];
	g->want = TRUE;
}

LOCAL void equals( CUENG *g )
{
	CUDEC	v, r;
	INT	er;

	take(g, &v);
	if ( g->nop == 0 ) {
		if ( g->lastop != 0 ) {
			er = ( g->lastop == CU_K_MUL ) ? apply(g, CU_K_MUL, &r, &g->lastv, &v)
						 : apply(g, g->lastop, &r, &v, &g->lastv);
			if ( er < CU_OK ) {
				fail(g, er);
				return;
			}
			g->x = r;
		}
		g->want = FALSE;
		return;
	}
	g->val[g->nval++] = v;
	while ( g->nop > 0 ) {
		if ( g->op[g->nop - 1] == CU_K_LP ) {
			g->nop--;
			continue;
		}
		er = reduce(g);
		if ( er < CU_OK ) {
			fail(g, er);
			return;
		}
	}
	g->x = g->val[0];
	g->nval = 0;
	g->want = FALSE;
}

LOCAL void open_bracket( CUENG *g )
{
	if ( g->typing ) {
		operation(g, CU_K_MUL);		/* 2(3 + 4) is 2 × (3 + 4) */
		if ( g->err != CU_OK ) return;
	}
	if ( g->nop >= CU_DEPTH - 1 ) return;
	g->op[g->nop++] = CU_K_LP;
	g->want = TRUE;
}

LOCAL void close_bracket( CUENG *g )
{
	CUDEC	v;
	INT	i, er;

	for ( i = g->nop - 1; i >= 0 && g->op[i] != CU_K_LP; i-- ) ;
	if ( i < 0 || g->nval >= CU_DEPTH - 1 ) {
		return;
	}
	take(g, &v);
	g->val[g->nval++] = v;
	while ( g->op[g->nop - 1] != CU_K_LP ) {
		er = reduce(g);
		if ( er < CU_OK ) {
			fail(g, er);
			return;
		}
	}
	g->nop--;
	g->x = g->val[--g->nval];
	g->want = FALSE;
}

/* ---------------------------------------------------------------- functions of one number */

/* The sine and cosine of 0, 90, 180 and 270 degrees */
LOCAL CONST INT	right_sin[4] = { 0, 1, 0, -1 };
LOCAL CONST INT	right_cos[4] = { 1, 0, -1, 0 };

/* An angle in degrees put between 0 and 360; on a right angle the answer is exact */
LOCAL INT trig( CUENG *g, INT key, CUDEC *r, CONST CUDEC *v )
{
	CUDEC	t = *v, q, f, full, right;
	INT	er;

	if ( g->deg ) {
		cu_int(&full, 360);
		cu_int(&right, 90);
		if ( ( er = cu_div(&q, v, &full) ) < CU_OK
		  || ( er = cu_floor(&f, &q) ) < CU_OK
		  || ( er = cu_mul(&f, &f, &full) ) < CU_OK
		  || ( er = cu_sub(&t, v, &f) ) < CU_OK ) {
			return er;
		}
		if ( cu_div(&q, &t, &right) >= CU_OK && cu_is_int(&q) ) {
			D	k = 0;

			(void)cu_to_int(&q, &k);
			k &= 3;
			if ( key == CU_K_SIN ) cu_int(r, right_sin[k]);
			else if ( key == CU_K_COS ) cu_int(r, right_cos[k]);
			else if ( ( k & 1 ) != 0 ) return CU_EDOM;
			else cu_zero(r);
			return CU_OK;
		}
	}
	if ( g->fn == NULL ) return CU_EDOM;
	*r = t;
	return g->fn(key, r, NULL, g->deg);
}

LOCAL INT function( CUENG *g, INT key, CUDEC *r, CONST CUDEC *v )
{
	CUDEC	one, t;
	D	n;
	INT	er;

	cu_int(&one, 1);
	switch ( key ) {
	case CU_K_SQRT:
		return cu_sqrt(r, v);
	case CU_K_SQR:
		return cu_mul(r, v, v);
	case CU_K_INV:
		return cu_div(r, &one, v);
	case CU_K_FACT:
		if ( cu_to_int(v, &n) < CU_OK || n < 0 ) return CU_EDOM;
		*r = one;
		for ( ; n > 1; n-- ) {
			cu_int(&t, n);
			if ( ( er = cu_mul(r, r, &t) ) < CU_OK ) return er;
		}
		return CU_OK;
	case CU_K_SIN:
	case CU_K_COS:
	case CU_K_TAN:
		return trig(g, key, r, v);
	case CU_K_ASIN:
	case CU_K_ACOS:
		cu_int(&t, -1);
		if ( cu_cmp(v, &one) > 0 || cu_cmp(v, &t) < 0 ) return CU_EDOM;
		break;
	case CU_K_LOG:
		if ( v->neg || cu_is_zero(v) ) return CU_EDOM;
		if ( v->c == 1 ) {			/* a power of ten */
			cu_int(r, v->e);
			return CU_OK;
		}
		break;
	case CU_K_LN:
		if ( v->neg || cu_is_zero(v) ) return CU_EDOM;
		if ( cu_cmp(v, &one) == 0 ) {
			cu_zero(r);
			return CU_OK;
		}
		break;
	case CU_K_EXP10:
		if ( cu_to_int(v, &n) >= CU_OK && n >= -999 && n <= 999 ) {
			cu_int(&t, 10);
			return cu_ipow(r, &t, n);
		}
		break;
	case CU_K_EXPE:
		if ( cu_is_zero(v) ) {
			*r = one;
			return CU_OK;
		}
		break;
	default:
		break;
	}
	if ( g->fn == NULL ) return CU_EDOM;
	*r = *v;
	return g->fn(key, r, NULL, g->deg);
}

LOCAL void one_number( CUENG *g, INT key )
{
	CUDEC	v, r;
	INT	er;

	take(g, &v);
	er = function(g, key, &r, &v);
	if ( er < CU_OK ) {
		fail(g, er);
		return;
	}
	g->x = r;
	g->want = FALSE;
}

/*
 * %: with + or − waiting, that percentage of the left operand is added
 * or taken away (200 + 5 % is 210); with × the product is taken as a
 * percentage (200 × 5 % is 10), with ÷ the quotient (50 ÷ 200 % is 25);
 * alone, the number is divided by 100.
 */
LOCAL void percent( CUENG *g )
{
	CUDEC	v, a, h, t, r;
	INT	er, op = 0;

	take(g, &v);
	hundred(&h);
	if ( g->nop > 0 && g->op[g->nop - 1] != CU_K_LP && g->op[g->nop - 1] != CU_K_POW ) {
		op = g->op[--g->nop];
		a = g->val[--g->nval];
	}
	switch ( op ) {
	case CU_K_ADD:
	case CU_K_SUB:
		er = cu_mul(&t, &a, &v);
		if ( er >= CU_OK ) er = cu_div(&t, &t, &h);
		if ( er >= CU_OK ) er = ( op == CU_K_ADD ) ? cu_add(&r, &a, &t) : cu_sub(&r, &a, &t);
		break;
	case CU_K_MUL:
		er = cu_mul(&t, &a, &v);
		if ( er >= CU_OK ) er = cu_div(&r, &t, &h);
		break;
	case CU_K_DIV:
		er = cu_div(&t, &a, &v);
		if ( er >= CU_OK ) er = cu_mul(&r, &t, &h);
		break;
	default:
		er = cu_div(&r, &v, &h);
		break;
	}
	if ( er < CU_OK ) {
		fail(g, er);
		return;
	}
	if ( op != 0 ) {
		g->lastop = op;
		g->lastv = ( op == CU_K_MUL ) ? a : v;
	}
	g->x = r;
	g->want = FALSE;
}

/* ---------------------------------------------------------------- typing */

LOCAL void start( CUENG *g )
{
	if ( !g->typing ) {
		g->typing = TRUE;
		g->nent = 0;
		g->eneg = FALSE;
		g->want = FALSE;
	}
}

LOCAL INT digits_typed( CUENG *g )
{
	INT	i, n = 0;

	for ( i = 0; i < g->nent; i++ ) {
		if ( g->ent[i] != '.' ) n++;
	}
	return n;
}

LOCAL BOOL has_point( CUENG *g )
{
	INT	i;

	for ( i = 0; i < g->nent; i++ ) {
		if ( g->ent[i] == '.' ) return TRUE;
	}
	return FALSE;
}

LOCAL void digit( CUENG *g, INT d )
{
	start(g);
	if ( g->nent == 1 && g->ent[0] == '0' ) {
		g->nent = 0;			/* a leading 0 is replaced */
	}
	if ( digits_typed(g) >= CU_SHOW || g->nent >= CU_ENTRY - 1 ) {
		return;
	}
	g->ent[g->nent++] = (char)( '0' + d );
}

LOCAL void point( CUENG *g )
{
	start(g);
	if ( has_point(g) || g->nent >= CU_ENTRY - 2 ) {
		return;
	}
	if ( g->nent == 0 ) g->ent[g->nent++] = '0';
	g->ent[g->nent++] = '.';
}

/* ---------------------------------------------------------------- the memory */

LOCAL void memory( CUENG *g, INT key )
{
	CUDEC	v;
	INT	er;

	if ( g->nop > 0 ) {
		equals(g);
		if ( g->err != CU_OK ) return;
	}
	take(g, &v);
	er = ( key == CU_K_MPLUS ) ? cu_add(&g->mem, &g->mem, &v) : cu_sub(&g->mem, &g->mem, &v);
	if ( er < CU_OK ) {
		cu_zero(&g->mem);
		fail(g, er);
		return;
	}
	g->want = FALSE;
}

/* ---------------------------------------------------------------- the keys */

EXPORT void cu_init( CUENG *g, BOOL sci, CUFN fn )
{
	zero(g, sizeof(*g));
	g->sci = sci;
	g->deg = TRUE;
	g->group = TRUE;
	g->fn = fn;
	cu_zero(&g->x);
	cu_zero(&g->mem);
}

EXPORT void cu_mode( CUENG *g, BOOL sci )
{
	/* what waits was ranked one way: done now, as = does */
	if ( g->nop > 0 && g->err == CU_OK ) equals(g);
	g->sci = sci;
}

LOCAL void all_clear( CUENG *g )
{
	CUDEC	mem = g->mem;
	CUFN	fn = g->fn;
	BOOL	sci = g->sci, deg = g->deg, group = g->group;

	cu_init(g, sci, fn);
	g->deg = deg;
	g->group = group;
	g->mem = mem;
}

EXPORT void cu_key( CUENG *g, INT key )
{
	if ( key == CU_K_AC ) {
		all_clear(g);
		return;
	}
	if ( key == CU_K_CE ) {
		if ( g->err != CU_OK ) {
			all_clear(g);
			return;
		}
		g->typing = FALSE;
		g->want = FALSE;
		cu_zero(&g->x);
		return;
	}
	if ( key == CU_K_ANGLE ) {
		g->deg = (BOOL)!g->deg;
		return;
	}
	if ( key == CU_K_MC ) {
		cu_zero(&g->mem);
		return;
	}
	if ( g->err != CU_OK ) {
		return;				/* only a clear goes on from an error */
	}
	if ( key >= CU_K_DIGIT(0) && key <= CU_K_DIGIT(9) ) {
		digit(g, key - CU_K_DIGIT(0));
		return;
	}
	switch ( key ) {
	case CU_K_POINT:
		point(g);
		break;
	case CU_K_00:
		digit(g, 0);
		digit(g, 0);
		break;
	case CU_K_BS:
		if ( g->typing && g->nent > 0 ) g->nent--;
		break;
	case CU_K_NEG:
		if ( g->typing ) {
			g->eneg = (BOOL)!g->eneg;
		} else if ( g->want ) {
			start(g);		/* the operand to come is negative */
			g->eneg = TRUE;
		} else {
			cu_neg(&g->x, &g->x);
			g->want = FALSE;
		}
		break;
	case CU_K_ADD:
	case CU_K_SUB:
	case CU_K_MUL:
	case CU_K_DIV:
	case CU_K_POW:
		operation(g, key);
		break;
	case CU_K_EQ:
		equals(g);
		break;
	case CU_K_PCT:
		percent(g);
		break;
	case CU_K_LP:
		open_bracket(g);
		break;
	case CU_K_RP:
		close_bracket(g);
		break;
	case CU_K_MPLUS:
	case CU_K_MMINUS:
		memory(g, key);
		break;
	case CU_K_MR:
		g->typing = FALSE;
		g->x = g->mem;
		g->want = FALSE;
		break;
	case CU_K_PI:
		g->typing = FALSE;
		(void)cu_parse(&g->x, "3.141592653589793238", 20, NULL);
		g->want = FALSE;
		break;
	case CU_K_SQRT:
	case CU_K_SQR:
	case CU_K_INV:
	case CU_K_FACT:
	case CU_K_SIN:
	case CU_K_COS:
	case CU_K_TAN:
	case CU_K_ASIN:
	case CU_K_ACOS:
	case CU_K_ATAN:
	case CU_K_LOG:
	case CU_K_LN:
	case CU_K_EXP10:
	case CU_K_EXPE:
		one_number(g, key);
		break;
	default:
		break;
	}
}

/* ---------------------------------------------------------------- what is shown */

typedef struct {
	char	*p;
	INT	n, max;
} OUT;

LOCAL void put( OUT *o, CONST char *s )
{
	while ( *s != 0 && o->n < o->max - 1 ) o->p[o->n++] = *s++;
	o->p[o->n] = 0;
}

LOCAL void put_c( OUT *o, char c )
{
	char	s[2];

	s[0] = c;
	s[1] = 0;
	put(o, s);
}

EXPORT CONST char *cu_err_text( INT err )
{
	switch ( err ) {
	case CU_EOVER:	return "桁あふれ";
	case CU_EDIV0:	return "0では割れません";
	case CU_EDOM:	return "計算できない値です";
	default:	return "エラー";
	}
}

EXPORT INT cu_show( CUENG *g, char *out, INT max )
{
	OUT	o;
	INT	i, w;

	o.p = out;
	o.n = 0;
	o.max = max;
	out[0] = 0;
	if ( g->err != CU_OK ) {
		put(&o, "E");
		return o.n;
	}
	if ( !g->typing ) {
		return cu_format(&g->x, out, max, g->group);
	}
	if ( g->eneg ) put_c(&o, '-');
	if ( g->nent == 0 ) {
		put_c(&o, '0');
		return o.n;
	}
	for ( w = 0; w < g->nent && g->ent[w] != '.'; w++ ) ;
	for ( i = 0; i < g->nent; i++ ) {
		if ( g->group && i > 0 && i < w && ( w - i ) % 3 == 0 ) put_c(&o, ',');
		put_c(&o, g->ent[i]);
	}
	return o.n;
}

LOCAL CONST char *op_text( INT op )
{
	switch ( op ) {
	case CU_K_ADD:	return "+";
	case CU_K_SUB:	return "−";
	case CU_K_MUL:	return "×";
	case CU_K_DIV:	return "÷";
	case CU_K_POW:	return "^";
	default:	return "(";
	}
}

EXPORT INT cu_expr( CUENG *g, char *out, INT max )
{
	OUT	o;
	char	s[48];
	INT	i, k = 0;

	o.p = out;
	o.n = 0;
	o.max = max;
	out[0] = 0;
	for ( i = 0; i < g->nop; i++ ) {
		if ( o.n > 0 ) put(&o, " ");
		if ( g->op[i] != CU_K_LP && k < g->nval ) {
			(void)cu_format(&g->val[k++], s, sizeof(s), g->group);
			put(&o, s);
			put(&o, " ");
		}
		put(&o, op_text(g->op[i]));
	}
	return o.n;
}

/* ---------------------------------------------------------------- from outside */

/* One letter of UTF-8 as the ASCII it stands for in a number, 0 for another */
LOCAL char ascii_of( CONST UB *s, INT n, INT *p_len )
{
	*p_len = 1;
	if ( s[0] < 0x80 ) return (char)s[0];
	if ( n >= 3 && s[0] == 0xEF && s[1] == 0xBC && s[2] >= 0x90 && s[2] <= 0x99 ) {
		*p_len = 3;
		return (char)( '0' + ( s[2] - 0x90 ) );		/* ０ to ９ */
	}
	if ( n >= 3 && s[0] == 0xEF && s[1] == 0xBC ) {
		*p_len = 3;
		switch ( s[2] ) {
		case 0x8B:	return '+';			/* ＋ */
		case 0x8C:	return ',';			/* ， */
		case 0x8D:	return '-';			/* － */
		case 0x8E:	return '.';			/* ． */
		case 0xA5:	return 'E';			/* Ｅ */
		default:	return 0;
		}
	}
	if ( n >= 3 && s[0] == 0xEF && s[1] == 0xBD && s[2] == 0x85 ) {
		*p_len = 3;
		return 'e';					/* ｅ */
	}
	if ( n >= 3 && s[0] == 0xE2 && s[1] == 0x88 && s[2] == 0x92 ) {
		*p_len = 3;
		return '-';					/* − */
	}
	if ( ( s[0] & 0xE0 ) == 0xC0 ) *p_len = 2;
	else if ( ( s[0] & 0xF0 ) == 0xE0 ) *p_len = 3;
	else if ( ( s[0] & 0xF8 ) == 0xF0 ) *p_len = 4;
	if ( *p_len > n ) *p_len = n;
	return 0;
}

EXPORT BOOL cu_paste( CUENG *g, CONST UB *s, INT n )
{
	char	a[128], b[96];
	INT	na = 0, nb = 0, i, len, at;
	BOOL	point = FALSE;
	CUDEC	v;

	/* the text as ASCII, each other letter a blank */
	for ( i = 0; i < n && na < (INT)sizeof(a) - 1; i += len ) {
		char	c = ascii_of(s + i, n - i, &len);

		a[na++] = ( c != 0 ) ? c : ' ';
	}
	a[na] = 0;

	/* the first digit, with a sign or a point just before it */
	for ( at = 0; at < na && !( a[at] >= '0' && a[at] <= '9' ); at++ ) ;
	if ( at == na ) {
		return FALSE;
	}
	if ( at > 0 && a[at - 1] == '.' ) at--;
	if ( at > 0 && ( a[at - 1] == '-' || a[at - 1] == '+' ) ) at--;
	for ( i = at; i < na && nb < (INT)sizeof(b) - 1; i++ ) {
		char	c = a[i];

		if ( c >= '0' && c <= '9' ) {
			b[nb++] = c;
		} else if ( ( c == '-' || c == '+' ) && i == at ) {
			b[nb++] = c;
		} else if ( c == '.' && !point ) {
			point = TRUE;
			b[nb++] = c;
		} else if ( c == ',' && !point && i + 1 < na && a[i + 1] >= '0' && a[i + 1] <= '9' ) {
			continue;			/* a comma between the digits of the whole part */
		} else if ( ( c == 'E' || c == 'e' ) && i + 1 < na
			 && ( ( a[i + 1] >= '0' && a[i + 1] <= '9' )
			   || ( ( a[i + 1] == '-' || a[i + 1] == '+' ) && i + 2 < na && a[i + 2] >= '0' && a[i + 2] <= '9' ) ) ) {
			b[nb++] = c;
			b[nb++] = a[++i];
			while ( i + 1 < na && a[i + 1] >= '0' && a[i + 1] <= '9' && nb < (INT)sizeof(b) - 1 ) {
				b[nb++] = a[++i];
			}
			break;
		} else {
			break;
		}
	}
	if ( cu_parse(&v, b, nb, NULL) < CU_OK ) {
		return FALSE;
	}
	if ( g->err != CU_OK ) all_clear(g);
	g->typing = FALSE;
	g->x = v;
	g->want = FALSE;
	return TRUE;
}

#define MOD_CTRL	( 0x01 | 0x10 )
#define MOD_ALT		( 0x04 | 0x40 )

EXPORT INT cu_hid_key( UINT code, UINT mods, UB ch )
{
	if ( ( mods & ( MOD_CTRL | MOD_ALT ) ) != 0 ) {
		return 0;
	}
	if ( code >= 0x59 && code <= 0x61 ) {
		return CU_K_DIGIT(code - 0x59 + 1);	/* the keypad's 1 to 9 */
	}
	switch ( code ) {
	case 0x62:	return CU_K_DIGIT(0);
	case 0x63:	return CU_K_POINT;
	case 0x54:	return CU_K_DIV;
	case 0x55:	return CU_K_MUL;
	case 0x56:	return CU_K_SUB;
	case 0x57:	return CU_K_ADD;
	case 0x58:					/* the keypad's Enter */
	case 0x28:	return CU_K_EQ;			/* Enter */
	case 0x29:	return CU_K_AC;			/* Esc */
	case 0x2A:	return CU_K_BS;			/* Backspace */
	case 0x4C:	return CU_K_CE;			/* Delete */
	default:	break;
	}
	if ( ch >= '0' && ch <= '9' ) {
		return CU_K_DIGIT(ch - '0');
	}
	switch ( ch ) {
	case '.':	return CU_K_POINT;
	case '+':	return CU_K_ADD;
	case '-':	return CU_K_SUB;
	case '*':	return CU_K_MUL;
	case '/':	return CU_K_DIV;
	case '=':	return CU_K_EQ;
	case '%':	return CU_K_PCT;
	case '(':	return CU_K_LP;
	case ')':	return CU_K_RP;
	case '^':	return CU_K_POW;
	default:	return 0;
	}
}
