/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	pwd.h
 *	newlib's, with BSD's names of users kept by their numbers (design
 *	5.8.3). The users are answered by lib/libposix from the system's
 *	credentials: there is no file of them.
 */

#ifndef __POSIX_PWD_H__
#define __POSIX_PWD_H__

#include_next <pwd.h>

#ifdef __cplusplus
extern "C" {
#endif

const char	*user_from_uid( uid_t, int );
int		uid_from_user( const char *, uid_t * );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_PWD_H__ */
