/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	err.h
 *	Messages to the standard error, the program's name before them
 *	(design 5.8.3; lib/libposix/psx_err.c)
 */

#ifndef _ERR_H_
#define _ERR_H_

#include <stdarg.h>
#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

__dead void	err( int, const char *, ... ) __printflike(2, 3);
__dead void	verr( int, const char *, va_list ) __printflike(2, 0);
__dead void	errc( int, int, const char *, ... ) __printflike(3, 4);
__dead void	verrc( int, int, const char *, va_list ) __printflike(3, 0);
__dead void	errx( int, const char *, ... ) __printflike(2, 3);
__dead void	verrx( int, const char *, va_list ) __printflike(2, 0);
void		warn( const char *, ... ) __printflike(1, 2);
void		vwarn( const char *, va_list ) __printflike(1, 0);
void		warnc( int, const char *, ... ) __printflike(2, 3);
void		vwarnc( int, const char *, va_list ) __printflike(2, 0);
void		warnx( const char *, ... ) __printflike(1, 2);
void		vwarnx( const char *, va_list ) __printflike(1, 0);

#ifdef __cplusplus
}
#endif

#endif /* _ERR_H_ */
