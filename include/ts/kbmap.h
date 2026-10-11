/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	kbmap.h
 *	Keyboards and what their keys type: the one table of layouts
 *	(design 11.14)
 *
 *	Three kinds of layout, each read from the JSON a TADjs Desktop
 *	keeps them in:
 *
 *	  a keyboard	where each key is and what is printed on it, in
 *			the rows of keyboard-layout-editor ("layout": rows of
 *			property objects and labels; r, rx, ry turn a key)
 *	  a kana layout	the kana each key types plainly, with the left
 *			shift and with the right ("mapping"), the chords that
 *			type kana together ("combos"), and which hand and
 *			finger each key is
 *	  a translation	the key a keyboard's own firmware reports in place
 *			of another ("keycodeTranslation"), Dvorak made in the
 *			keyboard
 *
 *	A key is named as a KeyboardEvent.code names it ("KeyQ", "IntlRo")
 *	in the JSON and is held here as its HID usage, which is what the
 *	keyboard reports and the window manager passes on. The layouts the
 *	system carries are here too, as the same JSON: the Japanese 109
 *	keyboard, the TRON kana layout and the JIS kana layout, and the
 *	spellings of romaji. Places are in millionths of a key, angles
 *	in thousandths of a degree; nothing is done in floating point, so
 *	the kernel and a program read a layout the same way.
 *
 *	Nothing here makes a system call or takes memory: what is read is
 *	the caller's bytes, what is made goes into the caller's structure.
 */

#ifndef __TS_KBMAP_H__
#define __TS_KBMAP_H__

#include <tk/typedef.h>
#include <tk/errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KB_UNIT		1000000		/* a key's width, in the units places are given in */

#define KB_NAME		64		/* bytes of a layout's name */
#define KB_LABEL	24		/* bytes of what is printed on a key */
#define KB_CP		8		/* letters one key or chord types, with the 0 after them */
#define KB_KEY_MAX	160		/* keys of a keyboard */
#define KB_MAP_MAX	128		/* keys a kana layout puts kana on */
#define KB_COMBO_MAX	192		/* chords of a kana layout */
#define KB_COMBO_KEYS	4		/* keys of one chord */
#define KB_TRANS_MAX	128		/* keys a translation changes */
#define KB_FINGERS	10

/* The planes of a kana layout */
#define KB_PLAIN	0
#define KB_LEFT		1		/* with the left shift */
#define KB_RIGHT	2		/* with the right shift */

/* Usages of the keys the layouts treat apart */
#define KB_K_ENTER	0x28
#define KB_K_ESC	0x29
#define KB_K_SPACE	0x2C
#define KB_K_EISU	0x39		/* CapsLock, 英数 */
#define KB_K_LCTRL	0xE0
#define KB_K_LSHIFT	0xE1
#define KB_K_LALT	0xE2
#define KB_K_LMETA	0xE3
#define KB_K_RCTRL	0xE4
#define KB_K_RSHIFT	0xE5
#define KB_K_RALT	0xE6
#define KB_K_RMETA	0xE7

/*
 * A set of keys: a bit for each usage, in bytes so that it is the same
 * whichever width the including file's UW has
 */
typedef struct {
	UB	b[32];
} KBSET;

#define KB_IN(s, u)	( (UINT)(u) < 256 && ( (s)->b[(UINT)(u) >> 3] & ( 1U << ( (UINT)(u) & 7 ) ) ) != 0 )
#define KB_ADD(s, u)	do { if ( (UINT)(u) < 256 ) (s)->b[(UINT)(u) >> 3] |= (UB)( 1U << ( (UINT)(u) & 7 ) ); } while (0)
#define KB_DEL(s, u)	do { if ( (UINT)(u) < 256 ) (s)->b[(UINT)(u) >> 3] &= (UB)~( 1U << ( (UINT)(u) & 7 ) ); } while (0)

/* One key of a keyboard */
typedef struct {
	UH	usage;			/* 0: a key the layout gives no id */
	INT	x, y, w, h;		/* its box before it is turned, KB_UNIT a key */
	INT	r;			/* turned this many thousandths of a degree about its middle */
	UB	label[KB_LABEL];	/* what is printed on it */
	UB	shift[KB_LABEL];	/* and above that, typed with a shift */
} KBKEY;

typedef struct {
	UB	name[KB_NAME];
	INT	n;
	KBKEY	k[KB_KEY_MAX];
	INT	w, h;			/* the whole keyboard; its top left corner is 0, 0 */
} KBLAYOUT;

/* What one key types on each plane: code points ending in 0, empty for nothing */
typedef struct {
	UH	usage;
	UH	cp[3][KB_CP];
} KBKANA;

typedef struct {
	UH	keys[KB_COMBO_KEYS];
	INT	nkey;
	UH	cp[KB_CP];		/* what the chord types */
} KBCOMBO;

typedef struct {
	UB	name[KB_NAME];
	INT	n;
	KBKANA	k[KB_MAP_MAX];
	INT	nc;
	KBCOMBO	c[KB_COMBO_MAX];
	BOOL	has_hand;
	KBSET	hand[2];		/* left, right */
	BOOL	has_finger;
	KBSET	finger[KB_FINGERS];	/* left pinky .. left thumb, right index .. right thumb */
} KBKANAMAP;

typedef struct {
	UB	name[KB_NAME];
	INT	n;
	UH	from[KB_TRANS_MAX], to[KB_TRANS_MAX];
} KBTRANS;

/* The usage of a key named as KeyboardEvent.code names it; 0 when unknown */
IMPORT UINT kb_usage( CONST UB *name, INT len );

/* The name of a usage, NULL when it has none */
IMPORT CONST char *kb_key_name( UINT usage );

/* ---------------------------------------------------------------- reading */

/* What kind a layout's JSON is, in the order a dropped one is tried */
#define KB_KIND_NONE	0
#define KB_KIND_KEYBOARD 1		/* "layout", or meta.format "kle-extended" */
#define KB_KIND_TRANS	2		/* "keycodeTranslation" */
#define KB_KIND_KANA	3		/* "mapping" */
IMPORT INT  kb_kind( CONST UB *json, INT len );

/* meta.name, unescaped; its length, 0 when there is none */
IMPORT INT  kb_meta_name( CONST UB *json, INT len, UB *out, INT max );

/*
 * A keyboard, a kana layout, a translation from their JSON. A name
 * that is not given is left empty for the caller to make one. E_PAR
 * when the text is not such a layout; keys beyond what fits are left
 * out (E_LIMIT is not answered: what fits is the layout).
 */
IMPORT ER   kb_read_keyboard( CONST UB *json, INT len, KBLAYOUT *l );
IMPORT ER   kb_read_kana( CONST UB *json, INT len, KBKANAMAP *m );
IMPORT ER   kb_read_trans( CONST UB *json, INT len, KBTRANS *t );

/*
 * The JSON written as JSON.stringify writes what JSON.parse read of it:
 * no blanks, strings escaped only where they must be, numbers in their
 * shortest form. The length, or E_PAR for text that is not JSON and
 * E_LIMIT when out is too small.
 */
IMPORT INT  kb_json_compact( CONST UB *json, INT len, UB *out, INT max );

/* ---------------------------------------------------------------- looking up */

/* What a key types on a plane, NULL for nothing */
IMPORT CONST UH *kb_kana_of( CONST KBKANAMAP *m, UINT usage, INT plane );

/* The hand a key is on: 0 neither, 1 left, 2 right */
IMPORT INT  kb_hand( CONST KBKANAMAP *m, UINT usage );

/* The key a translation reports in place of usage (usage when it changes nothing) */
IMPORT UINT kb_trans( CONST KBTRANS *t, UINT usage );

/* The middle of the first key with this usage, turned as it is; FALSE when there is none */
IMPORT BOOL kb_key_middle( CONST KBLAYOUT *l, UINT usage, INT *p_x, INT *p_y );

/* sin and cos of an angle in thousandths of a degree, 2^30 being 1 */
IMPORT INT  kb_sin( INT mdeg );
IMPORT INT  kb_cos( INT mdeg );

/* The square root of n, to the nearest whole */
IMPORT UD   kb_sqrt( UD n );

/* ---------------------------------------------------------------- the system's layouts */

IMPORT CONST char kb_jp109_json[];	/* 日本語109キー: the keyboard when an object has none */
IMPORT CONST char kb_tron_json[];	/* TRONかな配列: the kana layout when an object has none */
IMPORT CONST char kb_jis_json[];	/* JISかな: the kana printed on the Japanese keyboard */

/*
 * Romaji: the ways a kana (one, or one and a small one) is spelt, the
 * first the one shown. Spellings are lower case letters and the signs
 * typed as they are (- , . /).
 */
#define KB_ROMA_WAYS	4
#define KB_ROMA_LEN	8		/* bytes of a spelling, with its 0 */
typedef struct {
	UH	cp[3];			/* the kana, ending in 0 */
	char	way[KB_ROMA_WAYS][KB_ROMA_LEN];
} KBROMA;

IMPORT CONST KBROMA kb_roma[];
IMPORT CONST INT    kb_nroma;

/* The usage of a letter or sign typed as romaji, 0 for none */
IMPORT UINT kb_roma_key( char c );

/* The letter or sign a key types as romaji, 0 for none */
IMPORT char kb_roma_char( UINT usage );

#ifdef __cplusplus
}
#endif

#endif /* __TS_KBMAP_H__ */
