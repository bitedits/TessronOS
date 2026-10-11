/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	psx_path.c
 *	Paths, the place a program is in, and what is named by a path
 *	(lib/libposix, design 5.8.1)
 *
 *	The file layer takes a path only whole and plain: from the root,
 *	without "." or ".." or an empty name. A program's place is kept
 *	here, and each path it gives is put together with the place and
 *	made plain before it goes down; there are no symbolic links, so
 *	doing it by the letters of the path is right.
 */

#include "psx_int.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <utime.h>

#define TRON_EPOCH	473385600LL	/* 1985-01-01 in seconds since 1970-01-01 */

char	__psx_cwd[PATH_MAX] = "/";

int __psx_path( const char *path, char *out )
{
	char	tmp[PATH_MAX * 2];
	size_t	n = 0, k;
	const char *s;
	char	*o = out;

	if ( path == NULL ) {
		errno = EFAULT;
		return -1;
	}
	if ( path[0] == 0 ) {
		errno = ENOENT;
		return -1;
	}
	if ( path[0] != '/' ) {
		k = strlen(__psx_cwd);
		memcpy(tmp, __psx_cwd, k);
		n = k;
		tmp[n++] = '/';
	}
	k = strlen(path);
	if ( n + k >= sizeof(tmp) ) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(tmp + n, path, k + 1);

	/* name by name: "." dropped, ".." takes the one before away */
	*o = 0;
	n = 0;
	for ( s = tmp; *s != 0; ) {
		const char *e;

		while ( *s == '/' ) s++;
		if ( *s == 0 ) break;
		for ( e = s; *e != 0 && *e != '/'; e++ ) ;
		k = (size_t)( e - s );
		if ( k == 1 && s[0] == '.' ) {
			/* here */
		} else if ( k == 2 && s[0] == '.' && s[1] == '.' ) {
			while ( n > 0 && out[n - 1] != '/' ) n--;
			if ( n > 0 ) n--;
		} else {
			if ( n + 1 + k >= PATH_MAX ) {
				errno = ENAMETOOLONG;
				return -1;
			}
			out[n++] = '/';
			memcpy(out + n, s, k);
			n += k;
		}
		s = e;
	}
	if ( n == 0 ) out[n++] = '/';
	out[n] = 0;

	/* the FAT volumes the file layer mounts are not in the tree of objects */
	if ( ( strncmp(out, "/boot", 5) == 0 && ( out[5] == 0 || out[5] == '/' ) )
	  || ( strncmp(out, "/media", 6) == 0 && ( out[6] == 0 || out[6] == '/' ) ) ) {
		errno = ENOENT;
		return -1;
	}
	return 0;
}

/* A name taken from a directory a descriptor is open on */
int __psx_path_at( int dirfd, const char *path, char *out )
{
	char	tmp[PATH_MAX * 2];
	PSXFD	*e;
	size_t	a, b;

	if ( path == NULL ) {
		errno = EFAULT;
		return -1;
	}
	if ( dirfd == AT_FDCWD || path[0] == '/' ) return __psx_path(path, out);
	e = __psx_fd_get(dirfd);
	if ( e == NULL ) return -1;
	if ( e->path == NULL ) {
		errno = ENOTDIR;
		return -1;
	}
	a = strlen(e->path);
	b = strlen(path);
	if ( a + 1 + b >= sizeof(tmp) ) {
		errno = ENAMETOOLONG;
		return -1;
	}
	memcpy(tmp, e->path, a);
	tmp[a] = '/';
	memcpy(tmp + a + 1, path, b + 1);
	return __psx_path(tmp, out);
}

#define AT_PATH(dirfd, path)					\
	char	at_[PATH_MAX];					\
	if ( __psx_path_at(dirfd, path, at_) < 0 ) return -1;	\
	path = at_;

/* ---------------------------------------------------------------- stat */

static dev_t dev_of( const TS_UUID *u )
{
	unsigned int	h = 2166136261u;
	int		i, zero = 1;

	for ( i = 0; i < 16; i++ ) {
		if ( u->b[i] != 0 ) zero = 0;
		h = ( h ^ u->b[i] ) * 16777619u;
	}
	return zero ? 1 : (dev_t)( h | 0x100 );
}

void __psx_stat_of( const T_FSXSTAT *x, struct stat *st )
{
	memset(st, 0, sizeof(*st));
	st->st_dev = dev_of(&x->st.dev);
	st->st_ino = (ino_t)x->st.ino;
	st->st_mode = (mode_t)x->st.mode;
	st->st_nlink = (nlink_t)( ( x->nlink > 0 ) ? x->nlink : 1 );
	st->st_uid = (uid_t)x->uid;
	st->st_gid = (gid_t)x->gid;
	st->st_rdev = (dev_t)x->rdev;
	st->st_size = (off_t)x->st.size;
	st->st_mtim.tv_sec = (time_t)( (long long)x->st.mtime + TRON_EPOCH );
	st->st_atim.tv_sec = (time_t)( (long long)( x->atime ? x->atime : x->st.mtime ) + TRON_EPOCH );
	st->st_ctim.tv_sec = (time_t)( (long long)( x->ctime ? x->ctime : x->st.mtime ) + TRON_EPOCH );
	st->st_blksize = ( x->blksize > 0 ) ? x->blksize : 4096;
	st->st_blocks = (blkcnt_t)( ( x->st.size + 511 ) / 512 );
}

int stat( const char *path, struct stat *st )
{
	char		p[PATH_MAX];
	T_FSXSTAT	x;
	ER		er;

	if ( __psx_path(path, p) < 0 ) return -1;
	er = fs_xstat(p, &x);
	if ( er < 0 ) return __psx_fail_fs(er);
	__psx_stat_of(&x, st);
	return 0;
}

int lstat( const char *path, struct stat *st )
{
	return stat(path, st);
}

int fstatat( int dirfd, const char *path, struct stat *st, int flags )
{
	AT_PATH(dirfd, path)
	(void)flags;
	return stat(path, st);
}

/* ---------------------------------------------------------------- the place */

char *getcwd( char *buf, size_t size )
{
	size_t	n = strlen(__psx_cwd) + 1;

	if ( buf == NULL ) {
		if ( size < n ) size = n;
		buf = (char *)malloc(size);
		if ( buf == NULL ) {
			errno = ENOMEM;
			return NULL;
		}
	} else if ( size == 0 ) {
		errno = EINVAL;
		return NULL;
	}
	if ( size < n ) {
		errno = ERANGE;
		return NULL;
	}
	memcpy(buf, __psx_cwd, n);
	return buf;
}

char *getwd( char *buf )
{
	return getcwd(buf, PATH_MAX);
}

int chdir( const char *path )
{
	char		p[PATH_MAX];
	T_FSXSTAT	x;
	ER		er;

	if ( __psx_path(path, p) < 0 ) return -1;
	er = fs_xstat(p, &x);
	if ( er < 0 ) return __psx_fail_fs(er);
	if ( ( x.st.mode & FS_IFMT ) != FS_IFDIR ) {
		errno = ENOTDIR;
		return -1;
	}
	strcpy(__psx_cwd, p);
	return 0;
}

int fchdir( int fd )
{
	PSXFD	*e = __psx_fd_get(fd);

	if ( e == NULL ) return -1;
	if ( e->path == NULL ) {
		errno = ENOTDIR;
		return -1;
	}
	return chdir(e->path);
}

/* ---------------------------------------------------------------- names */

int access( const char *path, int mode )
{
	struct stat	st;
	uid_t		me = geteuid();
	mode_t		bits = 0;

	if ( stat(path, &st) < 0 ) return -1;
	if ( mode == F_OK ) return 0;
	if ( me == 0 ) {
		if ( ( mode & X_OK ) == 0 || ( st.st_mode & 0111 ) != 0 || S_ISDIR(st.st_mode) ) return 0;
		errno = EACCES;
		return -1;
	}
	if ( st.st_uid == me ) {
		bits = ( st.st_mode >> 6 ) & 7;
	} else if ( st.st_gid == getegid() ) {
		bits = ( st.st_mode >> 3 ) & 7;
	} else {
		bits = st.st_mode & 7;
	}
	if ( ( mode & bits ) != (mode_t)mode ) {
		errno = EACCES;
		return -1;
	}
	return 0;
}

int eaccess( const char *path, int mode )
{
	return access(path, mode);
}

int faccessat( int dirfd, const char *path, int mode, int flags )
{
	AT_PATH(dirfd, path)
	(void)flags;
	return access(path, mode);
}

#define PATH_CALL(call)					\
	char	p[PATH_MAX];				\
	ER	er;					\
	if ( __psx_path(path, p) < 0 ) return -1;	\
	er = call;					\
	return ( er < 0 ) ? __psx_fail_fs(er) : 0;

int mkdir( const char *path, mode_t mode )
{
	char	p[PATH_MAX];
	ER	er;

	if ( __psx_path(path, p) < 0 ) return -1;
	er = fs_mkdir(p);
	if ( er < 0 ) return __psx_fail_fs(er);
	(void)fs_chmod(p, (UINT)( mode & ~__psx_umask & 07777 ));
	return 0;
}

int mkdirat( int dirfd, const char *path, mode_t mode )
{
	AT_PATH(dirfd, path)
	return mkdir(path, mode);
}

int rmdir( const char *path )
{
	PATH_CALL(fs_rmdir(p))
}

int _unlink( const char *path )
{
	PATH_CALL(fs_unlink(p))
}

int unlink( const char *path ) __attribute__((alias("_unlink")));

int unlinkat( int dirfd, const char *path, int flags )
{
	AT_PATH(dirfd, path)
	return ( flags & AT_REMOVEDIR ) ? rmdir(path) : unlink(path);
}

int _rename( const char *from, const char *to )
{
	char	f[PATH_MAX], t[PATH_MAX];
	ER	er;

	if ( __psx_path(from, f) < 0 || __psx_path(to, t) < 0 ) return -1;
	er = fs_rename(f, t);
	if ( er == EX_NOTSUP ) {
		errno = EXDEV;
		return -1;
	}
	return ( er < 0 ) ? __psx_fail_fs(er) : 0;
}

int rename( const char *from, const char *to ) __attribute__((alias("_rename")));

int renameat( int a, const char *from, int b, const char *to )
{
	AT_PATH(a, from)
	{
		char	t[PATH_MAX];

		if ( __psx_path_at(b, to, t) < 0 ) return -1;
		return rename(from, t);
	}
}

int _link( const char *from, const char *to )
{
	char	f[PATH_MAX], t[PATH_MAX];
	ER	er;

	if ( __psx_path(from, f) < 0 || __psx_path(to, t) < 0 ) return -1;
	er = fs_link(f, t);
	if ( er == EX_NOTSUP ) {
		errno = EXDEV;
		return -1;
	}
	return ( er < 0 ) ? __psx_fail_fs(er) : 0;
}

int link( const char *from, const char *to ) __attribute__((alias("_link")));

int linkat( int a, const char *from, int b, const char *to, int flags )
{
	(void)flags;
	AT_PATH(a, from)
	{
		char	t[PATH_MAX];

		if ( __psx_path_at(b, to, t) < 0 ) return -1;
		return link(from, t);
	}
}

int symlink( const char *from, const char *to )
{
	(void)from;
	(void)to;
	errno = EOPNOTSUPP;
	return -1;
}

int symlinkat( const char *from, int dirfd, const char *to )
{
	AT_PATH(dirfd, to)
	return symlink(from, to);
}

ssize_t readlink( const char *path, char *buf, size_t len )
{
	struct stat	st;

	(void)buf;
	(void)len;
	if ( stat(path, &st) < 0 ) return -1;
	errno = EINVAL;			/* nothing is a symbolic link */
	return -1;
}

ssize_t readlinkat( int dirfd, const char *path, char *buf, size_t len )
{
	AT_PATH(dirfd, path)
	return readlink(path, buf, len);
}

char *realpath( const char *path, char *resolved )
{
	char		p[PATH_MAX];
	struct stat	st;

	if ( __psx_path(path, p) < 0 || stat(p, &st) < 0 ) {
		return NULL;
	}
	if ( resolved == NULL ) {
		return strdup(p);
	}
	strcpy(resolved, p);
	return resolved;
}

int chmod( const char *path, mode_t mode )
{
	PATH_CALL(fs_chmod(p, (UINT)( mode & 07777 )))
}

int lchmod( const char *path, mode_t mode )
{
	return chmod(path, mode);
}

int fchmodat( int dirfd, const char *path, mode_t mode, int flags )
{
	AT_PATH(dirfd, path)
	(void)flags;
	return chmod(path, mode);
}

int fchmod( int fd, mode_t mode )
{
	(void)fd;
	(void)mode;
	return 0;			/* the file layer keeps the mode by path only */
}

/* The owner of an object is its maker's; it is not given away here */
int chown( const char *path, uid_t uid, gid_t gid )
{
	struct stat	st;

	if ( stat(path, &st) < 0 ) return -1;
	if ( ( uid == (uid_t)-1 || uid == st.st_uid ) && ( gid == (gid_t)-1 || gid == st.st_gid ) ) {
		return 0;
	}
	errno = EPERM;
	return -1;
}

int lchown( const char *path, uid_t uid, gid_t gid )
{
	return chown(path, uid, gid);
}

int fchown( int fd, uid_t uid, gid_t gid )
{
	(void)fd;
	(void)uid;
	(void)gid;
	return 0;
}

int fchownat( int dirfd, const char *path, uid_t uid, gid_t gid, int flags )
{
	AT_PATH(dirfd, path)
	(void)flags;
	return chown(path, uid, gid);
}

int chflags( const char *path, unsigned long flags )
{
	struct stat	st;

	if ( stat(path, &st) < 0 ) return -1;
	if ( flags == 0 ) return 0;
	errno = EOPNOTSUPP;
	return -1;
}

int lchflags( const char *path, unsigned long flags )
{
	return chflags(path, flags);
}

int fchflags( int fd, unsigned long flags )
{
	(void)fd;
	if ( flags == 0 ) return 0;
	errno = EOPNOTSUPP;
	return -1;
}

int truncate( const char *path, off_t len )
{
	PATH_CALL(fs_truncate(p, (UD)len))
}

static int set_time( const char *path, time_t t )
{
	char	p[PATH_MAX];
	ER	er;

	if ( __psx_path(path, p) < 0 ) return -1;
	er = fs_utime(p, (UD)( (long long)t - TRON_EPOCH ));
	if ( er == EX_NOTSUP ) {
		T_FSXSTAT x;

		er = fs_xstat(p, &x);		/* a file layer that keeps no times: there is the file */
	}
	return ( er < 0 ) ? __psx_fail_fs(er) : 0;
}

int utime( const char *path, const struct utimbuf *t )
{
	return set_time(path, ( t != NULL ) ? t->modtime : time(NULL));
}

int utimes( const char *path, const struct timeval tv[2] )
{
	return set_time(path, ( tv != NULL ) ? tv[1].tv_sec : time(NULL));
}

int lutimes( const char *path, const struct timeval tv[2] )
{
	return utimes(path, tv);
}

int utimensat( int dirfd, const char *path, const struct timespec ts[2], int flags )
{
	AT_PATH(dirfd, path)
	(void)flags;
	if ( ts != NULL && ts[1].tv_nsec == UTIME_OMIT ) return 0;
	return set_time(path, ( ts != NULL && ts[1].tv_nsec != UTIME_NOW ) ? ts[1].tv_sec : time(NULL));
}

int futimens( int fd, const struct timespec ts[2] )
{
	(void)fd;
	(void)ts;
	return 0;
}

int futimes( int fd, const struct timeval tv[2] )
{
	(void)fd;
	(void)tv;
	return 0;
}

int mkfifo( const char *path, mode_t mode )
{
	(void)path;
	(void)mode;
	errno = EOPNOTSUPP;
	return -1;
}

int mknod( const char *path, mode_t mode, dev_t dev )
{
	(void)path;
	(void)mode;
	(void)dev;
	errno = EPERM;
	return -1;
}

mode_t umask( mode_t m )
{
	mode_t	old = (mode_t)__psx_umask;

	__psx_umask = m & 0777;
	return old;
}

/* ---------------------------------------------------------------- directories */

#define DIR_BATCH	8

struct __psx_dir {
	int		fd;
	int		n, at;		/* entries in batch, the next of them */
	int		dots;		/* "." and ".." given yet */
	long		pos;
	T_DIRENT	batch[DIR_BATCH];
	struct dirent	ent;
};

DIR *fdopendir( int fd )
{
	struct __psx_dir *d;
	struct stat	st;

	if ( fstat(fd, &st) < 0 ) return NULL;
	if ( !S_ISDIR(st.st_mode) ) {
		errno = ENOTDIR;
		return NULL;
	}
	d = (struct __psx_dir *)calloc(1, sizeof(*d));
	if ( d == NULL ) {
		errno = ENOMEM;
		return NULL;
	}
	d->fd = fd;
	(void)fcntl(fd, F_SETFD, FD_CLOEXEC);
	return d;
}

DIR *opendir( const char *path )
{
	DIR	*d;
	int	fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);

	if ( fd < 0 ) return NULL;
	d = fdopendir(fd);
	if ( d == NULL ) {
		int e = errno;

		(void)close(fd);
		errno = e;
	}
	return d;
}

static void ent_set( struct dirent *e, const char *name, unsigned int mode, unsigned long long ino )
{
	size_t	n = strlen(name);

	if ( n > MAXNAMLEN ) n = MAXNAMLEN;
	memcpy(e->d_name, name, n);
	e->d_name[n] = 0;
	e->d_namlen = (unsigned short)n;
	e->d_fileno = (ino_t)( ino != 0 ? ino : 1 );
	e->d_reclen = (unsigned short)sizeof(*e);
	e->d_type = ( ( mode & FS_IFMT ) == FS_IFDIR ) ? DT_DIR
		  : ( ( mode & FS_IFMT ) == FS_IFCHR ) ? DT_CHR
		  : ( ( mode & FS_IFMT ) == FS_IFIFO ) ? DT_FIFO
		  : ( ( mode & FS_IFMT ) == FS_IFREG ) ? DT_REG : DT_UNKNOWN;
}

struct dirent *readdir( DIR *d )
{
	PSXFD	*e;
	INT	n;

	if ( d->dots < 2 ) {
		ent_set(&d->ent, ( d->dots == 0 ) ? "." : "..", FS_IFDIR, (unsigned long long)( d->dots + 1 ));
		d->dots++;
		d->pos++;
		return &d->ent;
	}
	for ( ;; ) {
		if ( d->at < d->n ) {
			T_DIRENT *t = &d->batch[d->at++];

			if ( strcmp((const char *)t->name, ".") == 0 || strcmp((const char *)t->name, "..") == 0 ) {
				continue;
			}
			ent_set(&d->ent, (const char *)t->name, t->mode, t->ino);
			d->pos++;
			return &d->ent;
		}
		e = __psx_fd_get(d->fd);
		if ( e == NULL || e->kind != PK_FS ) return NULL;
		n = fs_getdents(e->kfd, d->batch, DIR_BATCH);
		if ( n <= 0 ) {
			if ( n < 0 ) __psx_fail_fs(n);
			return NULL;
		}
		d->n = n;
		d->at = 0;
	}
}

int readdir_r( DIR *d, struct dirent *ent, struct dirent **res )
{
	int		e = errno;
	struct dirent	*r;

	errno = 0;
	r = readdir(d);
	if ( r == NULL ) {
		*res = NULL;
		if ( errno != 0 ) return errno;
		errno = e;
		return 0;
	}
	memcpy(ent, r, sizeof(*ent));
	*res = ent;
	errno = e;
	return 0;
}

int closedir( DIR *d )
{
	int	r = close(d->fd);

	free(d);
	return r;
}

void rewinddir( DIR *d )
{
	(void)lseek(d->fd, 0, SEEK_SET);
	d->n = d->at = 0;
	d->dots = 0;
	d->pos = 0;
}

int dirfd( DIR *d )
{
	return d->fd;
}

long telldir( DIR *d )
{
	return d->pos;
}

void seekdir( DIR *d, long pos )
{
	rewinddir(d);
	while ( d->pos < pos && readdir(d) != NULL ) ;
}

int alphasort( const struct dirent **a, const struct dirent **b )
{
	return strcoll((*a)->d_name, (*b)->d_name);
}

int scandir( const char *path, struct dirent ***list, int (*sel)( const struct dirent * ),
	     int (*cmp)( const struct dirent **, const struct dirent ** ) )
{
	DIR		*d = opendir(path);
	struct dirent	*e, **v = NULL;
	int		n = 0, cap = 0;

	if ( d == NULL ) return -1;
	while ( ( e = readdir(d) ) != NULL ) {
		if ( sel != NULL && !sel(e) ) continue;
		if ( n == cap ) {
			struct dirent **nv;

			cap = ( cap == 0 ) ? 32 : cap * 2;
			nv = (struct dirent **)realloc(v, sizeof(*v) * (size_t)cap);
			if ( nv == NULL ) break;
			v = nv;
		}
		v[n] = (struct dirent *)malloc(sizeof(**v));
		if ( v[n] == NULL ) break;
		memcpy(v[n], e, sizeof(**v));
		n++;
	}
	closedir(d);
	if ( cmp != NULL && n > 1 ) {
		qsort(v, (size_t)n, sizeof(*v), (int (*)( const void *, const void * ))cmp);
	}
	*list = v;
	return n;
}

/* ---------------------------------------------------------------- file systems */

/* The mount a path is on: its mount point and kind */
static void mount_of( const char *p, struct statvfs *v )
{
	T_FSMNT	*m;
	INT	n, i;
	size_t	best = 0;

	strcpy(v->f_mntonname, "/");
	strcpy(v->f_fstypename, "objfs");
	strcpy(v->f_mntfromname, "objects");
	n = fs_mounts(NULL, 0);
	if ( n <= 0 ) return;
	m = (T_FSMNT *)malloc(sizeof(T_FSMNT) * (size_t)n);
	if ( m == NULL ) return;
	n = fs_mounts(m, n);
	for ( i = 0; i < n; i++ ) {
		size_t	k = strlen((const char *)m[i].path);

		if ( strncmp(p, (const char *)m[i].path, k) == 0
		  && ( p[k] == 0 || p[k] == '/' || k == 1 ) && k >= best ) {
			best = k;
			strlcpy(v->f_mntonname, (const char *)m[i].path, sizeof(v->f_mntonname));
			strlcpy(v->f_fstypename, (const char *)m[i].fimp, sizeof(v->f_fstypename));
			strlcpy(v->f_mntfromname, m[i].dev[0] != 0 ? (const char *)m[i].dev
						 : (const char *)m[i].fimp, sizeof(v->f_mntfromname));
		}
	}
	free(m);
}

int statvfs( const char *path, struct statvfs *v )
{
	char	p[PATH_MAX];
	T_FSSTAT s;
	ER	er;

	if ( __psx_path(path, p) < 0 ) return -1;
	memset(v, 0, sizeof(*v));
	er = fs_statvfs(p, &s);
	if ( er < 0 && er != EX_NOTSUP ) return __psx_fail_fs(er);
	if ( er >= 0 ) {
		v->f_bsize = v->f_frsize = v->f_iosize = (unsigned long)s.bsize;
		v->f_blocks = (fsblkcnt_t)s.blocks;
		v->f_bfree = v->f_bavail = (fsblkcnt_t)s.bfree;
	} else {
		v->f_bsize = v->f_frsize = v->f_iosize = 4096;
	}
	v->f_namemax = 255;
	v->f_flag = ST_LOCAL;
	mount_of(p, v);
	return 0;
}

int statvfs1( const char *path, struct statvfs *v, int flags )
{
	(void)flags;
	return statvfs(path, v);
}

int fstatvfs( int fd, struct statvfs *v )
{
	(void)fd;
	return statvfs(__psx_cwd, v);
}

int fstatvfs1( int fd, struct statvfs *v, int flags )
{
	(void)flags;
	return fstatvfs(fd, v);
}

int getvfsstat( struct statvfs *buf, size_t size, int flags )
{
	T_FSMNT	*m;
	INT	n, i;
	size_t	k = 0, max = size / sizeof(*buf);

	(void)flags;
	n = fs_mounts(NULL, 0);
	if ( n <= 0 ) return 0;
	if ( buf == NULL ) return n;
	m = (T_FSMNT *)malloc(sizeof(T_FSMNT) * (size_t)n);
	if ( m == NULL ) {
		errno = ENOMEM;
		return -1;
	}
	n = fs_mounts(m, n);
	for ( i = 0; i < n && k < max; i++ ) {
		if ( statvfs((const char *)m[i].path, &buf[k]) == 0 ) k++;
	}
	free(m);
	return (int)k;
}

int getmntinfo( struct statvfs **p, int flags )
{
	static struct statvfs *keep;
	int	n = getvfsstat(NULL, 0, flags);

	if ( n <= 0 ) return 0;
	free(keep);
	keep = (struct statvfs *)calloc((size_t)n, sizeof(*keep));
	if ( keep == NULL ) return 0;
	n = getvfsstat(keep, sizeof(*keep) * (size_t)n, flags);
	*p = keep;
	return ( n < 0 ) ? 0 : n;
}
