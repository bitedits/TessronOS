/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	cu_cases.h
 *	電卓: keys pressed and what the display must then show (design 11.17)
 *
 *	The kernel's test (tests/ktest/ktest_calcu.c) works the cases that
 *	need no double precision through the engine; the program started
 *	with "selftest" works all of them, with its functions. A key is one
 *	letter of the string:
 *
 *	  0-9 . + - * / = % ( ) ^	as on the keys
 *	  n ±   r √   C 全消去   c the number taken back   b its last digit
 *	  M M+  m M−  R MR  Z MC   z 00
 *	  s sin  o cos  t tan  S asin  O acos  T atan
 *	  l log  L ln  x 10^x  X e^x  q x²  i 1/x  ! n!  p π  a degrees/radians
 */

#ifndef __CU_CASES_H__
#define __CU_CASES_H__

#include "cu_eng.h"

typedef struct {
	BOOL		sci;		/* with the function keys' ranks */
	BOOL		fn;		/* needs the functions in double precision */
	CONST char	*keys;
	CONST char	*show;		/* the display */
	CONST char	*expr;		/* the operations waiting, NULL not looked at */
} CUCASE;

LOCAL CONST CUCASE	cu_cases[] = {
	/* decimal: no binary fractions */
	{ 0, 0, "0.1+0.2=",			"0.3",			"" },
	{ 0, 0, "0.3-0.1=",			"0.2",			NULL },
	{ 0, 0, "1.1*1.1=",			"1.21",			NULL },
	{ 0, 0, "0.7+0.1=",			"0.8",			NULL },
	{ 0, 0, "1-0.9=",			"0.1",			NULL },
	{ 0, 0, "4.35*100=",			"435",			NULL },
	{ 0, 0, "10/4=",			"2.5",			NULL },
	{ 0, 0, "1/8=",				"0.125",		NULL },
	{ 0, 0, "1/3*3=",			"1",			NULL },
	{ 0, 0, "2/3=",				"0.66666666667",	NULL },
	{ 0, 0, "0-2/3=",			"-0.66666666667",	NULL },
	{ 0, 0, "0.5/3=",			"0.16666666667",	NULL },

	/* the plain keys do each operation as the next is pressed */
	{ 0, 0, "1+2*3=",			"9",			NULL },
	{ 0, 0, "1+2*",				"3",			"3 ×" },
	{ 0, 0, "5+*3=",			"15",			NULL },
	{ 0, 0, "10-2-3=",			"5",			NULL },

	/* typing, its digits grouped, taken back */
	{ 0, 0, "1234567",			"1,234,567",		NULL },
	{ 0, 0, "1234567.5=",			"1,234,567.5",		NULL },
	{ 0, 0, ".5",				"0.5",			NULL },
	{ 0, 0, "0.0",				"0.0",			NULL },
	{ 0, 0, "007",				"7",			NULL },
	{ 0, 0, "1234b=",			"123",			NULL },
	{ 0, 0, "12.5bb",			"12",			NULL },
	{ 0, 0, "1234567890123",		"123,456,789,012",	NULL },
	{ 0, 0, "5+3c4=",			"9",			NULL },
	{ 0, 0, "5n+3=",			"-2",			NULL },
	{ 0, 0, "7nn",				"7",			NULL },
	{ 0, 0, "1z0+1=",			"1,001",		NULL },

	/* rounding to the display: half away from zero */
	{ 0, 0, "123456789012+0.5=",		"123,456,789,013",	NULL },
	{ 0, 0, "0-123456789012-0.5=",		"-123,456,789,013",	NULL },
	{ 0, 0, "123456789012+0.4=",		"123,456,789,012",	NULL },
	{ 0, 0, "999999999999+1=",		"1E+12",		NULL },
	{ 0, 0, "123456789*987654321=",		"1.219326311E+17",	NULL },
	{ 0, 0, "99999999999*99999999999=",	"1E+22",	NULL },
	{ 0, 0, "1/3000000=",			"3.333333333E-7",	NULL },
	{ 0, 0, "1/1000000=",			"0.000001",		NULL },
	{ 0, 0, "10000000000*10000000000=",	"1E+20",		NULL },

	/* errors, and only a clear goes on */
	{ 0, 0, "5/0=",				"E",			NULL },
	{ 0, 0, "5/0=7",			"E",			NULL },
	{ 0, 0, "5/0=C",			"0",			NULL },
	{ 0, 0, "5/0=c3",			"3",			NULL },
	{ 0, 0, "10000000000*10000000000=========", "E",		NULL },
	{ 0, 0, "9nr",				"E",			NULL },

	/* %, √, = again */
	{ 0, 0, "200+5%",			"210",			"" },
	{ 0, 0, "200-5%",			"190",			NULL },
	{ 0, 0, "200*5%",			"10",			NULL },
	{ 0, 0, "50/200%",			"25",			NULL },
	{ 0, 0, "50%",				"0.5",			NULL },
	{ 0, 0, "2r",				"1.41421356237",	NULL },
	{ 0, 0, "2.25r",			"1.5",			NULL },
	{ 0, 0, "144r",				"12",			NULL },
	{ 0, 0, "2*3==",			"12",			NULL },
	{ 0, 0, "2*3=4=",			"8",			NULL },
	{ 0, 0, "10-3==",			"4",			NULL },
	{ 0, 0, "10/4==",			"0.625",		NULL },

	/* the memory */
	{ 0, 0, "3*4=M10mR",			"2",			NULL },
	{ 0, 0, "3*4M",				"12",			"" },
	{ 0, 0, "3*4MCR",			"12",			NULL },
	{ 0, 0, "5MZR",				"0",			NULL },
	{ 0, 0, "0.1M0.2MR",			"0.3",			NULL },
	{ 0, 0, "7M2+R=",			"9",			NULL },

	/* the function keys' ranks and brackets */
	{ 1, 0, "1+2*3=",			"7",			"" },
	{ 1, 0, "2*(3+4)=",			"14",			NULL },
	{ 1, 0, "2(3+4)=",			"14",			NULL },
	{ 1, 0, "(1+2)*(3+4)=",			"21",			NULL },
	{ 1, 0, "1+2*3^2=",			"19",			NULL },
	{ 1, 0, "2^3^2=",			"512",			NULL },
	{ 1, 0, "2^10=",			"1,024",		NULL },
	{ 1, 0, "10-2-3=",			"5",			NULL },
	{ 1, 0, "2*(3+",			"3",			"2 × ( 3 +" },
	{ 1, 0, "2*(3+4",			"4",			"2 × ( 3 +" },
	{ 1, 0, "2*(3+4)",			"7",			"2 ×" },
	{ 1, 0, "0.5^2=",			"0.25",			NULL },
	{ 1, 0, "2^-",				"2",			"2 −" },
	{ 1, 0, "2^n3=",			"0.125",		NULL },
	{ 1, 0, "1+2*3%=",			"1.06",			NULL },

	/* functions the decimal numbers do exactly */
	{ 1, 0, "90s",				"1",			NULL },
	{ 1, 0, "180s",				"0",			NULL },
	{ 1, 0, "270s",				"-1",			NULL },
	{ 1, 0, "450s",				"1",			NULL },
	{ 1, 0, "90ns",				"-1",			NULL },
	{ 1, 0, "0o",				"1",			NULL },
	{ 1, 0, "90o",				"0",			NULL },
	{ 1, 0, "180o",				"-1",			NULL },
	{ 1, 0, "90t",				"E",			NULL },
	{ 1, 0, "180t",				"0",			NULL },
	{ 1, 0, "100l",				"2",			NULL },
	{ 1, 0, "0.001l",			"-3",			NULL },
	{ 1, 0, "0l",				"E",			NULL },
	{ 1, 0, "1L",				"0",			NULL },
	{ 1, 0, "3x",				"1,000",		NULL },
	{ 1, 0, "2nx",				"0.01",			NULL },
	{ 1, 0, "0X",				"1",			NULL },
	{ 1, 0, "3q",				"9",			NULL },
	{ 1, 0, "4i",				"0.25",			NULL },
	{ 1, 0, "0i",				"E",			NULL },
	{ 1, 0, "5!",				"120",			NULL },
	{ 1, 0, "0!",				"1",			NULL },
	{ 1, 0, "20!",				"2.432902008E+18",	NULL },
	{ 1, 0, "69!",				"1.711224524E+98",	NULL },
	{ 1, 0, "70!",				"E",			NULL },
	{ 1, 0, "2.5!",				"E",			NULL },
	{ 1, 0, "p",				"3.14159265359",	NULL },
	{ 1, 0, "2S",				"E",			NULL },

	/* and those worked out in double precision, rounded to 15 digits */
	{ 1, 1, "30s",				"0.5",			NULL },
	{ 1, 1, "45t",				"1",			NULL },
	{ 1, 1, "60o",				"0.5",			NULL },
	{ 1, 1, "1S",				"90",			NULL },
	{ 1, 1, "0.5O",				"60",			NULL },
	{ 1, 1, "1T",				"45",			NULL },
	{ 1, 1, "aps",				"0",			NULL },
	{ 1, 1, "apo",				"-1",			NULL },
	{ 1, 1, "a1T*4=",			"3.14159265359",	NULL },
	{ 1, 1, "a1S",				"1.57079632679",	NULL },
	{ 1, 1, "2l",				"0.30102999566",	NULL },
	{ 1, 1, "1X",				"2.71828182846",	NULL },
	{ 1, 1, "1XL",				"1",			NULL },
	{ 1, 1, "0.5x",				"3.16227766017",	NULL },
	{ 1, 1, "2^0.5=",			"1.41421356237",	NULL },
	{ 1, 1, "8n^0.5=",			"E",			NULL },
	{ 1, 1, "1000X",			"E",			NULL },
};

/* A letter of a case as a key */
LOCAL INT cu_case_key( char c )
{
	LOCAL CONST char	*CONST map = "nrCcbMmRZzsoStOTlLxXqi!pa+-*/=%()^.";
	LOCAL CONST INT		keys[] = {
		CU_K_NEG, CU_K_SQRT, CU_K_AC, CU_K_CE, CU_K_BS, CU_K_MPLUS, CU_K_MMINUS, CU_K_MR, CU_K_MC,
		CU_K_00, CU_K_SIN, CU_K_COS, CU_K_ASIN, CU_K_TAN, CU_K_ACOS, CU_K_ATAN, CU_K_LOG, CU_K_LN,
		CU_K_EXP10, CU_K_EXPE, CU_K_SQR, CU_K_INV, CU_K_FACT, CU_K_PI, CU_K_ANGLE, CU_K_ADD,
		CU_K_SUB, CU_K_MUL, CU_K_DIV, CU_K_EQ, CU_K_PCT, CU_K_LP, CU_K_RP, CU_K_POW, CU_K_POINT
	};
	INT	i;

	if ( c >= '0' && c <= '9' ) return CU_K_DIGIT(c - '0');
	for ( i = 0; map[i] != 0; i++ ) {
		if ( map[i] == c ) return keys[i];
	}
	return 0;
}

/*
 * The case worked from a calculator just made: the display and the
 * operations waiting written to show and expr (64 bytes each)
 */
LOCAL void cu_case_run( CONST CUCASE *k, CUFN fn, char *show, char *expr )
{
	CUENG	g;
	INT	i, key;

	cu_init(&g, k->sci, fn);
	for ( i = 0; k->keys[i] != 0; i++ ) {
		key = cu_case_key(k->keys[i]);
		if ( key != 0 ) cu_key(&g, key);
	}
	(void)cu_show(&g, show, 64);
	(void)cu_expr(&g, expr, 64);
}

#endif /* __CU_CASES_H__ */
