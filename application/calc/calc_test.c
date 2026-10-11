/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	calc_test.c
 *	基本表計算: the program's own test (design 11.13)
 *
 *	Started with "selftest" (tests/ktest/ktest_calc.c starts it so), the
 *	program works out the sheets of calc_cases.h with its engine and
 *	compares every cell's words with what the plugin's own formula
 *	methods and formula.min.js gave for them (tools/calc_cases.js); a
 *	program built without V8 is held to the sheets its small evaluator
 *	knows. It draws the charts of the table as the record keeps them and
 *	compares them with the plugin's, byte for byte, and reads and writes
 *	the records of the table, and works the parts of the 操作パネル on
 *	a sheet of its own (calc_panel.c). It says on the console what
 *	differed and "calc: selftest N of M" at the end; its exit code is 0
 *	when all of them matched.
 */

#include "calc.h"
#include "calc_cases.h"

#define N_OF(a)		( (INT)( sizeof(a) / sizeof((a)[0]) ) )

typedef struct {
	INT	col, row;
	char	w[256];
} SEEN;

static SEEN	seen[64];
static INT	nseen;

static void got( int col, int row, const unsigned char *s, int len, void *arg )
{
	INT	i;

	(void)arg;
	for ( i = 0; i < nseen && !( seen[i].col == col && seen[i].row == row ); i++ ) ;
	if ( i == nseen ) {
		if ( nseen == N_OF(seen) ) return;
		nseen++;
	}
	seen[i].col = col;
	seen[i].row = row;
	snprintf(seen[i].w, sizeof(seen[i].w), "%.*s", len, (CONST char *)s);
}

static CONST char *seen_of( INT col, INT row )
{
	INT	i;

	for ( i = 0; i < nseen; i++ ) {
		if ( seen[i].col == col && seen[i].row == row ) return seen[i].w;
	}
	return "";
}

static void cell_ref( CONST char *s, INT *p_col, INT *p_row )
{
	(void)cx_cell_parse((CONST UB *)s, (INT)strlen(s), p_col, p_row);
}

INT selftest( void )
{
	INT	i, k, pass = 0, all = 0;
	BOOL	v8;
	char	s[400];
	UB	*buf = malloc(256 * 1024);
	CTBUF	b;

	if ( buf == NULL || ce_start() < 0 ) {
		note("selftest: the engine cannot be started");
		return 2;
	}
	v8 = (BOOL)( strncmp(ce_name(), "V8", 2) == 0 );
	snprintf(s, sizeof(s), "selftest engine %s", ce_name());
	note(s);

	/* the formulas */
	for ( i = 0; i < N_OF(calc_sheets); i++ ) {
		CONST CALC_SHEET	*sh = &calc_sheets[i];

		if ( !v8 && !sh->c ) continue;
		(void)ce_reset();
		nseen = 0;
		for ( k = 0; k < sh->n; k++ ) {
			INT	col, row;

			cell_ref(sh->cells[k].cell, &col, &row);
			(void)ce_set(col, row, (CONST UB *)sh->cells[k].set);
			(void)ce_taken(got, NULL);
		}
		for ( k = 0; k < sh->n; k++ ) {
			INT		col, row;
			CONST char	*w;

			cell_ref(sh->cells[k].cell, &col, &row);
			w = seen_of(col, row);
			all++;
			if ( strcmp(w, sh->cells[k].shows) == 0 ) {
				pass++;
			} else {
				snprintf(s, sizeof(s), "selftest sheet %d %s %s: \"%s\", not \"%s\"", i, sh->cells[k].cell,
					 sh->cells[k].set, w, sh->cells[k].shows);
				note(s);
			}
		}
	}

	/* a range of two cells is the list of their numbers, not a [row,col] reference */
	{
		static CONST struct {
			CONST char	*cell, *set, *shows;
		} two[] = {
			{ "A1", "10", "10" }, { "A2", "20", "20" }, { "B1", "5", "5" },
			{ "C1", "=SUM(A1:A2)", "30" }, { "C2", "=AVERAGE(A1:B1)", "7.5" },
			{ "C3", "=MAX(A1:A2)*2", "40" }, { "C4", "=SUM(A1:A1)", "10" },
		};

		(void)ce_reset();
		nseen = 0;
		for ( k = 0; k < N_OF(two); k++ ) {
			INT	col, row;

			cell_ref(two[k].cell, &col, &row);
			(void)ce_set(col, row, (CONST UB *)two[k].set);
			(void)ce_taken(got, NULL);
		}
		for ( k = 0; k < N_OF(two); k++ ) {
			INT		col, row;
			CONST char	*w;

			cell_ref(two[k].cell, &col, &row);
			w = seen_of(col, row);
			all++;
			if ( strcmp(w, two[k].shows) == 0 ) {
				pass++;
			} else {
				snprintf(s, sizeof(s), "selftest two cells %s %s: \"%s\", not \"%s\"", two[k].cell,
					 two[k].set, w, two[k].shows);
				note(s);
			}
		}
	}

	/* a cell read from a record, and a formula copied */
	for ( i = 0; i < N_OF(calc_old); i++ ) {
		UB	out[256];

		(void)cx_old_form((CONST UB *)calc_old[i].in, (INT)strlen(calc_old[i].in), out, sizeof(out));
		all++;
		if ( strcmp((char *)out, calc_old[i].out) == 0 ) pass++;
		else note("selftest a conversion differs");
	}
	for ( i = 0; i < N_OF(calc_adj); i++ ) {
		UB	out[256];

		(void)cx_move_refs((CONST UB *)calc_adj[i].in, (INT)strlen(calc_adj[i].in), calc_adj[i].dc, calc_adj[i].dr,
				   CX_COLS, CX_ROWS, out, sizeof(out));
		all++;
		if ( strcmp((char *)out, calc_adj[i].out) == 0 ) pass++;
		else note("selftest a move of references differs");
	}

	/* the charts: the cells set as typing sets them, each chart written as the record keeps it */
	(void)ce_reset();
	cx_init(&g_s);
	for ( k = 0; k < N_OF(calc_chart_cells); k++ ) {
		INT	col, row;

		cell_ref(calc_chart_cells[k].cell, &col, &row);
		cell_text(col, row, (CONST UB *)calc_chart_cells[k].set);
	}
	if ( !v8 ) {
		/* the small evaluator's words are the engine's for these cells too */
		note("selftest charts with the small evaluator");
	}
	for ( i = 0; i < N_OF(calc_charts); i++ ) {
		CONST CALC_CHART	*t = &calc_charts[i];
		CXCHART			c;

		memset(&c, 0, sizeof(c));
		snprintf(c.name, sizeof(c.name), "%s", t->kind);
		for ( c.kind = 0; c.kind < CX_CH_KINDS; c.kind++ ) {
			static CONST char *CONST kinds[CX_CH_KINDS] = { "clustered-bar", "stacked-bar", "line", "stacked-line" };

			if ( strcmp(kinds[c.kind], t->kind) == 0 ) break;
		}
		c.left = t->x;
		c.top = t->y;
		c.right = t->x + t->w;
		c.bottom = t->y + t->h;
		c.has_range = cx_range_parse((CONST UB *)t->range, (INT)strlen(t->range), &c.range);
		(void)cx_chart_add(&g_s, &c);
		ct_buf(&b, buf, 256 * 1024);
		chart_xml(&b, &g_s, g_s.nch - 1, NULL);
		all++;
		if ( !b.fail && strcmp((CONST char *)b.p, t->xml) == 0 ) {
			pass++;
		} else {
			INT	at;

			for ( at = 0; b.p[at] != 0 && b.p[at] == (UB)t->xml[at]; at++ ) ;
			snprintf(s, sizeof(s), "selftest chart %d %s differs at %d: \"%.60s\"", i, t->kind, at, (char *)b.p + at);
			note(s);
		}
	}
	cx_free_all(&g_s);

	/* the records read and written again */
	for ( i = 0; i < N_OF(calc_records); i++ ) {
		CONST CALC_RECORD	*r = &calc_records[i];
		CONST char		*nm = strstr(r->in, "filename=\"");
		UB			name[64];

		cx_init(&g_s);
		snprintf((char *)name, sizeof(name), "%.*s", nm ? (int)( strchr(nm + 10, '"') - ( nm + 10 ) ) : 0, nm ? nm + 10 : "");
		(void)cx_read(&g_s, (CONST UB *)r->in, (INT)strlen(r->in));
		ct_buf(&b, buf, 256 * 1024);
		cx_write(&b, &g_s, name, chart_xml, NULL);
		all++;
		if ( !b.fail && strcmp((CONST char *)b.p, r->out) == 0 ) {
			pass++;
		} else {
			INT	at;

			for ( at = 0; b.p[at] != 0 && b.p[at] == (UB)r->out[at]; at++ ) ;
			snprintf(s, sizeof(s), "selftest record %d differs at %d: \"%.60s\"", i, at, (char *)b.p + at);
			note(s);
		}
		cx_free_all(&g_s);
	}
	free(buf);

	/* the 操作パネル: its parts and what they do */
	{
		INT	pall = 0, ppass;

		(void)ce_reset();
		ppass = panel_test(&pall);
		snprintf(s, sizeof(s), "selftest panel %d of %d", ppass, pall);
		note(s);
		pass += ppass;
		all += pall;
	}
	snprintf(s, sizeof(s), "selftest %d of %d", pass, all);
	note(s);
	ce_end();
	return ( pass == all ) ? 0 : 1;
}
