/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cu_eng.h
 *	電卓: what each key does (design 11.17)
 *
 *	The calculator is a number being typed or shown, the operations
 *	waiting for their right operand, and the memory. With the plain
 *	keys every operation has the same rank and is done as the next one
 *	is pressed (1 + 2 × 3 = 9, as on a desk calculator); with the
 *	function keys × and ÷ go before + and −, a power before them, and
 *	brackets group (1 + 2 × 3 = 7). = again repeats the last operation
 *	with the same operand: the left one for ×, the right one otherwise.
 *
 *	The keys are also the numbers of the push buttons of the data box,
 *	so that a button pressed is a key. A function the decimal numbers
 *	cannot work out exactly (the trigonometric functions, logarithms
 *	and a power to a fraction) is asked of eng->fn, which the program
 *	works out in double precision; without it they are an error.
 *	Nothing here makes a system call or takes memory, and no floating
 *	point is used: the program and the kernel's test both link it.
 */

#ifndef __CU_ENG_H__
#define __CU_ENG_H__

#include "cu_dec.h"

/* The keys, and the buttons */
#define CU_K_DIGIT(d)	( 20 + ( d ) )
#define CU_K_POINT	30
#define CU_K_ADD	31
#define CU_K_SUB	32
#define CU_K_MUL	33
#define CU_K_DIV	34
#define CU_K_EQ		35
#define CU_K_PCT	36
#define CU_K_NEG	37		/* ± */
#define CU_K_SQRT	38
#define CU_K_MC		39
#define CU_K_MR		40
#define CU_K_MMINUS	41
#define CU_K_MPLUS	42
#define CU_K_AC		43		/* 全消去 */
#define CU_K_CE		44		/* the number typed taken back */
#define CU_K_BS		45		/* its last digit */
#define CU_K_00		46		/* two zeros */
#define CU_K_SIN	50
#define CU_K_COS	51
#define CU_K_TAN	52
#define CU_K_ASIN	53
#define CU_K_ACOS	54
#define CU_K_ATAN	55
#define CU_K_LOG	56		/* to base 10 */
#define CU_K_LN		57
#define CU_K_EXP10	58		/* 10^x */
#define CU_K_EXPE	59		/* e^x */
#define CU_K_SQR	60		/* x² */
#define CU_K_POW	61		/* x^y */
#define CU_K_INV	62		/* 1/x */
#define CU_K_FACT	63		/* n! */
#define CU_K_PI		64
#define CU_K_LP		65
#define CU_K_RP		66
#define CU_K_ANGLE	67		/* degrees and radians */

#define CU_DEPTH	32		/* operations waiting, brackets among them */
#define CU_ENTRY	24		/* bytes of a number typed */

/*
 * A function worked out elsewhere: key is CU_K_SIN to CU_K_EXPE for a
 * function of *x, or CU_K_POW for *x to the power *y; the answer in *x,
 * rounded to the digits the computation can be trusted with. CU_OK or
 * an error.
 */
typedef INT (*CUFN)( INT key, CUDEC *x, CONST CUDEC *y, BOOL deg );

typedef struct {
	BOOL	sci;			/* the function keys: ranks and brackets */
	BOOL	deg;			/* angles in degrees */
	BOOL	group;			/* the whole part grouped by threes */
	INT	err;			/* an error shown (CU_E*), CU_OK none */
	CUDEC	x;			/* the number shown when none is typed */
	BOOL	typing;			/* a number is being typed in ent */
	char	ent[CU_ENTRY];		/* its digits, a point among them */
	INT	nent;
	BOOL	eneg;
	BOOL	want;			/* an operation was pressed and no number since */
	CUDEC	val[CU_DEPTH];		/* the left operands waiting */
	INT	nval;
	INT	op[CU_DEPTH];		/* the operations waiting, and CU_K_LP */
	INT	nop;
	INT	lastop;			/* what = repeats, 0 nothing */
	CUDEC	lastv;
	CUDEC	mem;
	CUFN	fn;
} CUENG;

IMPORT void cu_init( CUENG *g, BOOL sci, CUFN fn );
IMPORT void cu_key( CUENG *g, INT key );

/* The functions' keys and the plain keys are taken in either mode; this only changes how they rank */
IMPORT void cu_mode( CUENG *g, BOOL sci );

/* The number in the display, as it is shown; the operations waiting, as "12 × ( 3 +" */
IMPORT INT  cu_show( CUENG *g, char *out, INT max );
IMPORT INT  cu_expr( CUENG *g, char *out, INT max );

/* The value the display holds (the number typed, if one is) */
IMPORT void cu_value( CUENG *g, CUDEC *v );

/*
 * The first number in a text, written as a person writes it: full
 * width digits, "," between digits, a full width or a minus sign, an
 * exponent. It becomes the number shown, as MR puts the memory's;
 * FALSE when there is none.
 */
IMPORT BOOL cu_paste( CUENG *g, CONST UB *s, INT n );

/*
 * A key of the keyboard (its HID usage, the modifiers, and the letter
 * the layout gives it) as a calculator key, 0 for none: the digits and
 * + - * / = % ( ) ^ . by their letters, the numeric keypad by its keys,
 * Enter =, Esc 全消去, Backspace the last digit, Delete the number typed.
 */
IMPORT INT  cu_hid_key( UINT code, UINT mods, UB ch );

/* The words for an error */
IMPORT CONST char *cu_err_text( INT err );

/* The functions in double precision, a CUFN (cu_fn.c: the program only, which may use floating point) */
IMPORT INT  cu_fn( INT key, CUDEC *x, CONST CUDEC *y, BOOL deg );

#endif /* __CU_ENG_H__ */
