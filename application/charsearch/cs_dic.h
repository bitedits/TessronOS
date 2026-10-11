/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cs_dic.h
 *	文字検索: the dictionary, the searches and the character sets
 *	(design 11.19)
 *
 *	The dictionary is the record tools/gen_chardic.py writes (its
 *	format is described there), read whole into memory and looked at
 *	where it is. A character is its Unicode code point, as text is in
 *	TessronOS; the character sets of TRON code's first plane and their
 *	codes come from lib/libbpk's tables. Nothing here makes a system
 *	call or asks for memory, so the accessory and the kernel's test use
 *	the same code.
 */

#ifndef __CS_DIC_H__
#define __CS_DIC_H__

#include <tk/typedef.h>
#include <tk/errno.h>

/* The sources a dictionary was made with (its header) */
#define CD_SRC_KANJIDIC	0x01
#define CD_SRC_KRAD	0x02
#define CD_SRC_KANJIVG	0x04
#define CD_SRC_UNIHAN	0x08
#define CD_SRC_FONT	0x10

/* An entry's flags */
#define CD_F_KANJI	0x0001
#define CD_F_JOYO	0x0002		/* 常用漢字 */
#define CD_F_JINMEI	0x0004		/* 人名用漢字 */
#define CD_F_GLYPH	0x0008		/* the system's face has it */
#define CD_F_ELEM	0x0010		/* an element of another kanji */
#define CD_F_HANGUL	0x0020

/* The fields of an entry's data */
#define CD_T_ON		1		/* 音読み, in hiragana, a space between */
#define CD_T_KUN	2		/* 訓読み, "." before the okurigana */
#define CD_T_ELEMS	3		/* elements: UINT each, code point << 8 | how many times */
#define CD_T_TOP	4		/* the elements of the first level: UINT code points */
#define CD_T_VARS	5		/* related characters: UINT kind << 24 | code point */
#define CD_T_NAME	6		/* the Unicode name */
#define CD_T_KEYS	7		/* the parts of a symbol, UTF-8 */
#define CD_T_BIG5	8		/* two bytes */
#define CD_T_WORDS	9		/* other words it is found by, a space between */

/* The kinds of related characters */
#define CD_V_ITAI	1		/* 異体字 */
#define CD_V_SIMP	2		/* 中国漢字 (simplified) */
#define CD_V_TRAD	3		/* 台湾漢字 (traditional) */
#define CD_V_OLD	4		/* 旧字体 */
#define CD_V_NEW	5		/* 新字体 */
#define CD_V_UPPER	6		/* 大文字 */
#define CD_V_LOWER	7		/* 小文字 */
#define CD_V_REL	8		/* 関連字 */

/* Scripts (字種 of 記号検索), 0 any */
#define CD_SC_LATIN	1
#define CD_SC_CYRIL	2
#define CD_SC_GREEK	3
#define CD_SC_ARABIC	4
#define CD_SC_HEBREW	5
#define CD_SC_INDIC	6
#define CD_SC_THAI	7
#define CD_SC_HANGUL	8
#define CD_SC_CHINA	9
#define CD_SC_JAPAN	10
#define CD_SC_OTHER	11
#define CD_NSCRIPT	12

/* Classes (分類), 0 any */
#define CD_CL_CASE	1		/* 大小文字 */
#define CD_CL_LETTER	2		/* 文字一般 */
#define CD_CL_DIGIT	3		/* 数字 */
#define CD_CL_MARK	4		/* 記号一般 */
#define CD_CL_PHON	5		/* 発音記号 */
#define CD_CL_SHAPE	6		/* 図形 */
#define CD_CL_UNIT	7		/* 単位 */
#define CD_NCLASS	8

typedef struct {
	UINT	cp;			/* UINT, not UW: the same 32 bits in the kernel and in a process */
	UINT	data;			/* from the start of the data */
	UH	rank;			/* the order results are shown in */
	UH	flags;			/* CD_F_* */
	UB	strokes;		/* 0 not known */
	UB	radical;		/* 1..214, 0 not known */
	UB	grade;			/* KANJIDIC's: 1..6 教育漢字, 8 常用, 9 10 人名用 */
	UB	script;
	UB	cls;
	UB	sets;			/* CD_M_*: the sets of TRON code's first plane that have it */
	UB	pad[2];
} CDENT;

#define CD_M_X0208	0x01		/* JIS X 0208 */
#define CD_M_X0213_1	0x02		/* JIS X 0213's first plane, where JIS X 0208 has nothing */
#define CD_M_X0213_2	0x04		/* JIS X 0213's second plane */
#define CD_M_X0212	0x08		/* JIS X 0212 */
#define CD_M_GB		0x10		/* GB 2312 */
#define CD_M_KS		0x20		/* KS X 1001 */

typedef struct {
	CONST UB	*base;
	SZ		size;
	CONST CDENT	*ent;
	INT		n;
	CONST UB	*data;
	UINT		dsize;
	CONST UB	*elem;		/* the list of elements, 8 bytes each */
	INT		nelem;
	CONST UB	*jis208;	/* JIS X 0208's places, a bit each */
	UINT		src;
} CDIC;

/* A dictionary in memory, checked: E_OK, or E_OBJ when it is not one */
IMPORT ER   cd_open( CDIC *d, CONST void *rec, SZ size );

/* The entry of a character, NULL when the dictionary has none */
IMPORT CONST CDENT *cd_find( CONST CDIC *d, UINT cp );

/* A field of an entry's data: its length and where it is, -1 when it has none */
IMPORT INT  cd_field( CONST CDIC *d, CONST CDENT *e, INT tag, CONST UB **p_at );

/* Element i of the list to choose from: its code point, the character shown and its strokes */
IMPORT UINT   cd_elem( CONST CDIC *d, INT i, UINT *p_shown, INT *p_strokes );

/* How many strokes a character has: its entry's, or the list's for an element; 0 not known */
IMPORT INT  cd_strokes( CONST CDIC *d, UINT cp );

/* Radical n (1..214) as UTF-8 into s (4 bytes and a 0) */
IMPORT void cd_radical( INT n, char *s );

/* ---------------------------------------------------------------- searching */

/* The character sets a search looks in (検索設定), and a character's set */
#define CS_SET_JIS	0x01		/* 日本基本: JIS X 0208 and JIS X 0213 */
#define CS_SET_HOJO	0x02		/* 日本補助: JIS X 0212 */
#define CS_SET_GB	0x04		/* 中国: GB 2312 */
#define CS_SET_KS	0x08		/* 韓国: KS X 1001 */
#define CS_SET_UCS	0x10		/* 各国: the rest of the BMP */
#define CS_SET_ALL	0x1F

/* What a search answers below 0 */
#define CS_E_EMPTY	( -1 )		/* no condition */
#define CS_E_SYNTAX	( -2 )		/* a condition that cannot be read */

/*
 * 漢字検索: the kanji that meet every condition of q (UTF-8, the
 * conditions apart by spaces), in the order of their readings, into
 * out (at most max); answers how many were put there and *p_total how
 * many there are, or CS_E_*. The conditions are:
 *
 *   ひらがな		a reading (音 or 訓, without its okurigana or with it)
 *   カタカナ1字	an element like that katakana (イ is 亻)
 *   n			an element of n strokes
 *   漢字		an element, or the kanji itself; its variants too
 *   漢字×n, 漢字*n	that element n times
 *   漢字1-漢字2	the elements of 漢字1 but 漢字2
 *   =n =n+m =n-m =n±m =n+-m =n～m	the strokes of the whole
 *   . or 。		of the elements given and no others
 */
IMPORT INT  cs_kanji( CONST CDIC *d, CONST char *q, UINT sets, UINT *out, INT max, INT *p_total );

/*
 * 記号検索: the characters other than kanji whose name has every word
 * of name (romaji in capitals for the kana; a few Japanese words), whose
 * parts hold every part in parts, in script and class (0 any); in the
 * order of their codes.
 */
IMPORT INT  cs_symbol( CONST CDIC *d, CONST char *name, CONST char *parts, INT script, INT cls,
		       UINT sets, UINT *out, INT max, INT *p_total );

/* One character out of UTF-8: its length (0 at the end) and *p_cp */
IMPORT INT  cs_utf8_next( CONST UB *s, UINT *p_cp );
IMPORT INT  cs_utf8_put( UINT cp, char *o );		/* bytes written, at most 4 */

/* ---------------------------------------------------------------- the character sets */

/* The sets of the code list (コード一覧), each a table of rows of places */
#define CS_JIS		1		/* 日本基本: plane 1 (JIS X 0208 and 0213) and plane 2 (0213) */
#define CS_HOJO		2		/* 日本補助: JIS X 0212 */
#define CS_GB		3		/* 中国: GB 2312 */
#define CS_KS		4		/* 韓国: KS X 1001 */
#define CS_UCS		5		/* 各国: Unicode's BMP, 256 to a row */
#define CS_TRON		6		/* TRON code: a plane's codes */

/* What a place of a set holds, for its colour */
#define CS_K_NONE	0
#define CS_K_JIS12	1		/* JIS level 1 and 2, and JIS X 0208's other characters */
#define CS_K_JIS3	2		/* JIS X 0213's first plane */
#define CS_K_JIS4	3		/* its second plane */
#define CS_K_HOJO	4
#define CS_K_GB		5
#define CS_K_KS		6
#define CS_K_UCS	7
#define CS_K_OTHER	8		/* a TRON code no table fills */

/* A place: the set, its plane (日本基本 1 or 2, TRON's plane), row and cell; and TRON code */
typedef struct {
	INT	set;
	INT	kind;			/* CS_K_* */
	INT	plane, row, cell;	/* CS_UCS: the code point in cell */
	INT	tplane;			/* TRON code: the plane */
	UINT	tcode;			/* and the code in it */
} CSPLACE;

/* The cells of a row of a set: 94 (CS_UCS 256, CS_TRON 94 or 126) */
IMPORT INT  cs_row_cells( INT set, INT plane, INT row );

/* The rows of a set's plane: their number and the first */
IMPORT INT  cs_rows( INT set, INT plane, INT *p_first );

/*
 * The character(s) of a place: how many (0 none, 2 for a letter and a
 * mark that are one place), and its kind. For CS_TRON plane is the
 * plane, row the first byte and cell from 0 the place along the row.
 */
IMPORT INT  cs_cell( CONST CDIC *d, INT set, INT plane, INT row, INT cell, UINT out[2], INT *p_kind );

/* Where a character is, every set that has it, its first (the one TRON code uses) first */
IMPORT INT  cs_places( CONST CDIC *d, UINT cp, CSPLACE *out, INT max );

/* The set a character is shown as (CS_SET_*), 0 none */
IMPORT UINT cs_set_of( CONST CDIC *d, UINT cp );

/* The kind of a character for its colour (CS_K_*) */
IMPORT INT  cs_kind_of( CONST CDIC *d, UINT cp );

/* A character's TRON code: the plane and the code */
IMPORT ER   cs_tron( UINT cp, INT *p_plane, UINT *p_code );

/* The character of a TRON code, 0 none (the planes no table fills: GT, 大漢和 ...) */
IMPORT UINT   cs_from_tron( INT plane, UINT code );

/* テキスト形式TRONコード, "&T213021;" */
IMPORT void cs_tron_text( UINT cp, char *s );

/*
 * A place written as people write it, into s: "1-16-01" (日本基本,
 * with its plane), "16-01" (the others), "U+4E9C", "1-3021" (TRON)
 */
IMPORT void cs_place_text( CONST CSPLACE *p, char *s );

/* Its Shift_JIS and EUC codes (0 none) */
IMPORT UINT cs_sjis( CONST CSPLACE *p );
IMPORT UINT cs_euc( CONST CSPLACE *p );

/*
 * A code as the code list's box has it, for a set: "16-1" or "1-16-1"
 * (日本基本: plane, row, cell), "16" (a row), "4E9C" or "U+4E9C",
 * "1 3021" or "&T213021;" (TRON). The place found, FALSE when the text
 * is not one.
 */
IMPORT BOOL cs_parse_code( INT set, CONST char *s, INT *p_plane, INT *p_row, INT *p_cell );

#endif /* __CS_DIC_H__ */
