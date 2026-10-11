/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	endian.h
 *	The byte order names of the C library's <endian.h>, which newlib
 *	keeps in <machine/endian.h> under other names. Mesa's util/u_endian.h
 *	reads __BYTE_ORDER from here.
 */

#ifndef TS_SHIM_ENDIAN_H
#define TS_SHIM_ENDIAN_H

#include <machine/endian.h>

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN	__ORDER_LITTLE_ENDIAN__
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN	__ORDER_BIG_ENDIAN__
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER	__BYTE_ORDER__
#endif

#endif /* TS_SHIM_ENDIAN_H */
