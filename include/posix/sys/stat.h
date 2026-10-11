/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/stat.h
 *	struct stat of include/posix (design 5.8.3)
 *
 *	newlib's members, in its order, with the wider numbers of
 *	machine/_types.h and newlib's spare words given the names a BSD
 *	program looks for: st_flags and st_gen. st_birthtime is the time of
 *	the last change of the status, which the system does not keep apart.
 */

#ifndef _SYS_STAT_H
#define _SYS_STAT_H
#define _STAT_H_

#include <_ansi.h>
#include <time.h>
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/_timespec.h>

#ifdef __cplusplus
extern "C" {
#endif

struct stat {
	dev_t		st_dev;
	ino_t		st_ino;
	mode_t		st_mode;
	nlink_t		st_nlink;
	uid_t		st_uid;
	gid_t		st_gid;
	dev_t		st_rdev;
	off_t		st_size;
	struct timespec	st_atim;
	struct timespec	st_mtim;
	struct timespec	st_ctim;
	blksize_t	st_blksize;
	blkcnt_t	st_blocks;
	__uint32_t	st_flags;
	__uint32_t	st_gen;
	long		st_spare;
};

#define st_atime		st_atim.tv_sec
#define st_mtime		st_mtim.tv_sec
#define st_ctime		st_ctim.tv_sec
#define st_birthtime		st_ctim.tv_sec
#define st_atimespec		st_atim
#define st_mtimespec		st_mtim
#define st_ctimespec		st_ctim
#define st_birthtimespec	st_ctim
#define st_atimensec		st_atim.tv_nsec
#define st_mtimensec		st_mtim.tv_nsec
#define st_ctimensec		st_ctim.tv_nsec
#define st_birthtimensec	st_ctim.tv_nsec

#define	_IFMT		0170000
#define	_IFDIR		0040000
#define	_IFCHR		0020000
#define	_IFBLK		0060000
#define	_IFREG		0100000
#define	_IFLNK		0120000
#define	_IFSOCK		0140000
#define	_IFIFO		0010000

#define S_IFMT		_IFMT
#define S_IFDIR		_IFDIR
#define S_IFCHR		_IFCHR
#define S_IFBLK		_IFBLK
#define S_IFREG		_IFREG
#define S_IFLNK		_IFLNK
#define S_IFSOCK	_IFSOCK
#define S_IFIFO		_IFIFO
#define S_IFWHT		0160000

#define S_ISUID		0004000
#define S_ISGID		0002000
#define S_ISVTX		0001000
#define S_ISTXT		S_ISVTX
#define S_IREAD		0000400
#define S_IWRITE	0000200
#define S_IEXEC		0000100
#define S_ENFMT		0002000

#define S_IRWXU		0000700
#define S_IRUSR		0000400
#define S_IWUSR		0000200
#define S_IXUSR		0000100
#define S_IRWXG		0000070
#define S_IRGRP		0000040
#define S_IWGRP		0000020
#define S_IXGRP		0000010
#define S_IRWXO		0000007
#define S_IROTH		0000004
#define S_IWOTH		0000002
#define S_IXOTH		0000001

#define ACCESSPERMS	( S_IRWXU | S_IRWXG | S_IRWXO )
#define ALLPERMS	( S_ISUID | S_ISGID | S_ISTXT | S_IRWXU | S_IRWXG | S_IRWXO )
#define DEFFILEMODE	( S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH )
#define S_BLKSIZE	512

#define S_ISBLK(m)	( ( (m) & _IFMT ) == _IFBLK )
#define S_ISCHR(m)	( ( (m) & _IFMT ) == _IFCHR )
#define S_ISDIR(m)	( ( (m) & _IFMT ) == _IFDIR )
#define S_ISFIFO(m)	( ( (m) & _IFMT ) == _IFIFO )
#define S_ISREG(m)	( ( (m) & _IFMT ) == _IFREG )
#define S_ISLNK(m)	( ( (m) & _IFMT ) == _IFLNK )
#define S_ISSOCK(m)	( ( (m) & _IFMT ) == _IFSOCK )
#define S_ISWHT(m)	( ( (m) & _IFMT ) == S_IFWHT )

/* st_flags: the system sets none of them */
#define UF_SETTABLE	0x0000ffff
#define UF_NODUMP	0x00000001
#define UF_IMMUTABLE	0x00000002
#define UF_APPEND	0x00000004
#define UF_OPAQUE	0x00000008
#define SF_SETTABLE	0xffff0000
#define SF_ARCHIVED	0x00010000
#define SF_IMMUTABLE	0x00020000
#define SF_APPEND	0x00040000

#define UTIME_NOW	-2
#define UTIME_OMIT	-1

int	chmod( const char *, mode_t );
int	fchmod( int, mode_t );
int	lchmod( const char *, mode_t );
int	fstat( int, struct stat * );
int	lstat( const char *, struct stat * );
int	stat( const char *, struct stat * );
int	fstatat( int, const char *, struct stat *, int );
int	mkdir( const char *, mode_t );
int	mkdirat( int, const char *, mode_t );
int	mkfifo( const char *, mode_t );
int	mknod( const char *, mode_t, dev_t );
mode_t	umask( mode_t );
int	chflags( const char *, unsigned long );
int	lchflags( const char *, unsigned long );
int	fchflags( int, unsigned long );
int	futimens( int, const struct timespec[2] );
int	utimensat( int, const char *, const struct timespec[2], int );
int	utimens( const char *, const struct timespec[2] );
int	lutimens( const char *, const struct timespec[2] );
int	fchmodat( int, const char *, mode_t, int );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_STAT_H */
