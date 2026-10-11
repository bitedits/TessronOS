/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	time.h
 *	newlib's, with sleeping on a clock and the time of a broken-down
 *	UTC date (design 5.8.3). The operations on struct timespec of
 *	sys/time.h come with it, as the BSD commands expect.
 */

#ifndef __POSIX_TIME_H__
#define __POSIX_TIME_H__

#include_next <time.h>

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

int	clock_nanosleep( clockid_t, int, const struct timespec *, struct timespec * );
time_t	timegm( struct tm * );
time_t	timelocal( struct tm * );

/*
 * Time zones as values: NULL is UTC. A zone made by tzalloc is UTC when
 * named "UTC", "GMT" or "UTC0", and otherwise the zone of TZ.
 */
typedef struct __psx_tz	*timezone_t;
timezone_t tzalloc( const char * );
void	tzfree( timezone_t );
struct tm *localtime_rz( timezone_t, const time_t *, struct tm * );
time_t	mktime_z( timezone_t, struct tm * );
size_t	strftime_z( timezone_t, char *, size_t, const char *, const struct tm * );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_TIME_H__ */
