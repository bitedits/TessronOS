/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	tc_db.h
 *	電子手帳: what the object holds, read, changed and written (design 11.23)
 *
 *	A 電子手帳 object is a text: record 0 is an xmlTAD document whose
 *	paragraphs are cut into three parts by tab formats (<tab-format>),
 *	so that 基本文章編集 opens and edits it as well:
 *
 *	  《タブ書式》 予定表 《タブ書式》 住所録 《タブ書式》 休日 《タブ書式》
 *
 *	予定表: a paragraph for each plan, "西暦/月/日<tab>予定"; a day's
 *	plans are its paragraphs in order, and an empty one keeps a row of
 *	the day empty above a row written.
 *	住所録: a paragraph for each person, its ten fields cut by tabs in
 *	the order よみ, 郵便番号, 住所1, 住所2, 住所3, 氏名, 電話, FAX, メモ1,
 *	メモ2; kept in the order of よみ, by its index (あ か さ た な は ま
 *	や ら わ 他) first.
 *	休日: the days of the week that are holidays as their letters ("土日"),
 *	and dates "西暦/月/日" where '*' is every year, month or day, the year
 *	may be a range "1990-1999", and the day may be "nW", the n-th W of the
 *	month ("2月"). A line "祝日" makes the national holidays holidays,
 *	reckoned from the law's rules for any year (tc_jp_holiday). Words
 *	after a tab are a note for the reader. The lines are kept as they
 *	are written.
 *
 *	The document's <paper> is the paper 印刷 lays the pages on (用紙設定).
 *	Anything else in the text -- links, decorations, figures -- is passed
 *	over and not written back.
 *
 *	Searching takes hiragana and katakana, and capital and small letters,
 *	as the same; in the 住所録 "欄の名前:文字列" looks in that field only.
 *
 *	Nothing here makes a system call. Memory is taken with tc_alloc and
 *	given back with tc_free; the program and the kernel's test each give
 *	their own. What is written goes into the caller's CTBUF.
 */

#ifndef __TC_DB_H__
#define __TC_DB_H__

#include <tk/typedef.h>
#include <tk/errno.h>
#include "../calendar/cal_tad.h"

#define TC_Y_FIRST	1990		/* the first page of the 予定表 and カレンダー */
#define TC_Y_LAST	2050		/* and the year of the last */
#define TC_DAY_FIRST	19900101
#define TC_DAY_LAST	20501231
#define TC_LINE_MAX	512		/* bytes of a field, with its terminator */
#define TC_ROWS		10		/* 記入欄 of a day of the 予定表 */
#define TC_NF		10		/* fields of a person of the 住所録 */
#define TC_IDX_N	11		/* the indexes of the 住所録 */
#define TC_IDX_OTHER	10		/* 他 */
#define TC_RULE_MAX	512		/* holiday dates */
#define TC_HOLLINE_MAX	1024		/* lines of the 休日 part */

/* The fields of a person, in the order the record has them */
#define TC_F_YOMI	0
#define TC_F_ZIP	1
#define TC_F_ADDR1	2
#define TC_F_ADDR2	3
#define TC_F_ADDR3	4
#define TC_F_NAME	5
#define TC_F_TEL	6
#define TC_F_FAX	7
#define TC_F_MEMO1	8
#define TC_F_MEMO2	9

/* A day is a number yyyymmdd */
typedef struct {
	INT	day;
	UB	*text;			/* never NULL: "" for an empty row */
} TCPLAN;

typedef struct {
	UB	*f[TC_NF];		/* NULL for an empty field */
	INT	idx;			/* its index, from よみ */
} TCADDR;

/* A holiday date: 0 for '*', nth 1..5 with wday for "nW" */
typedef struct {
	INT	y0, y1, m, d, nth, wday;
} TCRULE;

typedef struct {
	TCPLAN	*p;
	INT	np, pmax;
	TCADDR	**a;
	INT	na, amax;
	UB	**hl;			/* the 休日 lines as written */
	INT	nhl;
	UINT	week;			/* bit w: weekday w (0 Sunday) is a holiday */
	BOOL	jp;			/* a line "祝日": the national holidays, as the law makes them */
	TCRULE	r[TC_RULE_MAX];
	INT	nr;
	/* the paper of 印刷, in points: its size and the margins */
	INT	pw, ph, mt, mb, ml, mr;
	INT	dropped;		/* paragraphs that were not read as anything */
} TCDB;

/* What the program and the kernel's test give */
IMPORT void *tc_alloc( INT size );
IMPORT void tc_free( void *p );

IMPORT void tc_init( TCDB *db );
IMPORT void tc_clear( TCDB *db );

/* ---------------------------------------------------------------- reading and writing */

/* Record 0 read: the three parts and the paper. E_NOMEM when memory runs out */
IMPORT ER   tc_read( TCDB *db, CONST UB *x, INT n );

/* Record 0 written whole, named name */
IMPORT ER   tc_write( CONST TCDB *db, CTBUF *b, CONST UB *name );

/*
 * The letters of the first paragraph of an xmlTAD text that has any (what
 * the tray holds): one line, a tab as a space. Their length.
 */
IMPORT INT  tc_doc_text( CONST UB *x, INT n, UB *out, INT max );

/* ---------------------------------------------------------------- days */

IMPORT INT  tc_day( INT y, INT m, INT d );
IMPORT void tc_day_split( INT day, INT *p_y, INT *p_m, INT *p_d );
IMPORT INT  tc_day_add( INT day, INT n );		/* n days on (or back) */
IMPORT BOOL tc_day_ok( INT day );			/* a day of the pages */
/* "平成17年", "令和元年": the era of a day, 0 when before 明治 */
IMPORT INT  tc_era( INT y, INT m, INT d, UB *out, INT max );
/* "西暦/月/日" at the head of s: the day and how many bytes it took, 0 when it is not one */
IMPORT INT  tc_parse_day( CONST UB *s, INT n, INT *p_day );

/* ---------------------------------------------------------------- 休日 */

IMPORT BOOL tc_holiday( CONST TCDB *db, INT y, INT m, INT d );

/*
 * The national holiday a day is, as 国民の祝日に関する法律 makes them in
 * any year from 1949 on: the fixed days, the Mondays (ハッピーマンデー),
 * 春分の日 and 秋分の日 by the usual reckoning of the equinox (to 2150),
 * the days that came once, 振替休日 and 国民の休日. Its name, or NULL
 * when the day is none. The years after the law as it stands keep its
 * rules as they are now.
 */
IMPORT CONST char *tc_jp_holiday( INT y, INT m, INT d );
/* The 休日 lines given anew (kept as they are written) and their rules read */
IMPORT ER   tc_set_holidays( TCDB *db, CONST UB *CONST *lines, INT n );

/* ---------------------------------------------------------------- 予定表 */

/* The rows of a day: how many, each one's text in out[] (at most max) */
IMPORT INT  tc_plans( CONST TCDB *db, INT day, CONST UB **out, INT max );
/* The rows of a day given anew; the empty ones after the last written are left out */
IMPORT ER   tc_set_plans( TCDB *db, INT day, CONST UB *CONST *rows, INT n );
/* The next day after 'after' (TC_DAY_FIRST - 1 for the first) with a row written, 0 none */
IMPORT INT  tc_next_planned( CONST TCDB *db, INT after );

/* ---------------------------------------------------------------- 住所録 */

IMPORT INT  tc_index_of( CONST UB *yomi );		/* TC_IDX_* of a よみ */
IMPORT CONST char *tc_index_name( INT idx );		/* "あ" .. "他" */
/* A new empty person, put last of 他: its place */
IMPORT INT  tc_addr_add( TCDB *db );
/* A field of the person at i set (NULL or "" empties it): the person's place after it is sorted again */
IMPORT INT  tc_addr_set( TCDB *db, INT i, INT f, CONST UB *text );
/* The person at i taken away; the last one left is emptied instead */
IMPORT void tc_addr_del( TCDB *db, INT i );
IMPORT CONST UB *tc_addr_get( CONST TCDB *db, INT i, INT f );	/* "" when empty */
/* The first person of an index, -1 none */
IMPORT INT  tc_index_first( CONST TCDB *db, INT idx );
/* Where the person at i is in its index: its place from 1, and how many the index has */
IMPORT void tc_index_pos( CONST TCDB *db, INT i, INT *p_pos, INT *p_count );
/* "2/24", or "***" past 999 */
IMPORT void tc_pos_text( INT pos, INT count, UB *out, INT max );

/* The fields as the window shows them, top to bottom, and their names */
IMPORT CONST INT tc_show_order[TC_NF];
IMPORT CONST char *CONST tc_field_name[TC_NF];

/* ---------------------------------------------------------------- 検索 */

/* Whether hay holds needle, kana of either kind and letters of either case the same */
IMPORT BOOL tc_match( CONST UB *hay, CONST UB *needle );
/* The first day after 'after' whose rows hold needle; 0 none */
IMPORT INT  tc_find_plan( CONST TCDB *db, INT after, CONST UB *needle );
/*
 * The first person from 'from' on who holds needle, -1 none. "欄:文字列"
 * (either colon) looks in that field only; 住所 is 住所1 to 3, メモ is
 * メモ1 and 2.
 */
IMPORT INT  tc_find_addr( CONST TCDB *db, INT from, CONST UB *needle );

/* ---------------------------------------------------------------- 印刷 */

/*
 * The pages of 印刷 (design 11.22.6) as one document on the paper of the
 * 用紙設定: a day of the 予定表 (day > 0) or every day with a row written
 * (day 0); a person of the 住所録 (i >= 0) or every one (i < 0).
 */
IMPORT ER   tc_print_plans( CONST TCDB *db, CTBUF *b, INT day );
IMPORT ER   tc_print_addrs( CONST TCDB *db, CTBUF *b, INT i );

/* The paper by its kind (TC_PAPER_*) and its turn; the margins in millimetres */
#define TC_PAPER_A4	0
#define TC_PAPER_A5	1
#define TC_PAPER_B5	2
#define TC_PAPER_B4	3
#define TC_PAPER_A3	4
#define TC_PAPER_LETTER	5
#define TC_PAPER_N	6
IMPORT void tc_set_paper( TCDB *db, INT kind, BOOL turn, CONST INT *mm );
/* The kind the paper is (TC_PAPER_N when none fits), its turn and the margins in millimetres */
IMPORT INT  tc_paper_kind( CONST TCDB *db, BOOL *p_turn, INT *mm );

#endif /* __TC_DB_H__ */
