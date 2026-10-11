/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	calc_chart.c
 *	基本表計算: the charts (design 11.13)
 *
 *	A chart is made from a range of cells: its first column the labels,
 *	each column after it a series of numbers. It is a figure, made
 *	again from the cells every time it is drawn or written: a white
 *	ground, the bars (side by side, or stacked) or the lines with a dot
 *	at each value (or the lines of the sums), and the labels under them.
 *	Each colour is a pattern of the figure's own, numbered from 128 in
 *	the order the colours are first wanted, the ground's first; the
 *	shapes are numbered down from 100 in the order they are drawn. The
 *	record keeps the figure in a <group> with the chart's kind and
 *	cells, so that a program that does not know charts still shows it.
 */

#include "calc.h"
#include <math.h>

#define PAD		30

static CONST char *CONST colours[6] = { "#51aefb", "#ff7f50", "#90ee90", "#dda0dd", "#f0e68c", "#87ceeb" };
static CONST char *CONST kinds[CX_CH_KINDS] = { "clustered-bar", "stacked-bar", "line", "stacked-line" };

#define SERIES_MAX	64
#define LABEL_MAX	512

typedef struct {
	INT	nl, ns;
	UB	*label[LABEL_MAX];
	double	*v[SERIES_MAX];		/* v[series][label] */
} DATA;

/* Math.round */
static INT rnd( double x )
{
	return (INT)floor(x + 0.5);
}

/* parseFloat of the words a cell shows: the number they begin with, 0 for none */
double cell_number( CONST CXSHEET *s, INT col, INT row )
{
	CONST CXCELL	*c = cx_get(s, col, row);
	CONST char	*w, *p;
	char		*end;
	double		x;

	if ( c == NULL ) return 0;
	w = (CONST char *)cx_shown(c);
	p = w;
	while ( *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ) p++;
	if ( strncmp(p, "Infinity", 8) == 0 ) return INFINITY;
	if ( strncmp(p, "-Infinity", 9) == 0 ) return -INFINITY;
	if ( !( ( *p >= '0' && *p <= '9' ) || *p == '.' || *p == '-' || *p == '+' ) ) return 0;
	if ( ( p[0] == '0' && ( p[1] == 'x' || p[1] == 'X' ) ) ) return 0;
	x = strtod(p, &end);
	if ( end == p || isnan(x) ) return 0;
	return x;
}

static void data_free( DATA *d )
{
	INT	i;

	for ( i = 0; i < d->nl; i++ ) free(d->label[i]);
	for ( i = 0; i < d->ns; i++ ) free(d->v[i]);
	d->nl = d->ns = 0;
}

/* The labels and series of a range */
static void data_of( CONST CXSHEET *s, CONST CXRANGE *r, DATA *d )
{
	INT	row, col, k;

	memset(d, 0, sizeof(*d));
	for ( row = r->r0; row <= r->r1 && d->nl < LABEL_MAX; row++ ) {
		CONST CXCELL	*c = cx_get(s, r->c0, row);

		d->label[d->nl++] = (UB *)strdup(( c != NULL ) ? (CONST char *)cx_shown(c) : "");
	}
	for ( col = r->c0 + 1; col <= r->c1 && d->ns < SERIES_MAX; col++ ) {
		double	*v = calloc((size_t)( d->nl + 1 ), sizeof(double));

		if ( v == NULL ) break;
		for ( k = 0; k < d->nl; k++ ) {
			double	x = cell_number(s, col, r->r0 + k);

			v[k] = isnan(x) ? 0 : x;
		}
		d->v[d->ns++] = v;
	}
}

/* The patterns: the ground's colour, then each series' */
static INT pattern_of( CONST char *c, CONST char **used, INT n )
{
	INT	i;

	for ( i = 0; i < n; i++ ) {
		if ( strcmp(used[i], c) == 0 ) return 128 + i;
	}
	return 0;
}

static void put_num( CTBUF *b, INT v )
{
	ct_num(b, v);
}

static void attr( CTBUF *b, CONST char *name, INT v )
{
	ct_put(b, " ");
	ct_put(b, name);
	ct_put(b, "=\"");
	put_num(b, v);
	ct_put(b, "\"");
}

/* A label under the chart: a text of one line, centred */
static void label( CTBUF *b, INT left, INT top, INT width, INT z, CONST UB *w, INT ox, INT oy )
{
	INT	right = left + rnd(width), bottom = top + 15;

	ct_put(b, "<document>\n<docView viewleft=\"");
	put_num(b, left + ox);
	ct_put(b, "\" viewtop=\"");
	put_num(b, top + oy);
	ct_put(b, "\" viewright=\"");
	put_num(b, right + ox);
	ct_put(b, "\" viewbottom=\"");
	put_num(b, bottom + oy);
	ct_put(b, "\"/>\r\n<docDraw drawleft=\"");
	put_num(b, left + ox);
	ct_put(b, "\" drawtop=\"");
	put_num(b, top + oy);
	ct_put(b, "\" drawright=\"");
	put_num(b, right + ox);
	ct_put(b, "\" drawbottom=\"");
	put_num(b, bottom + oy);
	ct_put(b, "\"/>\r\n<docScale hunit=\"-72\" vunit=\"-72\"/>\r\n<text lang=\"0\" bpat=\"0\" zIndex=\"");
	put_num(b, z);
	ct_put(b, "\"/>\r\n<font size=\"10\"/>\n<font face=\"sans-serif\"/>\n<font color=\"#000000\"/>\n<text align=\"center\"/>\n");
	cx_esc(b, w, (INT)strlen((CONST char *)w));
	ct_put(b, "\n</document>\n");
}

static void bar( CTBUF *b, double x, double y, double w, double h, INT pat, INT z, INT ox, INT oy )
{
	ct_put(b, "<rect round=\"0\" lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"");
	put_num(b, pat);
	ct_put(b, "\" angle=\"0\"");
	attr(b, "left", rnd(x) + ox);
	attr(b, "top", rnd(y) + oy);
	attr(b, "right", rnd(x + w) + ox);
	attr(b, "bottom", rnd(y + h) + oy);
	attr(b, "zIndex", z);
	ct_put(b, "/>\n");
}

/*
 * The shapes of a chart of width by height, the ground's pattern
 * first; a point (x, y) of it written as (ox + x, oy + y).
 */
static void shapes( CTBUF *b, INT kind, INT width, INT height, CONST DATA *d, INT ox, INT oy )
{
	CONST char	*used[8];
	INT		nused = 0, i, li, si, z = 100;
	double		aw = width - PAD * 2, ah = height - PAD * 2, max = 0;

	used[nused++] = "#ffffff";
	for ( i = 0; i < d->ns; i++ ) {
		if ( pattern_of(colours[i % 6], used, nused) == 0 && nused < 8 ) used[nused++] = colours[i % 6];
	}
	ct_put(b, "<patterns>\n");
	for ( i = 0; i < nused; i++ ) {
		ct_put(b, "<pattern id=\"");
		put_num(b, 128 + i);
		ct_put(b, "\" type=\"0\" width=\"4\" height=\"4\" ncol=\"1\" fgcolors=\"");
		ct_put(b, used[i]);
		ct_put(b, "\" bgcolor=\"transparent\" masks=\"7\" />\n");
	}
	ct_put(b, "</patterns>\n");
	ct_put(b, "<rect round=\"0\" lineType=\"0\" lineWidth=\"1\" l_pat=\"0\" f_pat=\"128\" angle=\"0\" ");
	ct_put(b, "left=\"");
	put_num(b, ox);
	ct_put(b, "\" top=\"");
	put_num(b, oy);
	ct_put(b, "\" right=\"");
	put_num(b, width + ox);
	ct_put(b, "\" bottom=\"");
	put_num(b, height + oy);
	ct_put(b, "\" zIndex=\"0\"/>\n");
	if ( d->nl == 0 ) return;

	/* the largest value, or the largest sum */
	for ( li = 0; li < d->nl; li++ ) {
		double	t = 0;

		for ( si = 0; si < d->ns; si++ ) {
			if ( kind == CX_CH_SBAR || kind == CX_CH_SLINE ) t += d->v[si][li];
			else if ( d->v[si][li] > max ) max = d->v[si][li];
		}
		if ( ( kind == CX_CH_SBAR || kind == CX_CH_SLINE ) && t > max ) max = t;
	}
	if ( max == 0 ) max = 1;

	if ( kind == CX_CH_BAR || kind == CX_CH_SBAR ) {
		double	gw = aw / d->nl;

		for ( li = 0; li < d->nl; li++ ) {
			double	y = PAD + ah;

			for ( si = 0; si < d->ns; si++ ) {
				INT	pat = pattern_of(colours[si % 6], used, nused);
				double	v = d->v[si][li];

				if ( kind == CX_CH_BAR ) {
					double	bw = gw * 0.8 / d->ns, h = v / max * ah;
					double	x = PAD + gw * li + gw * 0.1 + bw * si;

					bar(b, x, PAD + ah - h, bw, h, pat, z--, ox, oy);
				} else {
					double	bw = gw * 0.6, h = v / max * ah, x = PAD + gw * li + ( gw - bw ) / 2;

					y -= h;
					bar(b, x, y, bw, h, pat, z--, ox, oy);
				}
			}
		}
		for ( li = 0; li < d->nl; li++ ) {
			double	lx = PAD + gw * li + gw / 2;

			label(b, rnd(lx - gw / 2), rnd(PAD + ah + 5), rnd(gw), z--, d->label[li], ox, oy);
		}
		return;
	}

	/* lines */
	{
		double	sp = aw / ( d->nl > 1 ? d->nl - 1 : 1 ), *sum = calloc((size_t)( d->nl > 0 ? d->nl : 0 ) + 1, sizeof(double));

		for ( si = 0; si < d->ns && sum != NULL; si++ ) {
			INT	pat = pattern_of(colours[si % 6], used, nused);
			INT	*px = malloc(sizeof(INT) * 2 * (size_t)d->nl);

			if ( px == NULL ) break;
			for ( li = 0; li < d->nl; li++ ) {
				double	v = d->v[si][li], x;

				if ( kind == CX_CH_SLINE ) {
					sum[li] += v;
					v = sum[li];
				}
				x = PAD + ( d->nl > 1 ? sp * li : aw / 2 );
				px[2 * li] = rnd(x);
				px[2 * li + 1] = rnd(PAD + ah - v / max * ah);
			}
			if ( d->nl > 1 ) {
				ct_put(b, "<polyline lineType=\"0\" lineWidth=\"2\" l_pat=\"");
				put_num(b, pat);
				ct_put(b, "\" f_pat=\"0\" points=\"");
				for ( li = 0; li < d->nl; li++ ) {
					if ( li > 0 ) ct_put(b, " ");
					put_num(b, px[2 * li] + ox);
					ct_put(b, ",");
					put_num(b, px[2 * li + 1] + oy);
				}
				ct_put(b, "\"");
				attr(b, "zIndex", z--);
				ct_put(b, "/>\n");
			}
			for ( li = 0; li < d->nl; li++ ) {
				ct_put(b, "<ellipse lineType=\"0\" lineWidth=\"0\" l_pat=\"0\" f_pat=\"");
				put_num(b, pat);
				ct_put(b, "\" angle=\"0\"");
				attr(b, "frameLeft", px[2 * li] - 4 + ox);
				attr(b, "frameTop", px[2 * li + 1] - 4 + oy);
				attr(b, "frameRight", px[2 * li] + 4 + ox);
				attr(b, "frameBottom", px[2 * li + 1] + 4 + oy);
				attr(b, "zIndex", z--);
				ct_put(b, "/>\n");
			}
			free(px);
		}
		free(sum);
		for ( li = 0; li < d->nl; li++ ) {
			double	lx = PAD + ( d->nl > 1 ? sp * li : aw / 2 ), lw = ( sp > 0 ) ? sp : aw;

			label(b, rnd(lx - lw / 2), rnd(PAD + ah + 5), rnd(lw), z--, d->label[li], ox, oy);
		}
	}
}

static void chart_shapes( CTBUF *b, CONST CXSHEET *s, CONST CXCHART *c, INT ox, INT oy )
{
	DATA	d;

	memset(&d, 0, sizeof(d));
	if ( c->has_range ) data_of(s, &c->range, &d);
	shapes(b, c->kind, c->right - c->left, c->bottom - c->top, &d, ox, oy);
	data_free(&d);
}

void chart_xml( CTBUF *b, CONST CXSHEET *s, INT i, void *arg )
{
	CONST CXCHART	*c = &s->ch[i];
	char		nm[16];

	(void)arg;
	ct_put(b, "<group");
	attr(b, "left", c->left);
	attr(b, "top", c->top);
	attr(b, "right", c->right);
	attr(b, "bottom", c->bottom);
	ct_put(b, ">\n<calc chartType=\"");
	ct_put(b, c->name);
	ct_put(b, "\"");
	if ( c->has_range ) {
		ct_put(b, " dataRange=\"");
		(void)cx_cell_name(c->range.c0, c->range.r0, nm);
		ct_put(b, nm);
		ct_put(b, ":");
		(void)cx_cell_name(c->range.c1, c->range.r1, nm);
		ct_put(b, nm);
		ct_put(b, "\"");
	}
	ct_put(b, ">\n<figure>\n<figView top=\"0\" left=\"0\"");
	attr(b, "right", c->right - c->left);
	attr(b, "bottom", c->bottom - c->top);
	ct_put(b, "/>\n<figDraw top=\"0\" left=\"0\"");
	attr(b, "right", c->right - c->left);
	attr(b, "bottom", c->bottom - c->top);
	ct_put(b, "/>\n<figScale hunit=\"-72\" vunit=\"-72\"/>\n");
	chart_shapes(b, s, c, 0, 0);
	ct_put(b, "</figure>\n</calc>\n</group>\n");
}

void chart_draw( INT i )
{
	T_DPRECT	r, clip;
	CTBUF		b;
	UB		*buf = malloc(512 * 1024);

	if ( buf == NULL ) return;
	chart_box(i, &r);
	clip = r;
	if ( clip.left < grid_left() ) clip.left = grid_left();
	if ( clip.top < grid_top() ) clip.top = grid_top();
	if ( clip.right > g_w ) clip.right = g_w;
	if ( clip.bottom > g_h ) clip.bottom = g_h;
	if ( clip.right > clip.left && clip.bottom > clip.top ) {
		ct_buf(&b, buf, 512 * 1024);
		ct_put(&b, "<tad version=\"1.0\" encoding=\"UTF-8\">\n<figure>\n");
		chart_shapes(&b, &g_s, &g_s.ch[i], 0, 0);
		ct_put(&b, "</figure>\n</tad>\n");
		if ( !b.fail ) (void)dp_draw_tad(g_gid, &clip, r.left, r.top, b.p, b.n);
	}
	free(buf);
}

/* A chart of the cells taken, put to their right; FALSE when they hold no series or no labels */
BOOL chart_insert( INT kind )
{
	CXRANGE	r;
	CXCHART	c;
	DATA	d;
	INT	k;
	BOOL	any = FALSE;

	range_of(&r);
	data_of(&g_s, &r, &d);
	for ( k = 0; k < d.nl; k++ ) {
		if ( d.label[k][0] != 0 ) any = TRUE;
	}
	k = d.ns;
	data_free(&d);
	if ( k == 0 || !any ) return FALSE;
	memset(&c, 0, sizeof(c));
	c.kind = kind;
	snprintf(c.name, sizeof(c.name), "%s", kinds[kind]);
	c.left = HEAD_W + col_x(r.c1 + 1) + 20;
	c.top = row_y(r.r0);
	c.right = c.left + 200;
	c.bottom = c.top + 200;
	c.has_range = TRUE;
	c.range = r;
	if ( !cx_chart_add(&g_s, &c) ) return FALSE;
	g_chart = g_s.nch - 1;
	g_cells_on = FALSE;
	g_changed = TRUE;
	return TRUE;
}

/*
 * A chart on a page of 印刷 (calc_print.c): its shapes where the grid's
 * corner puts it, moved by dx, dy. A chart's left counts the rows'
 * numbers, which are not printed.
 */
void chart_print( CTBUF *b, CONST CXSHEET *s, INT i, INT dx, INT dy, void *arg )
{
	CONST CXCHART	*c = &s->ch[i];

	(void)arg;
	chart_shapes(b, s, c, c->left - HEAD_W + dx, c->top + dy);
}

/* The chart taken, to the tray as a figure (its shapes where it is) */
BOOL chart_frag( CTBUF *b, INT i )
{
	CONST CXCHART	*c = &g_s.ch[i];

	ct_put(b, "<tad version=\"1.0\" encoding=\"UTF-8\">\n<figure>\n");
	chart_shapes(b, &g_s, c, c->left, c->top);
	ct_put(b, "</figure>\n</tad>\n");
	return (BOOL)!b->fail;
}
