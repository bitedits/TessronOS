/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	calc_ceval.c
 *	基本表計算: the formulas worked out without V8 (design 11.13)
 *
 *	The engine of a program built without V8. It keeps the cells as the
 *	V8 engine does and turns a formula into an expression the same way
 *	-- a range into the list of its numbers, a cell into its value, a
 *	number as it is and anything else quoted -- and then evaluates what
 *	it knows of that expression: numbers, strings, true and false, the
 *	lists, the operators of arithmetic, comparison and logic, ?:, and
 *	the functions SUM, AVERAGE, COUNT, MAX, MIN, IF, ROUND, FLOOR, CEIL,
 *	ABS, SQRT, POWER, MOD, PI, RAND and RANDBETWEEN. Their values, and
 *	how a value is written as words, are those of the V8 engine; a
 *	result a function cannot give is an error as the functions there
 *	give it ("Error: #NUM!"). Anything else in an expression -- a method,
 *	a name not in the table -- is #ERROR!, and a search is in the words
 *	as they are, a regular expression not understood.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "calc_eng.h"

/* ---------------------------------------------------------------- values */

#define V_UNDEF		0
#define V_NULL		1
#define V_NUM		2
#define V_STR		3
#define V_BOOL		4
#define V_ARR		5
#define V_ERR		6		/* a function's error: s is its words ("#NUM!") */
#define V_FN		7		/* a function of the table: n its index */

typedef struct {
	int	t;
	double	n;
	char	*s;			/* V_STR, V_ERR: owned */
	double	*a;			/* V_ARR: owned */
	int	na;
} CV;

static CV v_num( double n )	{ CV v; memset(&v, 0, sizeof(v)); v.t = V_NUM; v.n = n; return v; }
static CV v_bool( int b )	{ CV v; memset(&v, 0, sizeof(v)); v.t = V_BOOL; v.n = b ? 1 : 0; return v; }
static CV v_undef( void )	{ CV v; memset(&v, 0, sizeof(v)); return v; }

static CV v_strn( const char *s, size_t n )
{
	CV	v;

	memset(&v, 0, sizeof(v));
	v.t = V_STR;
	v.s = malloc(n + 1);
	if ( v.s != NULL ) {
		memcpy(v.s, s, n);
		v.s[n] = 0;
	}
	return v;
}

static CV v_str( const char *s )	{ return v_strn(s, strlen(s)); }

static CV v_err( const char *what )
{
	CV	v = v_str(what);

	v.t = V_ERR;
	return v;
}

static void v_free( CV *v )
{
	free(v->s);
	free(v->a);
	memset(v, 0, sizeof(*v));
}

static CV v_copy( const CV *v )
{
	CV	c = *v;

	if ( v->s != NULL ) {
		c.s = strdup(v->s);
	}
	if ( v->a != NULL ) {
		c.a = malloc(sizeof(double) * (size_t)( v->na > 0 ? v->na : 1 ));
		if ( c.a != NULL ) memcpy(c.a, v->a, sizeof(double) * (size_t)v->na);
	}
	return c;
}

/* ---------------------------------------------------------------- words of a value */

/* A number as the script's language writes it: the fewest digits that read back the same */
static void num_words( double x, char *out, size_t max )
{
	char	buf[40], dig[24];
	int	k, e = 0, nd = 0, i, p;

	if ( isnan(x) ) {
		snprintf(out, max, "NaN");
		return;
	}
	if ( isinf(x) ) {
		snprintf(out, max, x < 0 ? "-Infinity" : "Infinity");
		return;
	}
	if ( x == 0 ) {
		snprintf(out, max, "0");
		return;
	}
	for ( k = 1; k <= 17; k++ ) {
		snprintf(buf, sizeof(buf), "%.*e", k - 1, x);
		if ( strtod(buf, NULL) == x ) break;
	}
	/* "-d.ddde+XX": the digits and the exponent */
	for ( i = ( buf[0] == '-' ) ? 1 : 0; buf[i] != 0 && buf[i] != 'e'; i++ ) {
		if ( buf[i] >= '0' && buf[i] <= '9' && nd < 20 ) dig[nd++] = buf[i];
	}
	dig[nd] = 0;
	if ( buf[i] == 'e' ) e = atoi(buf + i + 1);
	while ( nd > 1 && dig[nd - 1] == '0' ) dig[--nd] = 0;
	p = e + 1;				/* the point after this many digits */
	i = 0;
	if ( x < 0 && max > 1 ) out[i++] = '-';
	if ( nd <= p && p <= 21 ) {
		for ( k = 0; k < nd && (size_t)i < max - 1; k++ ) out[i++] = dig[k];
		for ( k = nd; k < p && (size_t)i < max - 1; k++ ) out[i++] = '0';
	} else if ( 0 < p && p <= 21 ) {
		for ( k = 0; k < nd && (size_t)i < max - 2; k++ ) {
			if ( k == p ) out[i++] = '.';
			out[i++] = dig[k];
		}
	} else if ( -6 < p && p <= 0 ) {
		if ( (size_t)i < max - 2 ) {
			out[i++] = '0';
			out[i++] = '.';
		}
		for ( k = 0; k < -p && (size_t)i < max - 1; k++ ) out[i++] = '0';
		for ( k = 0; k < nd && (size_t)i < max - 1; k++ ) out[i++] = dig[k];
	} else {
		out[i] = 0;
		snprintf(out + i, max - (size_t)i, "%c%s%s%se%c%d", dig[0], nd > 1 ? "." : "", nd > 1 ? dig + 1 : "",
			 "", ( p - 1 >= 0 ) ? '+' : '-', abs(p - 1));
		return;
	}
	out[i] = 0;
}

/* String(v), in memory of the caller's to free */
static char *v_words( const CV *v )
{
	char	b[40];
	size_t	n, at;
	char	*s;
	int	i;

	switch ( v->t ) {
	case V_NUM:	num_words(v->n, b, sizeof(b)); return strdup(b);
	case V_BOOL:	return strdup(v->n != 0 ? "true" : "false");
	case V_STR:	return strdup(v->s != NULL ? v->s : "");
	case V_NULL:	return strdup("null");
	case V_ERR:
		n = strlen(v->s != NULL ? v->s : "") + 8;
		s = malloc(n);
		if ( s != NULL ) snprintf(s, n, "Error: %s", v->s != NULL ? v->s : "");
		return s;
	case V_ARR:
		s = malloc((size_t)v->na * 26 + 1);
		if ( s == NULL ) return NULL;
		at = 0;
		for ( i = 0; i < v->na; i++ ) {
			if ( i > 0 ) s[at++] = ',';
			num_words(v->a[i], s + at, 26);
			at += strlen(s + at);
		}
		s[at] = 0;
		return s;
	case V_FN:	return strdup("function");
	default:	return strdup("undefined");
	}
}

static int white( unsigned char c )
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

/* Number(s) */
static double str_num( const char *s )
{
	const char	*a = s, *e;
	char		*end;
	double		x;
	size_t		n;

	while ( white((unsigned char)*a) ) a++;
	n = strlen(a);
	while ( n > 0 && white((unsigned char)a[n - 1]) ) n--;
	if ( n == 0 ) return 0;
	{
		char	*t = malloc(n + 1);

		if ( t == NULL ) return NAN;
		memcpy(t, a, n);
		t[n] = 0;
		e = t;
		if ( strcmp(t, "Infinity") == 0 || strcmp(t, "+Infinity") == 0 ) x = INFINITY;
		else if ( strcmp(t, "-Infinity") == 0 ) x = -INFINITY;
		else if ( n > 2 && t[0] == '0' && ( t[1] == 'x' || t[1] == 'X' ) ) {
			x = (double)strtoull(t + 2, &end, 16);
			if ( *end != 0 ) x = NAN;
		} else {
			/* only a decimal number: no "nan", "inf" or hexadecimal of strtod's own */
			const char	*p = t;

			if ( *p == '+' || *p == '-' ) p++;
			if ( !( ( *p >= '0' && *p <= '9' ) || ( *p == '.' && p[1] >= '0' && p[1] <= '9' ) ) ) {
				x = NAN;
			} else {
				x = strtod(t, &end);
				if ( *end != 0 ) x = NAN;
			}
		}
		free((void *)e);
	}
	return x;
}

static double v_numval( const CV *v )
{
	switch ( v->t ) {
	case V_NUM:
	case V_BOOL:	return v->n;
	case V_NULL:	return 0;
	case V_STR:	return str_num(v->s != NULL ? v->s : "");
	case V_ARR:
		if ( v->na == 0 ) return 0;
		if ( v->na == 1 ) return v->a[0];
		return NAN;
	default:	return NAN;
	}
}

static int v_true( const CV *v )
{
	switch ( v->t ) {
	case V_NUM:	return !( v->n == 0 || isnan(v->n) );
	case V_BOOL:	return v->n != 0;
	case V_STR:	return v->s != NULL && v->s[0] != 0;
	case V_ARR:
	case V_ERR:
	case V_FN:	return 1;
	default:	return 0;
	}
}

/* ---------------------------------------------------------------- the cells */

typedef struct {
	int	col, row;
	char	*value, *formula;
	CV	shown;			/* displayValue */
} CCELL;

static CCELL	*cells;
static int	ncell, mcell;
static int	*changed;		/* pairs, in the order they first changed */
static int	nchg, mchg;

static int find_cell( int col, int row )
{
	int	i;

	for ( i = 0; i < ncell; i++ ) {
		if ( cells[i].col == col && cells[i].row == row ) return i;
	}
	return -1;
}

static int make_cell( int col, int row )
{
	int	i = find_cell(col, row);

	if ( i >= 0 ) return i;
	if ( ncell == mcell ) {
		int	m = mcell ? mcell * 2 : 64;
		CCELL	*c = realloc(cells, sizeof(CCELL) * (size_t)m);

		if ( c == NULL ) return -1;
		cells = c;
		mcell = m;
	}
	memset(&cells[ncell], 0, sizeof(CCELL));
	cells[ncell].col = col;
	cells[ncell].row = row;
	cells[ncell].value = strdup("");
	cells[ncell].formula = strdup("");
	cells[ncell].shown = v_str("");
	return ncell++;
}

static void touch( int col, int row )
{
	int	i;

	for ( i = 0; i < nchg; i++ ) {
		if ( changed[2 * i] == col && changed[2 * i + 1] == row ) return;
	}
	if ( nchg == mchg ) {
		int	m = mchg ? mchg * 2 : 64, *c = realloc(changed, sizeof(int) * 2 * (size_t)m);

		if ( c == NULL ) return;
		changed = c;
		mchg = m;
	}
	changed[2 * nchg] = col;
	changed[2 * nchg + 1] = row;
	nchg++;
}

/* ---------------------------------------------------------------- names in a formula */

static int letter_col( const char *s, int n )
{
	long long	col = 0;
	int		i;

	for ( i = 0; i < n; i++ ) {
		col = col * 26 + ( s[i] - 64 );
		if ( col > 0x3FFFFFFF ) return 0x3FFFFFFF;
	}
	return (int)col;
}

static int col_letters( int col, char *out )
{
	char	t[8];
	int	n = 0, k = 0;

	while ( col > 0 && n < 7 ) {
		col--;
		t[n++] = (char)( 'A' + col % 26 );
		col /= 26;
	}
	while ( n > 0 ) out[k++] = t[--n];
	out[k] = 0;
	return k;
}

static int up( char c )		{ return c >= 'A' && c <= 'Z'; }
static int dig( char c )	{ return c >= '0' && c <= '9'; }
static int word( char c )	{ return up(c) || dig(c) || ( c >= 'a' && c <= 'z' ) || c == '_'; }

/* A growing string */
typedef struct {
	char	*p;
	size_t	n, max;
} SB;

static void sb_putn( SB *b, const char *s, size_t n )
{
	if ( b->n + n + 1 > b->max ) {
		size_t	m = ( b->n + n + 1 ) * 2;
		char	*q = realloc(b->p, m);

		if ( q == NULL ) return;
		b->p = q;
		b->max = m;
	}
	memcpy(b->p + b->n, s, n);
	b->n += n;
	b->p[b->n] = 0;
}

static void sb_put( SB *b, const char *s )	{ sb_putn(b, s, strlen(s)); }

/* The cells being worked out now: a cell named among them comes round to itself */
typedef struct EVSET {
	int	n;
	int	*k;			/* pairs */
} EVSET;

static int ev_has( const EVSET *e, int col, int row )
{
	int	i;

	for ( i = 0; i < e->n; i++ ) {
		if ( e->k[2 * i] == col && e->k[2 * i + 1] == row ) return 1;
	}
	return 0;
}

static CV evaluate( const char *formula, int col, int row, const EVSET *ev );

/* A cell's value: a formula's worked out again */
static CV value_recalc( int col, int row, const EVSET *ev )
{
	int	i = find_cell(col, row);

	if ( i < 0 ) return v_str("");
	if ( cells[i].formula[0] != 0 ) {
		EVSET	in;
		CV	v;
		char	*f;

		in.n = ev->n + 1;
		in.k = malloc(sizeof(int) * 2 * (size_t)in.n);
		if ( in.k == NULL ) return v_str("");
		memcpy(in.k, ev->k, sizeof(int) * 2 * (size_t)ev->n);
		in.k[2 * ev->n] = col;
		in.k[2 * ev->n + 1] = row;
		f = strdup(cells[i].formula);
		v = evaluate(f, col, row, &in);
		free(f);
		free(in.k);
		i = find_cell(col, row);
		if ( i >= 0 ) {
			v_free(&cells[i].shown);
			cells[i].shown = v_copy(&v);
		}
		return v;
	}
	return v_str(cells[i].value);
}

/* A value put in the expression: a number as it is, anything else quoted */
static void put_value( SB *b, const CV *v )
{
	char	*w = v_words(v);
	double	x = v_numval(v);

	if ( v->t == V_STR ) x = str_num(v->s != NULL ? v->s : "");
	if ( w == NULL ) return;
	if ( isnan(x) ) {
		sb_put(b, "\"");
		sb_put(b, w);
		sb_put(b, "\"");
	} else {
		sb_put(b, w);
	}
	free(w);
}

/* ---------------------------------------------------------------- the expression */

typedef struct {
	const char	*s;
	int		at;
	int		bad;		/* not an expression */
	int		thrown;		/* a name not known, a call of what is not a function */
} PX;

static void skip( PX *p )
{
	while ( white((unsigned char)p->s[p->at]) ) p->at++;
}

static int eat( PX *p, const char *op )
{
	size_t	n = strlen(op);

	skip(p);
	if ( strncmp(p->s + p->at, op, n) != 0 ) return 0;
	p->at += (int)n;
	return 1;
}

static int peek( PX *p, const char *op )
{
	skip(p);
	return strncmp(p->s + p->at, op, strlen(op)) == 0;
}

static CV cond( PX *p, int live );

/* ---------------------------------------------------------------- the functions */

static const char *const fn_name[] = {
	"SUM", "AVERAGE", "COUNT", "MAX", "MIN", "IF", "ROUND", "FLOOR", "CEIL", "ABS", "SQRT", "POWER",
	"MOD", "PI", "RAND", "RANDBETWEEN"
};
#define FN_N	( (int)( sizeof(fn_name) / sizeof(fn_name[0]) ) )
enum { F_SUM, F_AVERAGE, F_COUNT, F_MAX, F_MIN, F_IF, F_ROUND, F_FLOOR, F_CEIL, F_ABS, F_SQRT, F_POWER,
       F_MOD, F_PI, F_RAND, F_RANDBETWEEN };

/* The first error among the arguments, or -1 */
static int first_err( const CV *a, int n )
{
	int	i;

	for ( i = 0; i < n; i++ ) {
		if ( a[i].t == V_ERR ) return i;
	}
	return -1;
}

/* An argument as a number, as the functions read one; 0 and an error when it is not one */
static int arg_num( const CV *v, double *x )
{
	switch ( v->t ) {
	case V_UNDEF:
	case V_NULL:	*x = 0; return 1;
	case V_NUM:
	case V_BOOL:	*x = v->n; return 1;
	case V_STR:
		*x = str_num(v->s != NULL ? v->s : "");
		return !isnan(*x);
	case V_ARR:
		*x = ( v->na > 0 ) ? v->a[0] : 0;
		return 1;
	default:	*x = 0; return 0;
	}
}

/* Every number of the arguments, lists opened; strings that are numbers too when 'strings' */
static int numbers( const CV *a, int n, int strings, double *out, int max )
{
	int	i, k, m = 0;

	for ( i = 0; i < n; i++ ) {
		if ( a[i].t == V_NUM && m < max ) out[m++] = a[i].n;
		else if ( a[i].t == V_ARR ) {
			for ( k = 0; k < a[i].na && m < max; k++ ) out[m++] = a[i].a[k];
		} else if ( strings && a[i].t == V_STR ) {
			double	x = str_num(a[i].s != NULL ? a[i].s : "");

			if ( !isnan(x) && a[i].s[0] != 0 && m < max ) out[m++] = x;
		}
	}
	return m;
}

/* ROUND: through the digits as written, as the script's functions do */
static double round_to( double x, double d )
{
	char	w[40], e[40], t[96];
	double	y;

	num_words(x, w, sizeof(w));
	num_words(d, e, sizeof(e));
	snprintf(t, sizeof(t), "%se%s", w, e);
	y = str_num(t);
	if ( isnan(y) ) return NAN;
	y = floor(y + 0.5);
	num_words(y, w, sizeof(w));
	num_words(-d, e, sizeof(e));
	snprintf(t, sizeof(t), "%se%s", w, e);
	return str_num(t);
}

static CV call_fn( int f, CV *a, int n )
{
	double	x, y, *nums;
	int	k, m, e = first_err(a, n);
	CV	r;

	switch ( f ) {
	case F_SUM:
	case F_AVERAGE:
	case F_COUNT:
	case F_MAX:
	case F_MIN:
		if ( e >= 0 ) return v_copy(&a[e]);
		for ( k = 0, m = 0; k < n; k++ ) m += ( a[k].t == V_ARR ) ? a[k].na : 1;
		nums = malloc(sizeof(double) * (size_t)( m + 1 ));
		if ( nums == NULL ) return v_err("#VALUE!");
		m = numbers(a, n, f == F_SUM, nums, m + 1);
		x = 0;
		if ( f == F_SUM || f == F_AVERAGE ) {
			for ( k = 0; k < m; k++ ) x += nums[k];
			if ( f == F_AVERAGE ) {
				if ( m == 0 ) {
					free(nums);
					return v_err("#DIV/0!");
				}
				x /= m;
			}
		} else if ( f == F_COUNT ) {
			x = m;
		} else if ( m > 0 ) {
			x = nums[0];
			for ( k = 1; k < m; k++ ) {
				if ( f == F_MAX ? nums[k] > x : nums[k] < x ) x = nums[k];
			}
		}
		free(nums);
		return v_num(x);
	case F_IF:
		if ( n > 0 && a[0].t == V_ERR ) return v_copy(&a[0]);
		if ( n > 0 && v_true(&a[0]) ) return ( n > 1 ) ? v_copy(&a[1]) : v_bool(1);
		return ( n > 2 ) ? v_copy(&a[2]) : v_bool(0);
	case F_PI:
		return v_num(M_PI);
	case F_RAND:
		return v_num((double)rand() / ( (double)RAND_MAX + 1 ));
	default:
		break;
	}
	if ( e >= 0 ) return v_copy(&a[e]);
	if ( !arg_num(n > 0 ? &a[0] : &(CV){ 0 }, &x) ) return v_err("#VALUE!");
	if ( !arg_num(n > 1 ? &a[1] : &(CV){ 0 }, &y) ) return v_err("#VALUE!");
	switch ( f ) {
	case F_ROUND:
		return v_num(round_to(x, y));
	case F_FLOOR:
		if ( y == 0 ) return v_num(0);
		if ( ( x < 0 ) != ( y < 0 ) && x != 0 ) return v_err("#NUM!");
		if ( x < 0 ) return v_num(-ceil(-x / -y) * -y);
		return v_num(floor(x / y) * y);
	case F_CEIL:
		if ( y == 0 ) return v_num(0);
		y = fabs(y);
		if ( x < 0 ) return v_num(-floor(-x / y) * y);
		return v_num(ceil(x / y) * y);
	case F_ABS:
		return v_num(fabs(x));
	case F_SQRT:
		if ( x < 0 ) return v_err("#NUM!");
		return v_num(sqrt(x));
	case F_POWER:
		r = v_num(pow(x, y));
		if ( isnan(r.n) ) return v_err("#NUM!");
		return r;
	case F_MOD:
		if ( y == 0 ) return v_err("#DIV/0!");
		{
			double	md = fabs(fmod(x, y));

			if ( x < 0 && md != 0 ) md = fabs(y) - md;
			return v_num(y > 0 ? md : -md);
		}
	case F_RANDBETWEEN:
		return v_num(floor((double)rand() / ( (double)RAND_MAX + 1 ) * ( y - x + 1 )) + x);
	default:
		return v_err("#VALUE!");
	}
}

/* ---------------------------------------------------------------- the grammar */

static CV primary( PX *p, int live )
{
	CV	v;

	skip(p);
	if ( p->bad ) return v_undef();
	{
		char	c = p->s[p->at];

		if ( c == '(' ) {
			p->at++;
			v = cond(p, live);
			if ( !eat(p, ")") ) p->bad = 1;
			return v;
		}
		if ( c == '"' || c == '\'' ) {
			SB	b = { NULL, 0, 0 };

			p->at++;
			sb_put(&b, "");
			while ( p->s[p->at] != 0 && p->s[p->at] != c ) {
				char	ch = p->s[p->at++];

				if ( ch == '\\' && p->s[p->at] != 0 ) {
					ch = p->s[p->at++];
					if ( ch == 'n' ) ch = '\n';
					else if ( ch == 't' ) ch = '\t';
				}
				sb_putn(&b, &ch, 1);
			}
			if ( p->s[p->at] != c ) {
				p->bad = 1;
				free(b.p);
				return v_undef();
			}
			p->at++;
			v = v_str(b.p != NULL ? b.p : "");
			free(b.p);
			return v;
		}
		if ( c == '[' ) {
			double	*a = NULL;
			int	na = 0;

			p->at++;
			if ( !eat(p, "]") ) {
				do {
					CV	e = cond(p, live);
					double	*q = realloc(a, sizeof(double) * (size_t)( na + 1 ));

					if ( q != NULL ) {
						a = q;
						a[na++] = v_numval(&e);
					}
					v_free(&e);
				} while ( !p->bad && eat(p, ",") );
				if ( !eat(p, "]") ) p->bad = 1;
			}
			memset(&v, 0, sizeof(v));
			v.t = V_ARR;
			v.a = a;
			v.na = na;
			return v;
		}
		if ( dig(c) || ( c == '.' && dig(p->s[p->at + 1]) ) ) {
			char	*end;
			double	x = strtod(p->s + p->at, &end);

			p->at = (int)( end - p->s );
			if ( word(p->s[p->at]) ) p->bad = 1;
			return v_num(x);
		}
		if ( word(c) || c == '$' ) {
			int	a = p->at, k;
			char	nm[64];

			while ( word(p->s[p->at]) || p->s[p->at] == '$' ) p->at++;
			snprintf(nm, sizeof(nm), "%.*s", p->at - a, p->s + a);
			if ( strcmp(nm, "true") == 0 ) return v_bool(1);
			if ( strcmp(nm, "false") == 0 ) return v_bool(0);
			if ( strcmp(nm, "null") == 0 ) {
				v = v_undef();
				v.t = V_NULL;
				return v;
			}
			if ( strcmp(nm, "undefined") == 0 ) return v_undef();
			if ( strcmp(nm, "NaN") == 0 ) return v_num(NAN);
			if ( strcmp(nm, "Infinity") == 0 ) return v_num(INFINITY);
			for ( k = 0; k < FN_N; k++ ) {
				if ( strcmp(nm, fn_name[k]) == 0 ) {
					v = v_undef();
					v.t = V_FN;
					v.n = k;
					return v;
				}
			}
			if ( live ) p->thrown = 1;
			return v_undef();
		}
	}
	p->bad = 1;
	return v_undef();
}

static CV postfix( PX *p, int live )
{
	CV	v = primary(p, live);

	while ( !p->bad && peek(p, "(") ) {
		CV	args[32];
		int	n = 0, k;

		p->at++;
		if ( !eat(p, ")") ) {
			do {
				CV	a = cond(p, live);

				if ( n < 32 ) args[n++] = a;
				else v_free(&a);
			} while ( !p->bad && eat(p, ",") );
			if ( !eat(p, ")") ) p->bad = 1;
		}
		if ( live && !p->bad ) {
			if ( v.t != V_FN ) {
				p->thrown = 1;
				v_free(&v);
				v = v_undef();
			} else {
				CV	r = call_fn((int)v.n, args, n);

				v_free(&v);
				v = r;
			}
		}
		for ( k = 0; k < n; k++ ) v_free(&args[k]);
	}
	return v;
}

static CV unary( PX *p, int live );

static CV power( PX *p, int live )
{
	CV	v = postfix(p, live);

	if ( !p->bad && peek(p, "**") ) {
		CV	e;
		double	x;

		p->at += 2;
		e = unary(p, live);
		x = pow(v_numval(&v), v_numval(&e));
		v_free(&v);
		v_free(&e);
		return v_num(x);
	}
	return v;
}

static CV unary( PX *p, int live )
{
	CV	v;
	double	x;

	skip(p);
	if ( p->s[p->at] == '!' && p->s[p->at + 1] != '=' ) {
		p->at++;
		v = unary(p, live);
		x = !v_true(&v);
		v_free(&v);
		return v_bool((int)x);
	}
	if ( p->s[p->at] == '-' || p->s[p->at] == '+' ) {
		int	neg = ( p->s[p->at] == '-' );

		p->at++;
		v = unary(p, live);
		x = v_numval(&v);
		v_free(&v);
		return v_num(neg ? -x : x);
	}
	return power(p, live);
}

static CV mul( PX *p, int live )
{
	CV	v = unary(p, live);

	for ( ;; ) {
		char	op;
		CV	w;
		double	a, b, r;

		skip(p);
		op = p->s[p->at];
		if ( p->bad || ( op != '*' && op != '/' && op != '%' ) || p->s[p->at + 1] == '*' ) return v;
		p->at++;
		w = unary(p, live);
		a = v_numval(&v);
		b = v_numval(&w);
		r = ( op == '*' ) ? a * b : ( op == '/' ) ? a / b : fmod(a, b);
		v_free(&v);
		v_free(&w);
		v = v_num(r);
	}
}

/* Whether + is a joining of words: either side is not a number, true, false, null or undefined */
static int joins( const CV *v )
{
	return v->t == V_STR || v->t == V_ARR || v->t == V_ERR || v->t == V_FN;
}

static CV add( PX *p, int live )
{
	CV	v = mul(p, live);

	for ( ;; ) {
		char	op;
		CV	w;

		skip(p);
		op = p->s[p->at];
		if ( p->bad || ( op != '+' && op != '-' ) ) return v;
		p->at++;
		w = mul(p, live);
		if ( op == '+' && ( joins(&v) || joins(&w) ) ) {
			char	*a = v_words(&v), *b = v_words(&w);
			SB	s = { NULL, 0, 0 };

			sb_put(&s, a != NULL ? a : "");
			sb_put(&s, b != NULL ? b : "");
			free(a);
			free(b);
			v_free(&v);
			v = v_str(s.p != NULL ? s.p : "");
			free(s.p);
		} else {
			double	r = ( op == '+' ) ? v_numval(&v) + v_numval(&w) : v_numval(&v) - v_numval(&w);

			v_free(&v);
			v = v_num(r);
		}
		v_free(&w);
	}
}

/* <, >, <=, >=: words with words by their letters, anything else as numbers */
static int compare( const CV *a, const CV *b, const char *op )
{
	if ( joins(a) && joins(b) && a->t != V_FN && b->t != V_FN ) {
		char	*x = v_words(a), *y = v_words(b);
		int	c = strcmp(x != NULL ? x : "", y != NULL ? y : "");

		free(x);
		free(y);
		if ( op[0] == '<' ) return op[1] == '=' ? c <= 0 : c < 0;
		return op[1] == '=' ? c >= 0 : c > 0;
	} else {
		double	x = v_numval(a), y = v_numval(b);

		if ( op[0] == '<' ) return op[1] == '=' ? x <= y : x < y;
		return op[1] == '=' ? x >= y : x > y;
	}
}

static CV rel( PX *p, int live )
{
	CV	v = add(p, live);

	for ( ;; ) {
		const char	*op;
		CV		w;
		int		r;

		skip(p);
		if ( p->bad ) return v;
		if ( peek(p, "<=") ) op = "<=";
		else if ( peek(p, ">=") ) op = ">=";
		else if ( peek(p, "<") ) op = "<";
		else if ( peek(p, ">") ) op = ">";
		else return v;
		p->at += (int)strlen(op);
		w = add(p, live);
		r = compare(&v, &w, op);
		v_free(&v);
		v_free(&w);
		v = v_bool(r);
	}
}

static int equal( const CV *a, const CV *b, int strict )
{
	if ( a->t == b->t ) {
		switch ( a->t ) {
		case V_NUM:
		case V_BOOL:	return a->n == b->n;
		case V_STR:	return strcmp(a->s ? a->s : "", b->s ? b->s : "") == 0;
		case V_UNDEF:
		case V_NULL:	return 1;
		default:	return 0;
		}
	}
	if ( strict ) return 0;
	if ( ( a->t == V_NULL || a->t == V_UNDEF ) && ( b->t == V_NULL || b->t == V_UNDEF ) ) return 1;
	if ( a->t == V_NULL || a->t == V_UNDEF || b->t == V_NULL || b->t == V_UNDEF ) return 0;
	if ( ( a->t == V_ARR || a->t == V_ERR ) && ( b->t == V_ARR || b->t == V_ERR ) ) return 0;
	if ( joins(a) && joins(b) ) {
		char	*x = v_words(a), *y = v_words(b);
		int	r = strcmp(x ? x : "", y ? y : "") == 0;

		free(x);
		free(y);
		return r;
	}
	return v_numval(a) == v_numval(b);
}

static CV eq( PX *p, int live )
{
	CV	v = rel(p, live);

	for ( ;; ) {
		int	neg, strict, r;
		CV	w;

		skip(p);
		if ( p->bad ) return v;
		if ( peek(p, "===") ) { neg = 0; strict = 1; p->at += 3; }
		else if ( peek(p, "!==") ) { neg = 1; strict = 1; p->at += 3; }
		else if ( peek(p, "==") ) { neg = 0; strict = 0; p->at += 2; }
		else if ( peek(p, "!=") ) { neg = 1; strict = 0; p->at += 2; }
		else return v;
		w = rel(p, live);
		r = equal(&v, &w, strict);
		v_free(&v);
		v_free(&w);
		v = v_bool(neg ? !r : r);
	}
}

/* && and ||: the value of the side that decided, the other not worked out */
static CV logic_and( PX *p, int live )
{
	CV	v = eq(p, live);

	while ( !p->bad && eat(p, "&&") ) {
		int	go = live && v_true(&v);
		CV	w = eq(p, go);

		if ( go ) {
			v_free(&v);
			v = w;
		} else {
			v_free(&w);
		}
	}
	return v;
}

static CV logic_or( PX *p, int live )
{
	CV	v = logic_and(p, live);

	while ( !p->bad && eat(p, "||") ) {
		int	go = live && !v_true(&v);
		CV	w = logic_and(p, go);

		if ( go ) {
			v_free(&v);
			v = w;
		} else {
			v_free(&w);
		}
	}
	return v;
}

static CV cond( PX *p, int live )
{
	CV	c = logic_or(p, live), a, b;
	int	t;

	if ( p->bad || !eat(p, "?") ) return c;
	t = v_true(&c);
	v_free(&c);
	a = cond(p, live && t);
	if ( !eat(p, ":") ) {
		p->bad = 1;
		v_free(&a);
		return v_undef();
	}
	b = cond(p, live && !t);
	if ( t ) {
		v_free(&b);
		return a;
	}
	v_free(&a);
	return b;
}

/* ---------------------------------------------------------------- a formula */

static const char *const dangerous[] = {
	"require", "import", "process", "window", "document", "global", "globalThis", "self", "top",
	"parent", "frames", "constructor", "__proto__", "prototype", "eval", "Function", "setTimeout",
	"setInterval", "setImmediate", "fetch", "XMLHttpRequest", "WebSocket", "Worker", "Blob", "module",
	"exports", "child_process", "fs", "path", "os", "net", "http", "https", "crypto"
};

static int has_dangerous( const char *s )
{
	int	i = 0, k;

	while ( s[i] != 0 ) {
		int	a;

		if ( !word(s[i]) ) {
			i++;
			continue;
		}
		a = i;
		while ( word(s[i]) ) i++;
		for ( k = 0; k < (int)( sizeof(dangerous) / sizeof(dangerous[0]) ); k++ ) {
			if ( (int)strlen(dangerous[k]) == i - a && strncmp(s + a, dangerous[k], (size_t)( i - a )) == 0 ) {
				return 1;
			}
		}
	}
	return 0;
}

/* Whether s[at..] is an A1 name with the edges of a word: its length, letters and digits */
static int a1_at( const char *s, int at, int *p_col, long long *p_row, int bounded, int *p_nl )
{
	int	i = at, a;
	long long	r = 0;

	if ( bounded && at > 0 && word(s[at - 1]) ) return 0;
	while ( up(s[i]) ) i++;
	if ( i == at ) return 0;
	a = i;
	while ( dig(s[i]) ) {
		if ( r < 100000000000LL ) r = r * 10 + ( s[i] - '0' );
		i++;
	}
	if ( i == a ) return 0;
	if ( bounded && word(s[i]) ) return 0;
	*p_col = letter_col(s + at, a - at);
	*p_row = r;
	*p_nl = a - at;
	return i - at;
}

/* "[row,col]" of either width at s[at..]: its length */
static int old_at( const char *s, int at, int *p_col, long long *p_row )
{
	static const char *const open[2] = { "[", "\xEF\xBC\xBB" }, *const comma[2] = { ",", "\xEF\xBC\x8C" },
			  *const shut[2] = { "]", "\xEF\xBC\xBD" };
	int		i = at, k, a;
	long long	r = 0, c = 0;

	for ( k = 0; k < 2 && strncmp(s + i, open[k], strlen(open[k])) != 0; k++ ) ;
	if ( k == 2 ) return 0;
	i += (int)strlen(open[k]);
	for ( a = i; dig(s[i]); i++ ) if ( r < 100000000000LL ) r = r * 10 + ( s[i] - '0' );
	if ( i == a ) return 0;
	for ( k = 0; k < 2 && strncmp(s + i, comma[k], strlen(comma[k])) != 0; k++ ) ;
	if ( k == 2 ) return 0;
	i += (int)strlen(comma[k]);
	for ( a = i; dig(s[i]); i++ ) if ( c < 100000000000LL ) c = c * 10 + ( s[i] - '0' );
	if ( i == a ) return 0;
	for ( k = 0; k < 2 && strncmp(s + i, shut[k], strlen(shut[k])) != 0; k++ ) ;
	if ( k == 2 ) return 0;
	i += (int)strlen(shut[k]);
	*p_row = r;
	*p_col = ( c > 0x3FFFFFFF ) ? 0x3FFFFFFF : (int)c;
	return i - at;
}

static CV evaluate( const char *formula, int col, int row, const EVSET *ev )
{
	SB		a = { NULL, 0, 0 }, b = { NULL, 0, 0 }, c = { NULL, 0, 0 };
	const char	*s;
	int		i, n;
	PX		p;
	CV		v;

	(void)col;
	(void)row;
	/* the text after "=", trimmed */
	s = formula + 1;
	while ( white((unsigned char)*s ) ) s++;
	n = (int)strlen(s);
	while ( n > 0 && white((unsigned char)s[n - 1]) ) n--;
	sb_putn(&a, s, (size_t)n);
	if ( a.p == NULL ) sb_put(&a, "");

	/* a range: the numbers in it */
	for ( i = 0; a.p[i] != 0; ) {
		int		c0, c1, len, nl, k;
		long long	r0, r1;

		len = a1_at(a.p, i, &c0, &r0, 0, &nl);
		if ( len > 0 && a.p[i + len] == ':' && ( k = a1_at(a.p, i + len + 1, &c1, &r1, 0, &nl) ) > 0 ) {
			SB		list = { NULL, 0, 0 };
			int		circ = 0, first = 1, cc;
			long long	rr;

			sb_put(&list, "[");
			for ( rr = r0; rr <= r1 && !circ; rr++ ) {
				for ( cc = c0; cc <= c1; cc++ ) {
					CV	x;

					if ( ev_has(ev, cc, (int)rr) ) {
						circ = 1;
						break;
					}
					x = value_recalc(cc, (int)rr, ev);
					if ( !( x.t == V_STR && ( x.s == NULL || x.s[0] == 0 ) ) && !isnan(v_numval(&x)) ) {
						char	w[32];

						num_words(v_numval(&x), w, sizeof(w));
						/* ", " keeps a list of two from reading as [row,col] */
						if ( !first ) sb_put(&list, ", ");
						sb_put(&list, w);
						first = 0;
					}
					v_free(&x);
				}
			}
			sb_put(&list, "]");
			sb_put(&b, circ ? "#CIRCULAR!" : list.p);
			free(list.p);
			i += len + 1 + k;
			continue;
		}
		sb_putn(&b, a.p + i, 1);
		i++;
	}
	if ( b.p == NULL ) sb_put(&b, "");

	/* [row,col] */
	for ( i = 0; b.p[i] != 0; ) {
		int		cc, len;
		long long	rr;

		len = old_at(b.p, i, &cc, &rr);
		if ( len > 0 ) {
			if ( ev_has(ev, cc, (int)rr) ) {
				sb_put(&c, "#CIRCULAR!");
			} else {
				CV	x = value_recalc(cc, (int)rr, ev);

				put_value(&c, &x);
				v_free(&x);
			}
			i += len;
			continue;
		}
		sb_putn(&c, b.p + i, 1);
		i++;
	}
	if ( c.p == NULL ) sb_put(&c, "");

	/* A1 */
	free(a.p);
	a.p = NULL;
	a.n = a.max = 0;
	for ( i = 0; c.p[i] != 0; ) {
		int		cc, len, nl;
		long long	rr;

		len = a1_at(c.p, i, &cc, &rr, 1, &nl);
		if ( len > 0 ) {
			if ( ev_has(ev, cc, (int)rr) ) {
				sb_put(&a, "#CIRCULAR!");
			} else {
				CV	x = value_recalc(cc, (int)rr, ev);

				put_value(&a, &x);
				v_free(&x);
			}
			i += len;
			continue;
		}
		sb_putn(&a, c.p + i, 1);
		i++;
	}
	if ( a.p == NULL ) sb_put(&a, "");
	free(b.p);
	free(c.p);

	if ( strstr(a.p, "#CIRCULAR!") != NULL ) {
		free(a.p);
		return v_str("#CIRCULAR!");
	}
	if ( has_dangerous(a.p) ) {
		free(a.p);
		return v_str("#ERROR!");
	}
	memset(&p, 0, sizeof(p));
	p.s = a.p;
	v = cond(&p, 1);
	skip(&p);
	if ( p.bad || p.thrown || p.s[p.at] != 0 ) {
		v_free(&v);
		v = v_str("#ERROR!");
	}
	free(a.p);
	return v;
}

/* ---------------------------------------------------------------- the calls */

/* Whether a formula names (col, row): as A1 between the edges of words, or as [row,col] */
static int names( const char *f, int col, int row )
{
	char	ref[24], old[40];
	int	n, i, k;

	n = col_letters(col, ref);
	snprintf(ref + n, sizeof(ref) - (size_t)n, "%d", row);
	n = (int)strlen(ref);
	for ( i = 0; f[i] != 0; i++ ) {
		if ( strncmp(f + i, ref, (size_t)n) == 0 && ( i == 0 || !word(f[i - 1]) ) && !word(f[i + n]) ) return 1;
	}
	snprintf(old, sizeof(old), "[%d,%d]", row, col);
	if ( strstr(f, old) != NULL ) return 1;
	snprintf(old, sizeof(old), "\xEF\xBC\xBB%d\xEF\xBC\x8C%d\xEF\xBC\xBD", row, col);
	k = ( strstr(f, old) != NULL );
	return k;
}

static void recalc_dependents( int col, int row )
{
	int	i;

	for ( i = 0; i < ncell; i++ ) {
		int	c = cells[i].col, r = cells[i].row;

		if ( cells[i].formula[0] == 0 || ( c == col && r == row ) ) continue;
		if ( names(cells[i].formula, col, row) ) {
			EVSET	ev;
			int	k[2];
			char	*f = strdup(cells[i].formula);
			CV	v;

			k[0] = c;
			k[1] = r;
			ev.n = 1;
			ev.k = k;
			v = evaluate(f, c, r, &ev);
			free(f);
			{
				int	j = find_cell(c, r);

				if ( j >= 0 ) {
					v_free(&cells[j].shown);
					cells[j].shown = v;
				} else {
					v_free(&v);
				}
			}
			touch(c, r);
			recalc_dependents(c, r);
		}
	}
}

int ce_start( void )
{
	return 0;
}

void ce_end( void )
{
	(void)ce_reset();
	free(changed);
	changed = NULL;
	nchg = mchg = 0;
}

const char *ce_name( void )
{
	return "C";
}

int ce_set( int col, int row, const unsigned char *text )
{
	int	i = make_cell(col, row);
	char	*t = strdup((const char *)text);

	if ( i < 0 || t == NULL ) {
		free(t);
		return -1;
	}
	free(cells[i].value);
	free(cells[i].formula);
	v_free(&cells[i].shown);
	if ( t[0] == '=' ) {
		EVSET	ev;
		int	k[2] = { col, row };
		CV	v;

		cells[i].formula = t;
		cells[i].value = strdup("");
		cells[i].shown = v_str("");
		ev.n = 1;
		ev.k = k;
		v = evaluate(t, col, row, &ev);
		i = find_cell(col, row);
		if ( i >= 0 ) {
			v_free(&cells[i].shown);
			cells[i].shown = v;
		}
	} else {
		cells[i].value = t;
		cells[i].formula = strdup("");
		cells[i].shown = v_str(t);
	}
	touch(col, row);
	recalc_dependents(col, row);
	return 0;
}

static void drop_at( int i )
{
	free(cells[i].value);
	free(cells[i].formula);
	v_free(&cells[i].shown);
	memmove(&cells[i], &cells[i + 1], sizeof(CCELL) * (size_t)( ncell - i - 1 ));
	ncell--;
}

int ce_clear( int col, int row )
{
	int	i = find_cell(col, row);

	if ( i >= 0 ) drop_at(i);
	touch(col, row);
	return 0;
}

int ce_blank( int col, int row )
{
	int	i = make_cell(col, row);

	if ( i < 0 ) return -1;
	free(cells[i].value);
	free(cells[i].formula);
	v_free(&cells[i].shown);
	cells[i].value = strdup("");
	cells[i].formula = strdup("");
	cells[i].shown = v_str("");
	touch(col, row);
	return 0;
}

int ce_move( int cols, int at, int d )
{
	int	i;

	for ( i = 0; i < ncell; ) {
		int	*v = cols ? &cells[i].col : &cells[i].row;

		if ( d < 0 && *v == at ) {
			drop_at(i);
			continue;
		}
		if ( *v > at || ( d > 0 && *v == at ) ) *v += d;
		i++;
	}
	return 0;
}

int ce_reset( void )
{
	while ( ncell > 0 ) drop_at(ncell - 1);
	nchg = 0;
	return 0;
}

int ce_taken( CE_FN fn, void *arg )
{
	int	i, n = nchg;

	for ( i = 0; i < nchg; i++ ) {
		int	c = changed[2 * i], r = changed[2 * i + 1], k = find_cell(c, r);
		char	*w = NULL;

		/* the words shown: the value, or the formula's result, when either is anything */
		if ( k >= 0 ) {
			if ( v_true(&cells[k].shown) ) w = v_words(&cells[k].shown);
			else if ( cells[k].value[0] != 0 ) w = strdup(cells[k].value);
		}
		fn(c, r, (const unsigned char *)( w != NULL ? w : "" ), w != NULL ? (int)strlen(w) : 0, arg);
		free(w);
	}
	nchg = 0;
	return n;
}

int ce_test( const unsigned char *text, const unsigned char *pat, int regex )
{
	(void)regex;
	return strstr((const char *)text, (const char *)pat) != NULL;
}

int ce_replace( const unsigned char *text, const unsigned char *pat, const unsigned char *rep, int regex,
		unsigned char *out, int max )
{
	const char	*s = (const char *)text, *f;
	size_t		lp = strlen((const char *)pat), lr = strlen((const char *)rep);
	int		k = 0;

	(void)regex;
	if ( max <= 0 ) return -1;
	while ( lp > 0 && ( f = strstr(s, (const char *)pat) ) != NULL ) {
		size_t	m = (size_t)( f - s );

		if ( k + (int)( m + lr ) >= max ) break;
		memcpy(out + k, s, m);
		k += (int)m;
		memcpy(out + k, rep, lr);
		k += (int)lr;
		s = f + lp;
	}
	while ( *s != 0 && k < max - 1 ) out[k++] = (unsigned char)*s++;
	out[k] = 0;
	return k;
}
