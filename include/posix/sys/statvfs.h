/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/statvfs.h
 *	How big a file system is and how much of it is free (design 5.8.3):
 *	what the file layer says of a mount (fs_statvfs), and its place.
 */

#ifndef _SYS_STATVFS_H_
#define _SYS_STATVFS_H_

#include <sys/types.h>

#define _VFS_NAMELEN	32
#define _VFS_MNAMELEN	1024
#define VFS_NAMELEN	_VFS_NAMELEN
#define VFS_MNAMELEN	_VFS_MNAMELEN

typedef struct { int __fsid_val[2]; } fsid_t;

struct statvfs {
	unsigned long	f_flag;
	unsigned long	f_bsize;
	unsigned long	f_frsize;
	unsigned long	f_iosize;
	fsblkcnt_t	f_blocks;
	fsblkcnt_t	f_bfree;
	fsblkcnt_t	f_bavail;
	fsblkcnt_t	f_bresvd;
	fsfilcnt_t	f_files;
	fsfilcnt_t	f_ffree;
	fsfilcnt_t	f_favail;
	fsfilcnt_t	f_fresvd;
	unsigned long	f_syncreads, f_syncwrites, f_asyncreads, f_asyncwrites;
	fsid_t		f_fsidx;
	unsigned long	f_fsid;
	unsigned long	f_namemax;
	uid_t		f_owner;
	char		f_fstypename[_VFS_NAMELEN];
	char		f_mntonname[_VFS_MNAMELEN];
	char		f_mntfromname[_VFS_MNAMELEN];
	char		f_mntfromlabel[_VFS_MNAMELEN];
};

#define ST_RDONLY	0x00000001
#define ST_NOSUID	0x00000008
#define ST_LOCAL	0x00001000

#define MNT_RDONLY	ST_RDONLY
#define MNT_LOCAL	ST_LOCAL
#define MNT_NOSUID	ST_NOSUID
#define MNT_IGNORE	0x00100000	/* not shown by df */
#define MNT_WAIT	1
#define MNT_NOWAIT	2
#define MNT_LAZY	3

#ifdef __cplusplus
extern "C" {
#endif

int	statvfs( const char *, struct statvfs * );
int	fstatvfs( int, struct statvfs * );
int	statvfs1( const char *, struct statvfs *, int );
int	fstatvfs1( int, struct statvfs *, int );
int	getvfsstat( struct statvfs *, size_t, int );
int	getmntinfo( struct statvfs **, int );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_STATVFS_H_ */
