/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	fcntl.h
 *	newlib's, with what it leaves out of POSIX and BSD (design 5.8.3)
 */

#ifndef __POSIX_FCNTL_H__
#define __POSIX_FCNTL_H__

#include_next <fcntl.h>

#ifndef F_DUPFD_CLOEXEC
#define F_DUPFD_CLOEXEC		14
#endif
#ifndef F_CLOSEM
#define F_CLOSEM		10
#endif
#ifndef F_MAXFD
#define F_MAXFD			11	/* the highest descriptor open */
#endif
#ifndef AT_FDCWD
#define AT_FDCWD		-100
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW	0x200
#endif
#ifndef AT_SYMLINK_FOLLOW
#define AT_SYMLINK_FOLLOW	0x400
#endif
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR		0x800
#endif
#ifndef AT_EACCESS
#define AT_EACCESS		0x100
#endif
#ifndef O_SHLOCK
#define O_SHLOCK		0
#define O_EXLOCK		0
#endif
#ifndef O_ALT_IO
#define O_ALT_IO		0
#endif
#ifndef FREAD
#define FREAD			0x0001
#define FWRITE			0x0002
#endif

#ifdef __cplusplus
extern "C" {
#endif
int	openat( int, const char *, int, ... );
#ifdef __cplusplus
}
#endif

#endif /* __POSIX_FCNTL_H__ */
