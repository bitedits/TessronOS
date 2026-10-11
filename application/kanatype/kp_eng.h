/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	kp_eng.h
 *	かな入力練習: the questions, the judging of each key and the scores
 *	(design 11.14)
 *
 *	The questions are the paragraphs of the object's record 0. A
 *	question is phrases parted by blanks, and a word written
 *	"漢字(かんじ)" shows the word with its reading over it and is typed
 *	as the reading. A practice takes KP_PICK questions at random, none
 *	twice, and goes through them phrase by phrase.
 *
 *	A key is judged by the kana layout chosen (ts/kbmap.h): the kana it
 *	types on the plane the shifts held choose, or a chord whose keys are
 *	all held; the longest chord's kana are tried first. A kana with a
 *	(han)dakuten that the layout does not put on a key is typed as its
 *	kana and then the mark. With romaji the letters are judged against
 *	every way the next kana may be spelt.
 *
 *	Nothing here makes a system call or takes memory, and no floating
 *	point is used: the program (kp_main.c) and the kernel's test both
 *	link it. The time is the caller's, in milliseconds.
 */

#ifndef __KP_ENG_H__
#define __KP_ENG_H__

#include <tk/typedef.h>
#include <tk/errno.h>
#include <ts/kbmap.h>

#define KP_Q_MAX	512		/* questions an object may hold */
#define KP_POOL		( 64 * 1024 )	/* bytes of their text */
#define KP_PICK		10		/* questions one practice takes */
#define KP_QCP		512		/* letters of one question */
#define KP_PH_MAX	64		/* phrases of a question */
#define KP_SEG_MAX	256		/* words of a question, with or without a reading */
#define KP_IN_MAX	1024		/* letters typed for a question */
#define KP_LOOK_MAX	8		/* keys shown ahead */
#define KP_REV_MAX	( KB_MAP_MAX * 3 )

/* What a kana is typed with */
#define KP_SH_NONE	0
#define KP_SH_LEFT	1
#define KP_SH_RIGHT	2

/* How the kana are typed */
#define KP_M_KANA	0		/* a kana layout */
#define KP_M_ROMAJI	1

/* A word of a question: what is shown, and its reading (r0 == r1: none) */
typedef struct {
	INT	d0, d1;			/* in the question's letters */
	INT	r0, r1;
} KPSEG;

typedef struct {
	INT	seg0, nseg;		/* its words */
	INT	in0, nin;		/* what is typed for it, in the reading's letters */
} KPPHRASE;

/* A key to come: a chord (nkey > 0) or one key with a shift */
typedef struct {
	UH	keys[KB_COMBO_KEYS];
	INT	nkey;
	UH	usage;
	INT	shift;			/* KP_SH_* */
	UH	cp[KB_CP];		/* what it types */
} KPNEXT;

/* What a key did */
typedef struct {
	INT	correct;		/* 1 right, 0 wrong, -1 neither yet */
	BOOL	partial;		/* a chord or a mark still to come */
	BOOL	has_char;		/* the key typed a letter (cp) */
	UH	cp[KB_CP];
	BOOL	combo_wrong;		/* a key that is not of the chord awaited */
	UH	combo[KB_COMBO_KEYS];	/* the chord's keys, when it was one */
	INT	ncombo;
	BOOL	finished;		/* the last question is done */
	BOOL	phrase_done;
	BOOL	question_done;
} KPRES;

/* The scores, as shown: tenths and hundredths are whole numbers */
typedef struct {
	INT	speed;			/* letters a minute */
	INT	accuracy;		/* tenths of a per cent */
	INT	left, right;		/* keys of each hand */
	INT	shift_ratio;		/* tenths of a per cent */
	INT	distance;		/* hundredths of a key a letter */
	INT	total;			/* tenths of a key */
	INT	correct, wrong, keys;
	INT	seconds;
} KPSCORE;

typedef struct {
	/* the questions */
	UB		pool[KP_POOL];
	INT		pool_n;
	INT		q_at[KP_Q_MAX], q_len[KP_Q_MAX];
	INT		nq;
	INT		pick[KP_Q_MAX];		/* the questions of the practice, in order */
	INT		npick;
	INT		pick_count;		/* how many a practice takes */

	/* the question shown */
	INT		cq;
	UH		q[KP_QCP];
	INT		nqcp;
	KPSEG		seg[KP_SEG_MAX];
	INT		nseg;
	KPPHRASE	ph[KP_PH_MAX];
	INT		nph;
	UH		in[KP_IN_MAX];
	INT		nin;
	INT		cph, cch;		/* the phrase, and the letter of its reading */

	/* how it is typed */
	INT		mode;
	CONST KBKANAMAP	*map;
	struct {
		UH	cp;
		UH	usage;
		INT	shift;
	}		rev[KP_REV_MAX];	/* each kana's key: the last that types it */
	INT		nrev;
	INT		maxcombo;		/* letters of the longest chord's kana */
	UH		pending;		/* a kana whose mark is awaited */
	char		rbuf[KB_ROMA_LEN * 2];	/* romaji typed for the kana to come */
	INT		nrbuf;

	/* the scores */
	INT		correct, wrong, left, right, shift, keys;
	D		dist;			/* KB_UNIT a key */
	BOOL		started;
	UD		start, end;		/* ms; end 0 while it goes on */
	UINT		last;			/* the key before, 0 none */

	BOOL		practising;
	BOOL		completed;
} KPENG;

/* ---------------------------------------------------------------- the questions */

IMPORT void kp_init( KPENG *e );

/* The paragraphs of xmlTAD as the questions; how many (one, こんにちは, when there are none) */
IMPORT INT  kp_questions( KPENG *e, CONST UB *xtad, INT len );

/* The kana layout, or romaji (map NULL); taken as it is, not copied */
IMPORT void kp_set_map( KPENG *e, INT mode, CONST KBKANAMAP *map );

/* Question index of the practice shown from its start */
IMPORT void kp_load( KPENG *e, INT index );

/* A practice begun: rnd() picks the questions */
IMPORT void kp_start( KPENG *e, UINT (*rnd)( void ), UD now );

/* Stopped where it is */
IMPORT void kp_stop( KPENG *e );

/* ---------------------------------------------------------------- keys */

/*
 * A key pressed: its usage (translated already), the shifts held, the
 * keys held (with this one), the keyboard it is measured on (NULL: no
 * distance) and the time.
 */
IMPORT void kp_key( KPENG *e, UINT usage, BOOL lsh, BOOL rsh, CONST KBSET *held,
		    CONST KBLAYOUT *kb, UD now, KPRES *r );

/* The keys to come, at most count of them; how many */
IMPORT INT  kp_next( KPENG *e, INT count, KPNEXT *out );

/* How many letters of the reading the key or chord to come types */
IMPORT INT  kp_expected_len( KPENG *e );

/* ---------------------------------------------------------------- what is shown */

IMPORT void kp_score( KPENG *e, UD now, KPSCORE *s );

/* The question shown: its number from 1, and of how many */
IMPORT void kp_progress( KPENG *e, INT *p_cur, INT *p_total );

/* Code points as UTF-8, ending in 0; its length */
IMPORT INT  kp_utf8( CONST UH *cp, INT n, UB *out, INT max );

/* A whole number of tenths or hundredths written with its point: 125, 1 -> "12.5" */
IMPORT void kp_fixed( INT v, INT places, char *out );

#endif /* __KP_ENG_H__ */
