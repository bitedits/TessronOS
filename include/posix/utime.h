/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	utime.h
 *	newlib's struct utimbuf, with the call that sets the times
 *	(design 5.8.3)
 */

#ifndef __POSIX_UTIME_H__
#define __POSIX_UTIME_H__

#include <sys/types.h>
#include_next <utime.h>

#ifdef __cplusplus
extern "C" {
#endif

int	utime( const char *, const struct utimbuf * );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_UTIME_H__ */
