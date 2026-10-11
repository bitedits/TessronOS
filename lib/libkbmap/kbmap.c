/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	kbmap.c
 *	Keyboards and what their keys type: reading the layouts (design 11.14)
 *
 *	The JSON is walked with lib/libjson. Numbers are read here, from
 *	their text, into whole numbers of the units kbmap.h gives places
 *	and angles in, so that 0.25 of a key is 250000 exactly; a turned key
 *	is placed with sines in fixed point.
 */

#include <ts/kbmap.h>
#include <ts/json.h>

#define Q30		( 1 << 30 )

/* ---------------------------------------------------------------- small things */

LOCAL void zero( void *p, INT n )
{
	UB	*b = (UB *)p;

	while ( n-- > 0 ) *b++ = 0;
}

LOCAL BOOL is_blank( UB c )
{
	return (BOOL)( c == ' ' || c == '\t' || c == '\n' || c == '\r' );
}

LOCAL BOOL same_str( CONST UB *a, CONST char *b )
{
	INT	i;

	for ( i = 0; b[i] != 0; i++ ) {
		if ( a[i] != (UB)b[i] ) return FALSE;
	}
	return (BOOL)( a[i] == 0 );
}

/* UTF-8 into code points, as many as fit before the 0 written after them */
LOCAL INT to_cp( CONST UB *s, UH *cp, INT max )
{
	INT	n = 0;

	while ( *s != 0 && n < max - 1 ) {
		UINT	c = *s++;

		if ( c >= 0xF0 ) {
			c = ( ( c & 0x07 ) << 18 ) | ( ( s[0] & 0x3F ) << 12 ) | ( ( s[1] & 0x3F ) << 6 ) | ( s[2] & 0x3F );
			s += 3;
			c = 0xFFFD;			/* beyond what a kana layout holds */
		} else if ( c >= 0xE0 ) {
			c = ( ( c & 0x0F ) << 12 ) | ( ( s[0] & 0x3F ) << 6 ) | ( s[1] & 0x3F );
			s += 2;
		} else if ( c >= 0xC0 ) {
			c = ( ( c & 0x1F ) << 6 ) | ( s[0] & 0x3F );
			s += 1;
		}
		cp[n++] = (UH)c;
	}
	cp[n] = 0;
	return n;
}

/*
 * The members of an object, one after another: start with *p_at 0; the
 * name's span (with its quotes) and the value's, FALSE after the last.
 */
LOCAL BOOL member( CONST T_JSON *obj, INT *p_at, T_JSON *name, T_JSON *val )
{
	CONST UB	*s = obj->s;
	INT		at = *p_at, end = obj->len;

	if ( obj->len < 2 || s[0] != '{' ) return FALSE;
	if ( at == 0 ) at = 1;
	while ( at < end && ( is_blank(s[at]) || s[at] == ',' ) ) at++;
	if ( at >= end || s[at] != '"' ) return FALSE;
	if ( js_span(s, end, at, name) < E_OK || js_type(name) != JS_STRING ) return FALSE;
	at = (INT)( name->s - s ) + name->len;
	while ( at < end && is_blank(s[at]) ) at++;
	if ( at >= end || s[at] != ':' ) return FALSE;
	if ( js_span(s, end, at + 1, val) < E_OK ) return FALSE;
	*p_at = (INT)( val->s - s ) + val->len;
	return TRUE;
}

/* Whether a value is what JavaScript takes as true */
LOCAL BOOL truthy( CONST T_JSON *v )
{
	switch ( js_type(v) ) {
	case JS_OBJECT:
	case JS_ARRAY:
	case JS_TRUE:
		return TRUE;
	case JS_STRING:
		return (BOOL)( v->len > 2 );
	case JS_NUMBER: {
		INT	i;

		for ( i = 0; i < v->len; i++ ) {
			UB	c = v->s[i];

			if ( c == 'e' || c == 'E' ) break;
			if ( c >= '1' && c <= '9' ) return TRUE;
		}
		return FALSE;
	}
	default:
		return FALSE;
	}
}

/* A member that is there and true */
LOCAL BOOL has( CONST T_JSON *obj, CONST char *name, T_JSON *val )
{
	return (BOOL)( js_get(obj, name, val) >= E_OK && truthy(val) );
}

/*
 * A number times 10^pow, rounded to the nearest whole (half away from
 * zero); E_PAR when it is not a number.
 */
LOCAL ER fixed( CONST T_JSON *v, INT pow, INT *p_out )
{
	CONST UB	*s = v->s;
	INT		i = 0, n = v->len, ex = 0, frac = 0;
	BOOL		neg = FALSE, round = FALSE;
	D		m = 0;
	INT		digits = 0;

	if ( js_type(v) != JS_NUMBER ) return E_PAR;
	if ( i < n && s[i] == '-' ) {
		neg = TRUE;
		i++;
	}
	for ( ; i < n && s[i] >= '0' && s[i] <= '9'; i++ ) {
		if ( digits < 17 ) {
			m = m * 10 + ( s[i] - '0' );
			if ( m != 0 ) digits++;
		} else {
			ex++;
		}
	}
	if ( i < n && s[i] == '.' ) {
		for ( i++; i < n && s[i] >= '0' && s[i] <= '9'; i++ ) {
			if ( digits < 17 ) {
				m = m * 10 + ( s[i] - '0' );
				if ( m != 0 ) digits++;
				frac++;
			}
		}
	}
	if ( i < n && ( s[i] == 'e' || s[i] == 'E' ) ) {
		BOOL	eneg = FALSE;
		INT	e = 0;

		i++;
		if ( i < n && ( s[i] == '+' || s[i] == '-' ) ) eneg = (BOOL)( s[i++] == '-' );
		for ( ; i < n && s[i] >= '0' && s[i] <= '9'; i++ ) {
			if ( e < 1000 ) e = e * 10 + ( s[i] - '0' );
		}
		ex += eneg ? -e : e;
	}
	ex += pow - frac;
	while ( ex > 0 && m < 1000000000000000LL ) {
		m *= 10;
		ex--;
	}
	while ( ex < 0 && m != 0 ) {
		round = (BOOL)( m % 10 >= 5 );
		m /= 10;
		ex++;
		if ( ex < 0 ) round = FALSE;
	}
	if ( round ) m++;
	if ( ex > 0 || m > 0x7FFFFFFF ) m = 0x7FFFFFFF;
	*p_out = neg ? -(INT)m : (INT)m;
	return E_OK;
}

/* The string of a member, unescaped; "" when it is not one */
LOCAL INT get_str( CONST T_JSON *obj, CONST char *name, UB *out, INT max )
{
	T_JSON	v;
	INT	n;

	out[0] = 0;
	if ( js_get(obj, name, &v) < E_OK || js_type(&v) != JS_STRING ) return 0;
	n = js_str(&v, out, max);
	if ( n < 0 ) {
		out[max - 1] = 0;
		n = max - 1;
	}
	return n;
}

/* A key's name as a value: its usage, 0 when it is none or not known */
LOCAL UINT usage_of( CONST T_JSON *v )
{
	UB	b[32];
	INT	n;

	if ( js_type(v) != JS_STRING ) return 0;
	n = js_str(v, b, sizeof(b));
	return ( n > 0 ) ? kb_usage(b, n) : 0;
}

/* A key's name as a member's name (its span with quotes) */
LOCAL UINT usage_of_name( CONST T_JSON *name )
{
	return usage_of(name);
}

LOCAL void set_add( KBSET *s, UINT u )
{
	if ( u > 0 ) KB_ADD(s, u);
}

LOCAL void set_read( CONST T_JSON *arr, KBSET *s )
{
	T_JSON	it;

	zero(s, sizeof(*s));
	if ( js_type(arr) != JS_ARRAY ) return;
	it.s = NULL;
	while ( js_next(arr, &it) ) set_add(s, usage_of(&it));
}

/* ---------------------------------------------------------------- what a layout is */

EXPORT INT kb_kind( CONST UB *json, INT len )
{
	T_JSON	root, v, meta;
	UB	f[32];

	if ( js_parse(json, len, &root) < E_OK || js_type(&root) != JS_OBJECT ) return KB_KIND_NONE;
	if ( has(&root, "layout", &v) ) return KB_KIND_KEYBOARD;
	if ( has(&root, "meta", &meta) && js_type(&meta) == JS_OBJECT
	  && get_str(&meta, "format", f, sizeof(f)) > 0 && same_str(f, "kle-extended") ) {
		return KB_KIND_KEYBOARD;
	}
	if ( has(&root, "keycodeTranslation", &v) ) return KB_KIND_TRANS;
	if ( has(&root, "mapping", &v) ) return KB_KIND_KANA;
	return KB_KIND_NONE;
}

EXPORT INT kb_meta_name( CONST UB *json, INT len, UB *out, INT max )
{
	T_JSON	root, meta;

	out[0] = 0;
	if ( js_parse(json, len, &root) < E_OK || js_type(&root) != JS_OBJECT ) return 0;
	if ( !has(&root, "meta", &meta) || js_type(&meta) != JS_OBJECT ) return 0;
	return get_str(&meta, "name", out, max);
}

/* ---------------------------------------------------------------- angles */

/* sin of x radians (|x| <= pi/2), both 2^30 being 1 */
LOCAL INT sin_q30( D x )
{
	D	x2 = ( x * x ) >> 30, term = x, sum = x;
	INT	k;

	for ( k = 1; k <= 7; k++ ) {
		term = ( term * x2 ) >> 30;
		term = -term / ( ( 2 * k ) * ( 2 * k + 1 ) );
		sum += term;
	}
	return (INT)sum;
}

EXPORT INT kb_sin( INT mdeg )
{
	BOOL	neg = FALSE;
	D	x;

	mdeg %= 360000;
	if ( mdeg < 0 ) mdeg += 360000;
	if ( mdeg >= 180000 ) {
		mdeg -= 180000;
		neg = TRUE;
	}
	if ( mdeg > 90000 ) mdeg = 180000 - mdeg;
	/* pi * 2^30 = 3373259426.1 */
	x = ( (D)mdeg * 3373259426LL + 90000 ) / 180000;
	x = sin_q30(x);
	if ( x > Q30 ) x = Q30;
	return (INT)( neg ? -x : x );
}

EXPORT INT kb_cos( INT mdeg )
{
	return kb_sin(mdeg + 90000);
}

/* A product with 2^30 being 1, rounded to the nearest whole */
LOCAL INT round_q30( D p )
{
	return (INT)( ( p >= 0 ) ? ( p + ( Q30 / 2 ) ) >> 30 : -( ( -p + ( Q30 / 2 ) ) >> 30 ) );
}

EXPORT UD kb_sqrt( UD n )
{
	UD	r = 0, bit = (UD)1 << 62, m = n;

	while ( bit > n ) bit >>= 2;
	while ( bit != 0 ) {
		if ( n >= r + bit ) {
			n -= r + bit;
			r = ( r >> 1 ) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return ( m - r * r > r ) ? r + 1 : r;		/* to the nearest */
}

/* ---------------------------------------------------------------- a keyboard */

/*
 * A row's labels are one string, the lines of what is printed on the
 * key: the second line is the key's own label and the first what a
 * shift types; a single line is the key's label.
 */
LOCAL void key_labels( CONST T_JSON *v, KBKEY *k )
{
	UB	b[128];
	INT	n, i, nl, end;

	n = js_str(v, b, sizeof(b));
	if ( n < 0 ) n = (INT)sizeof(b) - 1;
	b[n] = 0;
	for ( nl = 0; nl < n && b[nl] != '\n'; nl++ ) ;
	if ( nl < n ) {
		for ( end = nl + 1; end < n && b[end] != '\n'; end++ ) ;
		for ( i = 0; nl + 1 + i < end && i < KB_LABEL - 1; i++ ) k->label[i] = b[nl + 1 + i];
		k->label[i] = 0;
		for ( i = 0; i < nl && i < KB_LABEL - 1; i++ ) k->shift[i] = b[i];
		k->shift[i] = 0;
	} else {
		for ( i = 0; i < n && i < KB_LABEL - 1; i++ ) k->label[i] = b[i];
		k->label[i] = 0;
		k->shift[0] = 0;
	}
}

/* The keyboard's bounds, and every key moved so that its top left is 0, 0 */
LOCAL void bounds( KBLAYOUT *l )
{
	INT	i, minx = 0x7FFFFFFF, miny = 0x7FFFFFFF, maxx = -0x7FFFFFFF, maxy = -0x7FFFFFFF;

	if ( l->n == 0 ) {
		l->w = 0;
		l->h = 0;
		return;
	}
	for ( i = 0; i < l->n; i++ ) {
		KBKEY	*k = &l->k[i];
		INT	x0, y0, x1, y1;

		if ( k->r != 0 ) {
			INT	cx = k->x + k->w / 2, cy = k->y + k->h / 2, hw = k->w / 2, hh = k->h / 2;
			INT	a = ( k->r < 0 ) ? -k->r : k->r;
			INT	c = kb_cos(a), s = kb_sin(a);
			INT	rw = round_q30((D)hw * c + (D)hh * s);
			INT	rh = round_q30((D)hw * s + (D)hh * c);

			x0 = cx - rw;
			x1 = cx + rw;
			y0 = cy - rh;
			y1 = cy + rh;
		} else {
			x0 = k->x;
			y0 = k->y;
			x1 = k->x + k->w;
			y1 = k->y + k->h;
		}
		if ( x0 < minx ) minx = x0;
		if ( x1 > maxx ) maxx = x1;
		if ( y0 < miny ) miny = y0;
		if ( y1 > maxy ) maxy = y1;
	}
	for ( i = 0; i < l->n; i++ ) {
		l->k[i].x -= minx;
		l->k[i].y -= miny;
	}
	l->w = maxx - minx;
	l->h = maxy - miny;
}

EXPORT ER kb_read_keyboard( CONST UB *json, INT len, KBLAYOUT *l )
{
	T_JSON	root, rows, row, it, meta;
	INT	cx = 0, cy = 0, cr = 0, crx = 0, cry = 0;

	zero(l, sizeof(*l));
	if ( js_parse(json, len, &root) < E_OK ) return E_PAR;
	rows = root;
	if ( js_type(&root) == JS_OBJECT ) {
		if ( !has(&root, "layout", &rows) ) return E_PAR;
		if ( has(&root, "meta", &meta) && js_type(&meta) == JS_OBJECT ) {
			(void)get_str(&meta, "name", l->name, KB_NAME);
		}
	}
	if ( js_type(&rows) != JS_ARRAY ) return E_PAR;

	row.s = NULL;
	while ( js_next(&rows, &row) ) {
		INT	w = 0, h = 0;
		UINT	id = 0;
		BOOL	has_w = FALSE, has_h = FALSE;

		if ( js_type(&row) != JS_ARRAY ) continue;
		cx = crx;
		it.s = NULL;
		while ( js_next(&row, &it) ) {
			if ( js_type(&it) == JS_OBJECT ) {
				T_JSON	v, rx, ry;
				BOOL	hx = (BOOL)( js_get(&it, "rx", &rx) >= E_OK );
				BOOL	hy = (BOOL)( js_get(&it, "ry", &ry) >= E_OK );
				INT	n;

				if ( hx || hy ) {
					if ( hx && fixed(&rx, 6, &n) >= E_OK ) crx = n;
					if ( hy && fixed(&ry, 6, &n) >= E_OK ) cry = n;
					cx = crx;
					cy = cry;
				}
				if ( js_get(&it, "r", &v) >= E_OK && fixed(&v, 3, &n) >= E_OK ) cr = n;
				if ( js_get(&it, "x", &v) >= E_OK && fixed(&v, 6, &n) >= E_OK ) cx += n;
				if ( js_get(&it, "y", &v) >= E_OK && fixed(&v, 6, &n) >= E_OK ) cy += n;
				if ( js_get(&it, "w", &v) >= E_OK && fixed(&v, 6, &n) >= E_OK ) {
					w = n;
					has_w = TRUE;
				}
				if ( js_get(&it, "h", &v) >= E_OK && fixed(&v, 6, &n) >= E_OK ) {
					h = n;
					has_h = TRUE;
				}
				if ( js_get(&it, "id", &v) >= E_OK ) id = usage_of(&v);
			} else if ( js_type(&it) == JS_STRING ) {
				KBKEY	*k;
				INT	kw = ( has_w && w != 0 ) ? w : KB_UNIT;
				INT	kh = ( has_h && h != 0 ) ? h : KB_UNIT;

				if ( l->n < KB_KEY_MAX ) {
					k = &l->k[l->n++];
					k->usage = (UH)id;
					k->w = kw;
					k->h = kh;
					k->r = cr;
					key_labels(&it, k);
					if ( cr != 0 ) {
						INT	lcx = cx + kw / 2, lcy = cy + kh / 2;
						INT	dx = lcx - crx, dy = lcy - cry;
						INT	c = kb_cos(cr), s = kb_sin(cr);

						k->x = crx + round_q30((D)dx * c - (D)dy * s) - kw / 2;
						k->y = cry + round_q30((D)dx * s + (D)dy * c) - kh / 2;
					} else {
						k->x = cx;
						k->y = cy;
					}
				}
				cx += kw;
				w = h = 0;
				has_w = has_h = FALSE;
				id = 0;
			}
		}
		cy += KB_UNIT;
	}
	bounds(l);
	return E_OK;
}

EXPORT BOOL kb_key_middle( CONST KBLAYOUT *l, UINT usage, INT *p_x, INT *p_y )
{
	INT	i;

	for ( i = 0; i < l->n; i++ ) {
		if ( l->k[i].usage == usage && usage != 0 ) {
			*p_x = l->k[i].x + l->k[i].w / 2;
			*p_y = l->k[i].y + l->k[i].h / 2;
			return TRUE;
		}
	}
	return FALSE;
}

/* ---------------------------------------------------------------- a kana layout */

LOCAL void plane_read( CONST T_JSON *e, CONST char *name, UH *cp )
{
	UB	b[64];

	cp[0] = 0;
	if ( get_str(e, name, b, sizeof(b)) > 0 ) (void)to_cp(b, cp, KB_CP);
}

EXPORT ER kb_read_kana( CONST UB *json, INT len, KBKANAMAP *m )
{
	static CONST char *CONST fingers[KB_FINGERS] = {
		"left_pinky", "left_ring", "left_middle", "left_index", "left_thumb",
		"right_index", "right_middle", "right_ring", "right_pinky", "right_thumb"
	};
	T_JSON	root, map, name, e, v, meta;
	INT	at = 0, i;

	zero(m, sizeof(*m));
	if ( js_parse(json, len, &root) < E_OK || js_type(&root) != JS_OBJECT ) return E_PAR;
	if ( has(&root, "meta", &meta) && js_type(&meta) == JS_OBJECT ) {
		(void)get_str(&meta, "name", m->name, KB_NAME);
	}
	map = root;
	if ( has(&root, "mapping", &v) ) map = v;
	if ( js_type(&map) != JS_OBJECT ) return E_PAR;

	while ( member(&map, &at, &name, &e) ) {
		UINT	u;
		KBKANA	*k = NULL;

		if ( name.len > 1 && name.s[1] == '_' ) continue;
		u = usage_of_name(&name);
		if ( u == 0 || js_type(&e) != JS_OBJECT ) continue;
		for ( i = 0; i < m->n; i++ ) {
			if ( m->k[i].usage == u ) k = &m->k[i];	/* named twice: the last is what it types */
		}
		if ( k == NULL ) {
			if ( m->n >= KB_MAP_MAX ) continue;
			k = &m->k[m->n++];
		}
		k->usage = (UH)u;
		plane_read(&e, "normal", k->cp[KB_PLAIN]);
		plane_read(&e, "leftShift", k->cp[KB_LEFT]);
		plane_read(&e, "rightShift", k->cp[KB_RIGHT]);
	}

	if ( js_get(&root, "combos", &v) >= E_OK && js_type(&v) == JS_ARRAY ) {
		T_JSON	c;

		c.s = NULL;
		while ( js_next(&v, &c) && m->nc < KB_COMBO_MAX ) {
			T_JSON	keys, out, kk;
			KBCOMBO	*cb = &m->c[m->nc];
			UB	b[64];

			if ( js_type(&c) != JS_OBJECT || !has(&c, "keys", &keys) || !has(&c, "output", &out) ) continue;
			if ( js_type(&keys) != JS_ARRAY || js_type(&out) != JS_STRING ) continue;
			cb->nkey = 0;
			kk.s = NULL;
			while ( js_next(&keys, &kk) && cb->nkey < KB_COMBO_KEYS ) {
				cb->keys[cb->nkey++] = (UH)usage_of(&kk);
			}
			if ( js_str(&out, b, sizeof(b)) <= 0 ) continue;
			(void)to_cp(b, cb->cp, KB_CP);
			m->nc++;
		}
	}

	if ( has(&root, "handAssignment", &v) && js_type(&v) == JS_OBJECT ) {
		T_JSON	side;

		m->has_hand = TRUE;
		if ( js_get(&v, "left", &side) >= E_OK ) set_read(&side, &m->hand[0]);
		if ( js_get(&v, "right", &side) >= E_OK ) set_read(&side, &m->hand[1]);
	}
	if ( has(&root, "fingerAssignment", &v) && js_type(&v) == JS_OBJECT ) {
		T_JSON	f;

		m->has_finger = TRUE;
		for ( i = 0; i < KB_FINGERS; i++ ) {
			if ( js_get(&v, fingers[i], &f) >= E_OK ) set_read(&f, &m->finger[i]);
		}
	}
	return E_OK;
}

EXPORT CONST UH *kb_kana_of( CONST KBKANAMAP *m, UINT usage, INT plane )
{
	INT	i;

	if ( plane < KB_PLAIN || plane > KB_RIGHT ) return NULL;
	for ( i = 0; i < m->n; i++ ) {
		if ( m->k[i].usage == usage ) {
			return ( m->k[i].cp[plane][0] != 0 ) ? m->k[i].cp[plane] : NULL;
		}
	}
	return NULL;
}

EXPORT INT kb_hand( CONST KBKANAMAP *m, UINT usage )
{
	if ( !m->has_hand ) return 0;
	if ( KB_IN(&m->hand[0], usage) ) return 1;
	if ( KB_IN(&m->hand[1], usage) ) return 2;
	return 0;
}

/* ---------------------------------------------------------------- a translation */

EXPORT ER kb_read_trans( CONST UB *json, INT len, KBTRANS *t )
{
	T_JSON	root, map, name, v, meta;
	INT	at = 0;

	zero(t, sizeof(*t));
	if ( js_parse(json, len, &root) < E_OK || js_type(&root) != JS_OBJECT ) return E_PAR;
	if ( has(&root, "meta", &meta) && js_type(&meta) == JS_OBJECT ) {
		(void)get_str(&meta, "name", t->name, KB_NAME);
	}
	if ( !has(&root, "keycodeTranslation", &map) || js_type(&map) != JS_OBJECT ) return E_PAR;
	while ( member(&map, &at, &name, &v) && t->n < KB_TRANS_MAX ) {
		UINT	f = usage_of_name(&name), to = usage_of(&v);
		INT	i;

		if ( f == 0 || to == 0 ) continue;
		for ( i = 0; i < t->n && t->from[i] != f; i++ ) ;
		t->from[i] = (UH)f;
		t->to[i] = (UH)to;
		if ( i == t->n ) t->n++;
	}
	return E_OK;
}

EXPORT UINT kb_trans( CONST KBTRANS *t, UINT usage )
{
	INT	i;

	if ( t == NULL ) return usage;
	for ( i = 0; i < t->n; i++ ) {
		if ( t->from[i] == usage ) return t->to[i];
	}
	return usage;
}

/* ---------------------------------------------------------------- JSON as JSON.stringify writes it */

typedef struct {
	UB	*p;
	INT	n, max;
	BOOL	full;
} OUT;

LOCAL void put( OUT *o, UB c )
{
	if ( o->n < o->max ) o->p[o->n++] = c;
	else o->full = TRUE;
}

LOCAL void puts_( OUT *o, CONST char *s )
{
	while ( *s != 0 ) put(o, (UB)*s++);
}

LOCAL INT hexval( UB c )
{
	if ( c >= '0' && c <= '9' ) return c - '0';
	if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

LOCAL void put_utf8( OUT *o, UINT c )
{
	if ( c < 0x80 ) {
		put(o, (UB)c);
	} else if ( c < 0x800 ) {
		put(o, (UB)( 0xC0 | ( c >> 6 ) ));
		put(o, (UB)( 0x80 | ( c & 0x3F ) ));
	} else if ( c < 0x10000 ) {
		put(o, (UB)( 0xE0 | ( c >> 12 ) ));
		put(o, (UB)( 0x80 | ( ( c >> 6 ) & 0x3F ) ));
		put(o, (UB)( 0x80 | ( c & 0x3F ) ));
	} else {
		put(o, (UB)( 0xF0 | ( c >> 18 ) ));
		put(o, (UB)( 0x80 | ( ( c >> 12 ) & 0x3F ) ));
		put(o, (UB)( 0x80 | ( ( c >> 6 ) & 0x3F ) ));
		put(o, (UB)( 0x80 | ( c & 0x3F ) ));
	}
}

/* A character as a string escapes it: the short escapes, \u00xx below a space */
LOCAL void put_char( OUT *o, UINT c )
{
	static CONST char	hex[] = "0123456789abcdef";

	switch ( c ) {
	case '"':	puts_(o, "\\\"");	return;
	case '\\':	puts_(o, "\\\\");	return;
	case '\b':	puts_(o, "\\b");	return;
	case '\f':	puts_(o, "\\f");	return;
	case '\n':	puts_(o, "\\n");	return;
	case '\r':	puts_(o, "\\r");	return;
	case '\t':	puts_(o, "\\t");	return;
	default:	break;
	}
	if ( c < 0x20 || ( c >= 0xD800 && c <= 0xDFFF ) ) {
		puts_(o, "\\u");
		put(o, (UB)hex[( c >> 12 ) & 15]);
		put(o, (UB)hex[( c >> 8 ) & 15]);
		put(o, (UB)hex[( c >> 4 ) & 15]);
		put(o, (UB)hex[c & 15]);
		return;
	}
	put_utf8(o, c);
}

/* A string span (with its quotes), unescaped and escaped again */
LOCAL void put_string( OUT *o, CONST UB *s, INT n )
{
	INT	i = 1;

	put(o, '"');
	while ( i < n - 1 ) {
		UINT	c = s[i];

		if ( c == '\\' && i + 1 < n - 1 ) {
			UB	e = s[i + 1];

			i += 2;
			switch ( e ) {
			case 'b':	c = '\b';	break;
			case 'f':	c = '\f';	break;
			case 'n':	c = '\n';	break;
			case 'r':	c = '\r';	break;
			case 't':	c = '\t';	break;
			case 'u': {
				INT	k, v = 0;

				for ( k = 0; k < 4 && i + k < n - 1; k++ ) v = v * 16 + hexval(s[i + k]);
				i += 4;
				c = (UINT)v;
				/* a pair of surrogates is one character */
				if ( c >= 0xD800 && c <= 0xDBFF && i + 5 < n - 1 && s[i] == '\\' && s[i + 1] == 'u' ) {
					INT	lo = 0;

					for ( k = 0; k < 4; k++ ) lo = lo * 16 + hexval(s[i + 2 + k]);
					if ( lo >= 0xDC00 && lo <= 0xDFFF ) {
						c = 0x10000 + ( ( c - 0xD800 ) << 10 ) + ( (UINT)lo - 0xDC00 );
						i += 6;
					}
				}
				break;
			}
			default:	c = e;		break;	/* \" \\ \/ */
			}
			put_char(o, c);
			continue;
		}
		if ( c < 0x80 ) {
			put_char(o, c);
			i++;
		} else {
			put(o, (UB)c);			/* UTF-8 goes as it is */
			i++;
		}
	}
	put(o, '"');
}

/*
 * A number as JavaScript prints it: its digits without the zeros that
 * end them, the point where it falls, an exponent beyond 21 digits or
 * smaller than a millionth.
 */
LOCAL void put_number( OUT *o, CONST UB *s, INT n )
{
	char	dg[40];
	INT	nd = 0, i = 0, point = 0, ex = 0, k;
	BOOL	neg = FALSE, seen = FALSE;

	if ( i < n && s[i] == '-' ) {
		neg = TRUE;
		i++;
	}
	for ( ; i < n && s[i] >= '0' && s[i] <= '9'; i++ ) {
		if ( s[i] != '0' || seen ) {
			seen = TRUE;
			if ( nd < 32 ) dg[nd++] = (char)s[i];
			point++;
		}
	}
	if ( i < n && s[i] == '.' ) {
		for ( i++; i < n && s[i] >= '0' && s[i] <= '9'; i++ ) {
			if ( s[i] != '0' || seen ) {
				seen = TRUE;
				if ( nd < 32 ) dg[nd++] = (char)s[i];
			} else {
				point--;			/* a zero before the first digit */
			}
		}
	}
	if ( i < n && ( s[i] == 'e' || s[i] == 'E' ) ) {
		BOOL	eneg = FALSE;
		INT	e = 0;

		i++;
		if ( i < n && ( s[i] == '+' || s[i] == '-' ) ) eneg = (BOOL)( s[i++] == '-' );
		for ( ; i < n && s[i] >= '0' && s[i] <= '9'; i++ ) {
			if ( e < 10000 ) e = e * 10 + ( s[i] - '0' );
		}
		ex = eneg ? -e : e;
	}
	if ( nd > 17 ) {
		/* no more digits than a double keeps */
		BOOL	up = (BOOL)( dg[17] >= '5' );

		nd = 17;
		for ( k = nd - 1; up && k >= 0; k-- ) {
			if ( dg[k] == '9' ) {
				dg[k] = '0';
			} else {
				dg[k]++;
				up = FALSE;
			}
		}
		if ( up ) {
			for ( k = nd; k > 0; k-- ) dg[k] = dg[k - 1];
			dg[0] = '1';
			point++;
		}
	}
	while ( nd > 0 && dg[nd - 1] == '0' ) nd--;
	if ( nd == 0 ) {
		put(o, '0');				/* -0 too */
		return;
	}
	point += ex;					/* the digits are 0.d1d2.. times 10^point */
	if ( neg ) put(o, '-');
	if ( nd <= point && point <= 21 ) {
		for ( k = 0; k < nd; k++ ) put(o, (UB)dg[k]);
		for ( k = nd; k < point; k++ ) put(o, '0');
	} else if ( point > 0 && point <= 21 ) {
		for ( k = 0; k < point; k++ ) put(o, (UB)dg[k]);
		put(o, '.');
		for ( ; k < nd; k++ ) put(o, (UB)dg[k]);
	} else if ( point > -6 && point <= 0 ) {
		puts_(o, "0.");
		for ( k = point; k < 0; k++ ) put(o, '0');
		for ( k = 0; k < nd; k++ ) put(o, (UB)dg[k]);
	} else {
		char	eb[12];
		INT	e = point - 1, m = 0;

		put(o, (UB)dg[0]);
		if ( nd > 1 ) {
			put(o, '.');
			for ( k = 1; k < nd; k++ ) put(o, (UB)dg[k]);
		}
		put(o, 'e');
		put(o, ( e < 0 ) ? '-' : '+');
		if ( e < 0 ) e = -e;
		do {
			eb[m++] = (char)( '0' + e % 10 );
			e /= 10;
		} while ( e > 0 );
		while ( m > 0 ) put(o, (UB)eb[--m]);
	}
}

LOCAL void put_value( OUT *o, CONST T_JSON *v )
{
	T_JSON	it, name;
	INT	at = 0;
	BOOL	first = TRUE;

	switch ( js_type(v) ) {
	case JS_OBJECT:
		put(o, '{');
		while ( member(v, &at, &name, &it) ) {
			if ( !first ) put(o, ',');
			first = FALSE;
			put_string(o, name.s, name.len);
			put(o, ':');
			put_value(o, &it);
		}
		put(o, '}');
		break;
	case JS_ARRAY:
		put(o, '[');
		it.s = NULL;
		while ( js_next(v, &it) ) {
			if ( !first ) put(o, ',');
			first = FALSE;
			put_value(o, &it);
		}
		put(o, ']');
		break;
	case JS_STRING:
		put_string(o, v->s, v->len);
		break;
	case JS_NUMBER:
		put_number(o, v->s, v->len);
		break;
	case JS_TRUE:
		puts_(o, "true");
		break;
	case JS_FALSE:
		puts_(o, "false");
		break;
	default:
		puts_(o, "null");
		break;
	}
}

EXPORT INT kb_json_compact( CONST UB *json, INT len, UB *out, INT max )
{
	T_JSON	root;
	OUT	o;

	if ( js_parse(json, len, &root) < E_OK ) return E_PAR;
	o.p = out;
	o.n = 0;
	o.max = max;
	o.full = FALSE;
	put_value(&o, &root);
	return o.full ? E_LIMIT : o.n;
}
