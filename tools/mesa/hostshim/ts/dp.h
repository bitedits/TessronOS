/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ts/dp.h (host)
 *	The rectangles and regions compose.c works with, for the host test
 *	of the GPU's shaders (tools/mesa/test_cp_shaders.c).
 */

#ifndef _HOSTSHIM_TS_DP_H_
#define _HOSTSHIM_TS_DP_H_

#include "kernel.h"

typedef struct {
	INT	left, top, right, bottom;
} T_DPRECT;

typedef struct {
	INT	x, y;
} T_DPPOINT;

typedef struct dp_region T_DPRGN;

IMPORT INT  dp_rgn_row( CONST T_DPRGN *rgn, INT y, CONST INT **p_spans );

#endif /* _HOSTSHIM_TS_DP_H_ */
