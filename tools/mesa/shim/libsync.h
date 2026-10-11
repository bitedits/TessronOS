/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	libsync.h
 *	Waiting on and merging sync_file descriptors, the fences Linux
 *	passes around as descriptors. On TessronOS such a descriptor lives
 *	in the process only: the DRM shim (lib/libgl/gl_drm.c) makes it
 *	from a synchronisation object, and it carries the fence number of
 *	the job it waits for (13.6.2).
 */

#ifndef TS_SHIM_LIBSYNC_H
#define TS_SHIM_LIBSYNC_H

#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

int	ts_gl_sync_wait( int fd, int timeout );
int	ts_gl_sync_merge( int fd1, int fd2 );
int	ts_gl_sync_accumulate( int *fd1, int fd2 );

#ifdef __cplusplus
}
#endif

/* Wait 'timeout' ms (-1: for ever) for the fence of a descriptor */
static inline int sync_wait( int fd, int timeout )
{
	return ts_gl_sync_wait(fd, timeout);
}

/* A new descriptor that is over when both are */
static inline int sync_merge( const char *name, int fd1, int fd2 )
{
	(void)name;
	return ts_gl_sync_merge(fd1, fd2);
}

/* *fd1 replaced with a descriptor that is over when it and fd2 are (*fd1 < 0: a copy of fd2) */
static inline int sync_accumulate( const char *name, int *fd1, int fd2 )
{
	(void)name;
	return ts_gl_sync_accumulate(fd1, fd2);
}

#endif /* TS_SHIM_LIBSYNC_H */
