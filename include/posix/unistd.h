/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	unistd.h
 *	newlib's, with the BSD calls it leaves out (design 5.8.3)
 */

#ifndef __POSIX_UNISTD_H__
#define __POSIX_UNISTD_H__

#include_next <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

void	*setmode( const char * );
mode_t	getmode( const void *, mode_t );
void	closefrom( int );
int	getgrouplist( const char *, gid_t, gid_t *, int * );
int	pipe2( int [2], int );
pid_t	tcgetpgrp( int );
int	tcsetpgrp( int, pid_t );
int	lchmod( const char *, mode_t );
int	setlogin( const char * );
int	eaccess( const char *, int );
int	faccessat( int, const char *, int, int );
int	unlinkat( int, const char *, int );
int	fchdir( int );
int	getpeereid( int, uid_t *, gid_t * );
ssize_t	readlinkat( int, const char *, char *, size_t );
int	linkat( int, const char *, int, const char *, int );
int	symlinkat( const char *, int, const char * );
int	renameat( int, const char *, int, const char * );
int	fchownat( int, const char *, uid_t, gid_t, int );
char	*getusershell( void );
void	setusershell( void );
void	endusershell( void );
int	swapctl( int, void *, int );
int	undelete( const char * );
int	ioctl( int, unsigned long, ... );	/* also in sys/ioctl.h */
int	fchroot( int );
int	pledge( const char *, const char * );	/* the calls a program will make: not narrowed */
int	unveil( const char *, const char * );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_UNISTD_H__ */
