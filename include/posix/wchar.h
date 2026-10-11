/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	wchar.h
 *	newlib's, with BSD's reading of a line of wide characters (design
 *	5.8.3)
 */

#ifndef __POSIX_WCHAR_H__
#define __POSIX_WCHAR_H__

#include_next <wchar.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

wchar_t	*fgetwln( FILE *, size_t * );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_WCHAR_H__ */
