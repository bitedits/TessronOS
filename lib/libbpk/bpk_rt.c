/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	bpk_rt.c
 *	The elements of 実時間制御TAD and the segments they are (design 15.7)
 *
 *	One table serves both ways, binary TAD to xmlTAD (bpk_tad.c) and
 *	back (lib/libbtbk): an element's segment id and sub id, and the
 *	segment that ends it when elements may stand inside it. An element
 *	the table does not name is written as BPK_RT_ELEM with its name.
 */

#include <ts/bpk.h>

#define NOEND	BPK_RT_NOEND

LOCAL const BPKRTEL rt_el[] = {
	/* 管理 */
	{ "realData",		TS_REAL,	0,	0,	TS_REALEND },
	{ "stream",		TS_REAL,	1,	NOEND,	TS_REAL },
	{ "realtime",		TS_REAL,	2,	2,	TS_REALEND },
	/* グループ */
	{ "realGroup",		TS_RGRP,	0,	1,	TS_RGRP },	/* parallel */
	{ "realGroup",		TS_RGRP,	2,	3,	TS_RGRP },	/* serial */
	{ "eventArray",		TS_RGRP,	4,	5,	TS_RGRP },
	/* 時間 */
	{ "timeDelta",		TS_RTIME,	0,	NOEND,	TS_REAL },
	{ "timeAbsolute",	TS_RTIME,	1,	NOEND,	TS_REAL },
	/* 分岐・繰り返し */
	{ "label",		TS_BRLP,	0,	NOEND,	TS_REAL },
	{ "jump",		TS_BRLP,	1,	NOEND,	TS_REAL },
	{ "if",			TS_BRLP,	2,	4,	TS_BRLP },
	{ "elseif",		TS_BRLP,	3,	NOEND,	TS_REAL },
	{ "while",		TS_BRLP,	5,	6,	TS_BRLP },
	{ "assign",		TS_BRLP,	7,	NOEND,	TS_REAL },
	{ "random",		TS_BRLP,	8,	9,	TS_BRLP },
	{ "else",		TS_BRLP,	10,	NOEND,	TS_REAL },
	/* 補間 */
	{ "interpolate",	TS_RGRD,	0,	1,	TS_RGRD },
	/* 単位系 */
	{ "unitSpec",		TS_SUNIT,	0,	NOEND,	TS_REAL },
	{ "unitSpecTemp",	TS_SUNIT,	1,	NOEND,	TS_REAL },
	{ "unitSystem",		TS_SUNIT,	2,	NOEND,	TS_REAL },
	{ "unitScale",		TS_SUNIT,	3,	NOEND,	TS_REAL },
	{ "absoluteUnitCoeff",	TS_SUNIT,	4,	NOEND,	TS_REAL },
	/* 音 */
	{ "note",		TS_RSND,	0,	NOEND,	TS_REAL },
	{ "midi",		TS_RSND,	1,	NOEND,	TS_REAL },
	{ "pcm",		TS_RSND,	2,	NOEND,	TS_REAL },
	/* メディア */
	{ "audio",		TS_RMEDIA,	0,	NOEND,	TS_REAL },
	{ "video",		TS_RMEDIA,	1,	NOEND,	TS_REAL },
	{ "image",		TS_RMEDIA,	2,	NOEND,	TS_REAL },
	{ "figureRef",		TS_RMEDIA,	4,	NOEND,	TS_REAL },
	/* メディア制御 */
	{ "audioControl",	TS_RMCTL,	0,	NOEND,	TS_REAL },
	{ "videoControl",	TS_RMCTL,	1,	NOEND,	TS_REAL },
	{ "imageControl",	TS_RMCTL,	2,	NOEND,	TS_REAL },
	{ "textControl",	TS_RMCTL,	3,	NOEND,	TS_REAL },
	{ "shapeControl",	TS_RMCTL,	4,	NOEND,	TS_REAL },
	{ "groupControl",	TS_RMCTL,	5,	NOEND,	TS_REAL },
	{ "triggerControl",	TS_RMCTL,	6,	NOEND,	TS_REAL },
	/* 待機、アニメーション、トリガー、エラー処理 */
	{ "wait",		TS_RWAIT,	0,	NOEND,	TS_REAL },
	{ "keyframes",		TS_RANIM,	0,	1,	TS_RANIM },
	{ "keyframe",		TS_RANIM,	2,	NOEND,	TS_REAL },
	{ "trigger",		TS_RTRIG,	0,	1,	TS_RTRIG },
	{ "onerror",		TS_RERR,	0,	1,	TS_RERR },
	{ "fallback",		TS_RERR,	2,	NOEND,	TS_REAL },
};
#define N_RT_EL	( (INT)( sizeof(rt_el) / sizeof(rt_el[0]) ) )

LOCAL BOOL name_is( const char *s, INT n, const char *w )
{
	INT	i;

	for ( i = 0; i < n; i++ ) {
		if ( w[i] == 0 || s[i] != w[i] ) return FALSE;
	}
	return (BOOL)( w[n] == 0 );
}

EXPORT const BPKRTEL *bpk_rt_by_name( const char *name, INT nlen, BOOL serial )
{
	INT	i;

	for ( i = 0; i < N_RT_EL; i++ ) {
		if ( !name_is(name, nlen, rt_el[i].name) ) continue;
		if ( rt_el[i].id == TS_RGRP && rt_el[i].sub < 4 && serial ) return &rt_el[i + 1];
		return &rt_el[i];
	}
	return NULL;
}

EXPORT const BPKRTEL *bpk_rt_by_seg( UINT id, UINT sub )
{
	INT	i;

	for ( i = 0; i < N_RT_EL; i++ ) {
		if ( rt_el[i].id == id && rt_el[i].sub == sub ) return &rt_el[i];
	}
	return NULL;
}

/*
 * The elements of a figure or a text the program of 実時間制御TAD names
 * by id (a shape, a picture, a text box and its text, a group): binary
 * TAD has no place for the id, so a segment of its own carries it
 */
EXPORT BOOL bpk_rt_id_kept( const char *name, INT nlen )
{
	static const char *const kept[] = {
		"rect", "ellipse", "arc", "chord", "polygon", "line", "polyline", "curve",
		"elliptical_arc", "freefig", "image", "pixelmap", "text", "document", "group", NULL
	};
	INT	i;

	for ( i = 0; kept[i] != NULL; i++ ) {
		if ( name_is(name, nlen, kept[i]) ) return TRUE;
	}
	return FALSE;
}

EXPORT BOOL bpk_rt_is_end( UINT id, UINT sub )
{
	INT	i;

	if ( id == TS_REALEND || ( id == TS_REAL && sub == BPK_RT_CLOSE ) ) return TRUE;
	for ( i = 0; i < N_RT_EL; i++ ) {
		if ( rt_el[i].end != NOEND && rt_el[i].endid == id && rt_el[i].end == sub ) return TRUE;
	}
	return FALSE;
}

EXPORT BOOL bpk_rt_seg( UINT id, BOOL inside )
{
	switch ( id ) {
	case TS_RSND: case TS_RWAIT: case TS_RANIM: case TS_RTRIG: case TS_RERR:
	case TS_RGRP: case TS_RTIME: case TS_BRLP: case TS_RGRD:
	case TS_REAL: case TS_REALEND: case TS_SUNIT:
		return TRUE;
	case TS_RMEDIA: case TS_RMCTL:
		return inside;
	default:
		return FALSE;
	}
}
