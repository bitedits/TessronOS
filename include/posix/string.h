/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	string.h
 *	newlib's, with the BSD functions the commands use (design 5.8.3)
 */

#ifndef __POSIX_STRING_H__
#define __POSIX_STRING_H__

#include_next <string.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void	strmode( mode_t, char * );
void	*explicit_memset( void *, int, size_t );
int	consttime_memequal( const void *, const void *, size_t );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_STRING_H__ */
