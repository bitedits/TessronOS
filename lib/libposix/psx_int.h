/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	psx_int.h
 *	What the parts of lib/libposix share (design 5.8.3)
 *
 *	lib/libposix is the C library's lower half for the commands and
 *	the shell: newlib's system calls (_read, _write, ...) and the POSIX
 *	calls newlib leaves to the system, on the kernel's file layer (fs_),
 *	its POSIX process calls (include/ts/psx.h) and the objects (ob_).
 *
 *	A POSIX descriptor (0 to PSX_FD_MAX - 1) is an entry of the table
 *	here: a descriptor of the file layer, or the console of the system
 *	for a program started without any (its 1 and 2 go to tm_putstring,
 *	its 0 is at its end). The table is the process's own; what a child
 *	is to have is handed to it with ts_spn_prc and the start-up
 *	argument (psx_start.c says the form).
 */

#ifndef __PSX_INT_H__
#define __PSX_INT_H__

#include <tk/typedef.h>
#include <tk/errno.h>
#include <ts/fs.h>
#include <ts/psx.h>

#define PSX_FD_MAX	256

/* What an entry is */
#define PK_NONE		0
#define PK_FS		1		/* kfd: a descriptor of the file layer */
#define PK_CONOUT	2		/* the system's console, written with tm_putstring */
#define PK_CONIN	3		/* nothing to read: the end of the file at once */

typedef struct {
	unsigned char	kind;		/* PK_* */
	unsigned char	cloexec;
	int		kfd;
	char		*path;		/* the absolute name it was opened by, or NULL */
} PSXFD;

extern PSXFD	__psx_fd[PSX_FD_MAX];

/* The kernel's error as errno: fs_'s negated errno values and the core's E_ codes */
extern int	__psx_errno_fs( int er );
extern int	__psx_errno_er( int er );
extern int	__psx_fail_fs( int er );	/* errno set, -1 back */
extern int	__psx_fail_er( int er );

/* The entry of a descriptor, NULL with EBADF when it is not open */
extern PSXFD	*__psx_fd_get( int fd );
/* The lowest free descriptor from 'from' on; -1 with EMFILE */
extern int	__psx_fd_alloc( int from );
/* A kernel descriptor given an entry; the entry's number, or -1 (the kernel's closed then) */
extern int	__psx_fd_new( int kfd, int from, int cloexec );

/* The path as the file layer takes it: absolute, without "." and ".." (PATH_MAX bytes) */
extern int	__psx_path( const char *path, char *out );
extern int	__psx_path_at( int dirfd, const char *path, char *out );
extern char	__psx_cwd[];

/* What the start-up gave: umask, and the program's name */
extern unsigned int __psx_umask;
extern const char *__psx_progname;

/* stat as the commands see it, from the file layer's */
struct stat;
extern void	__psx_stat_of( const T_FSXSTAT *x, struct stat *st );

/* Signals: the handlers, called on the main task as a call returns (psx_sig.c) */
extern void	__psx_sig_init( void );
extern int	__psx_restart( void );		/* a call cut short is made again (SA_RESTART) */
extern void	__psx_tz_init( void );

/* The descriptors and the environment a spawn hands on (psx_spawn.c) */
extern char	**environ;

#endif /* __PSX_INT_H__ */
