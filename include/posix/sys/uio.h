/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/uio.h
 *	Reading and writing several pieces at once (design 5.8.2)
 */

#ifndef _SYS_UIO_H_
#define _SYS_UIO_H_

#include <sys/types.h>

struct iovec {
	void	*iov_base;
	size_t	iov_len;
};

#define IOV_MAX		1024
#define UIO_MAXIOV	IOV_MAX

#ifdef __cplusplus
extern "C" {
#endif

ssize_t	readv( int, const struct iovec *, int );
ssize_t	writev( int, const struct iovec *, int );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_UIO_H_ */
