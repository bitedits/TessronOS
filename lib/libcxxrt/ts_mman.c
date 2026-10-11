/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	ts_mman.c
 *	mmap, munmap, mprotect and madvise on ts_map_mem, ts_unm_mem and
 *	ts_ctl_mem (include/tsposix/sys/mman.h, design 9.8.1).
 *
 *	memfd_create makes a memory object of the process, known by
 *	descriptors of their own range (MEMFD_BASE up): its size is set once
 *	with ftruncate, which takes the pages, and every mapping of it
 *	(mmap with MAP_SHARED) is the same memory, at the same address. The
 *	descriptors can be duplicated and marked read-only (F_SETFL); the
 *	pages go back when the last descriptor is closed and the last mapping
 *	let go. This is the shared memory a program shares with itself (the
 *	buffers of Mojo's data pipes in the browser); the protection asked
 *	for a mapping is not applied.
 */

#include <config.h>
#include <stdint.h>
#include <string.h>
#include <tk/typedef.h>
#include <tk/errno.h>
#include <ts/umem.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

#define PAGE	4096UL

/*
 * The GPU's descriptors (GPUFD_BASE up), when the program has OpenGL ES
 * linked in: the DRM shim (lib/libgl/gl_drm.c) maps a BO for an mmap of
 * one, and knows the BOs' mappings, which stay until the BO goes.
 * Without it these are not there and the descriptors are not either.
 */
extern void	*ts_gpu_fd_mmap( int fd, size_t len, off_t off ) __attribute__((weak));
extern int	ts_gpu_fd_munmap( void *addr, size_t len ) __attribute__((weak));

/* ---------------------------------------------------------------- memory objects */

#define MEMFD_BASE	0x3c00		/* between the random sources and the sockets */
/*
 * As many descriptors as the range up to the sockets holds, and as many
 * objects: a page that reads many Blobs and fetches many bodies has a data
 * pipe, and so an object, for each until the collector lets it go
 */
#define MEMFD_MAX	1024		/* descriptors */
#define MOBJ_MAX	1024		/* objects */

typedef struct {
	int	refs;			/* descriptors on it; 0: free */
	int	maps;			/* mappings of it */
	char	*base;			/* its pages, once its size is set */
	size_t	size;
} MOBJ;

static struct {
	int	obj;			/* index of the object + 1; 0: free */
	int	rdonly;
} mfd[MEMFD_MAX];
static MOBJ		mobj[MOBJ_MAX];
static pthread_mutex_t	mfd_mu = PTHREAD_MUTEX_INITIALIZER;

static MOBJ *mfd_obj( int fd )
{
	int	i = fd - MEMFD_BASE;

	if ( i < 0 || i >= MEMFD_MAX || mfd[i].obj == 0 ) return NULL;
	return &mobj[mfd[i].obj - 1];
}

/* An object without descriptors or mappings gives its pages back (under mfd_mu) */
static void mobj_drop( MOBJ *o )
{
	if ( o->refs > 0 || o->maps > 0 ) return;
	if ( o->base != NULL ) (void)ts_unm_mem(o->base, (SZ)( ( o->size + PAGE - 1 ) & ~( PAGE - 1 ) ));
	memset(o, 0, sizeof(*o));
}

static int mfd_new( int obj, int rdonly )
{
	int	i;

	for ( i = 0; i < MEMFD_MAX; i++ ) {
		if ( mfd[i].obj == 0 ) {
			mfd[i].obj = obj + 1;
			mfd[i].rdonly = rdonly;
			mobj[obj].refs++;
			return MEMFD_BASE + i;
		}
	}
	errno = EMFILE;
	return -1;
}

int ts_memfd_is( int fd )
{
	return fd >= MEMFD_BASE && fd < MEMFD_BASE + MEMFD_MAX;
}

int memfd_create( const char *name, unsigned int flags )
{
	int	i, fd = -1;

	(void)name; (void)flags;
	pthread_mutex_lock(&mfd_mu);
	for ( i = 0; i < MOBJ_MAX && mobj[i].refs + mobj[i].maps > 0; i++ ) ;
	if ( i < MOBJ_MAX ) {
		memset(&mobj[i], 0, sizeof(mobj[i]));
		fd = mfd_new(i, 0);
	} else {
		errno = EMFILE;
	}
	pthread_mutex_unlock(&mfd_mu);
	return fd;
}

int ts_memfd_close( int fd )
{
	MOBJ	*o;

	pthread_mutex_lock(&mfd_mu);
	o = mfd_obj(fd);
	if ( o == NULL ) {
		pthread_mutex_unlock(&mfd_mu);
		errno = EBADF;
		return -1;
	}
	mfd[fd - MEMFD_BASE].obj = 0;
	o->refs--;
	mobj_drop(o);
	pthread_mutex_unlock(&mfd_mu);
	return 0;
}

int ts_memfd_dup( int fd )
{
	MOBJ	*o;
	int	r = -1;

	pthread_mutex_lock(&mfd_mu);
	o = mfd_obj(fd);
	if ( o != NULL ) {
		r = mfd_new((int)( o - mobj ), mfd[fd - MEMFD_BASE].rdonly);
	} else {
		errno = EBADF;
	}
	pthread_mutex_unlock(&mfd_mu);
	return r;
}

/* The size, set once: the pages are taken, zero filled */
int ts_memfd_truncate( int fd, size_t len )
{
	MOBJ	*o;
	void	*p = NULL;
	int	r = 0;

	pthread_mutex_lock(&mfd_mu);
	o = mfd_obj(fd);
	if ( o == NULL || mfd[fd - MEMFD_BASE].rdonly ) {
		errno = EBADF;
		r = -1;
	} else if ( o->base != NULL ) {
		if ( len != o->size ) {
			errno = EINVAL;
			r = -1;
		}
	} else if ( len > 0 ) {
		if ( ts_map_mem(&p, (SZ)( ( len + PAGE - 1 ) & ~( PAGE - 1 ) ), 0, TS_MEM_RW) < E_OK ) {
			errno = ENOMEM;
			r = -1;
		} else {
			o->base = (char *)p;
			o->size = len;
		}
	}
	pthread_mutex_unlock(&mfd_mu);
	return r;
}

int ts_memfd_fstat( int fd, struct stat *st )
{
	MOBJ	*o;

	pthread_mutex_lock(&mfd_mu);
	o = mfd_obj(fd);
	memset(st, 0, sizeof(*st));
	if ( o != NULL ) {
		st->st_mode = S_IFREG | 0600;
		st->st_size = (off_t)o->size;
		st->st_ino = (ino_t)( o - mobj + 1 );
		st->st_nlink = 1;
		st->st_blksize = PAGE;
	}
	pthread_mutex_unlock(&mfd_mu);
	if ( o == NULL ) {
		errno = EBADF;
		return -1;
	}
	return 0;
}

/* F_GETFL says whether the descriptor is read-only; F_SETFL may make it so */
int ts_memfd_fcntl( int fd, int cmd, int arg )
{
	int	r = 0;

	pthread_mutex_lock(&mfd_mu);
	if ( mfd_obj(fd) == NULL ) {
		errno = EBADF;
		r = -1;
	} else if ( cmd == F_GETFL ) {
		r = mfd[fd - MEMFD_BASE].rdonly ? O_RDONLY : O_RDWR;
	} else if ( cmd == F_SETFL ) {
		if ( ( arg & O_ACCMODE ) == O_RDONLY ) mfd[fd - MEMFD_BASE].rdonly = 1;
	}
	pthread_mutex_unlock(&mfd_mu);
	return r;
}

/* A mapping of an object: its own pages, from the offset */
static void *mfd_map( int fd, size_t len, off_t off )
{
	MOBJ	*o;
	void	*p = MAP_FAILED;

	pthread_mutex_lock(&mfd_mu);
	o = mfd_obj(fd);
	if ( o == NULL || o->base == NULL ) {
		errno = ( o == NULL ) ? EBADF : EINVAL;
	} else if ( off < 0 || (size_t)off > o->size || len > o->size - (size_t)off ) {
		errno = EINVAL;
	} else {
		o->maps++;
		p = o->base + off;
	}
	pthread_mutex_unlock(&mfd_mu);
	return p;
}

/* Whether addr is in an object's pages: the mapping let go (1), or not one (0) */
static int mfd_unmap( void *addr )
{
	int	i, r = 0;

	pthread_mutex_lock(&mfd_mu);
	for ( i = 0; i < MOBJ_MAX; i++ ) {
		MOBJ *o = &mobj[i];

		if ( o->base != NULL && o->maps > 0 && (char *)addr >= o->base
		  && (char *)addr < o->base + o->size ) {
			o->maps--;
			mobj_drop(o);
			r = 1;
			break;
		}
	}
	pthread_mutex_unlock(&mfd_mu);
	return r;
}

/* Executable and writable: written and run by turns (TS_MEM_JIT) */
static UINT to_prot( int prot )
{
	if ( ( prot & ( PROT_WRITE | PROT_EXEC ) ) == ( PROT_WRITE | PROT_EXEC ) ) return TS_MEM_JIT;
	if ( prot & PROT_EXEC ) return TS_MEM_READ | TS_MEM_EXEC;
	if ( prot & PROT_WRITE ) return TS_MEM_RW;
	if ( prot & PROT_READ ) return TS_MEM_READ;
	return TS_MEM_NONE;
}

static int to_errno( ER er )
{
	switch ( er ) {
	case E_NOMEM:	return ENOMEM;
	case E_NOSPT:	return EACCES;
	default:	return EINVAL;
	}
}

void *mmap( void *addr, size_t len, int prot, int flags, int fd, off_t off )
{
	void	*p = addr;
	ER	er;

	if ( len > 0 && ts_memfd_is(fd) && ( flags & MAP_SHARED ) != 0 && ( flags & MAP_FIXED ) == 0 ) {
		return mfd_map(fd, len, off);
	}
	if ( fd >= 0 && ts_gpu_fd_mmap != NULL && ( flags & MAP_FIXED ) == 0 ) {
		p = ts_gpu_fd_mmap(fd, len, off);
		if ( p != MAP_FAILED || errno != EBADF ) {
			return p;
		}
		p = addr;
	}
	if ( len == 0 || ( flags & MAP_ANONYMOUS ) == 0 || fd != -1 ) {
		errno = ( len == 0 ) ? EINVAL : ENODEV;
		return MAP_FAILED;
	}
	if ( flags & MAP_FIXED ) {
		/* over what is there: the pages go and come back zero filled */
		er = ts_unm_mem(addr, (SZ)len);
		(void)er;
	}
	er = ts_map_mem(&p, (SZ)len, 0, to_prot(prot));
	if ( er < E_OK ) {
		errno = to_errno(er);
		return MAP_FAILED;
	}
	if ( ( flags & ( MAP_FIXED | MAP_FIXED_NOREPLACE ) ) != 0 && p != addr ) {
		(void)ts_unm_mem(p, (SZ)len);
		errno = ( flags & MAP_FIXED ) ? ENOMEM : EEXIST;
		return MAP_FAILED;
	}
	return p;
}

void *mmap_aligned_tf( size_t len, size_t align, int prot )
{
	void	*p = NULL;
	ER	er = ts_map_mem(&p, (SZ)len, (SZ)align, to_prot(prot));

	if ( er < E_OK ) {
		errno = to_errno(er);
		return MAP_FAILED;
	}
	return p;
}

int munmap( void *addr, size_t len )
{
	ER	er;

	if ( mfd_unmap(addr) ) {
		return 0;			/* a mapping of a memory object */
	}
	if ( ts_gpu_fd_munmap != NULL && ts_gpu_fd_munmap(addr, len) == 0 ) {
		return 0;			/* a BO's, which stays mapped while the BO is */
	}
	er = ts_unm_mem(addr, (SZ)( ( len + PAGE - 1 ) & ~( PAGE - 1 ) ));
	if ( er < E_OK ) {
		errno = to_errno(er);
		return -1;
	}
	return 0;
}

int mprotect( void *addr, size_t len, int prot )
{
	ER	er;

	er = ts_ctl_mem(addr, (SZ)( ( len + PAGE - 1 ) & ~( PAGE - 1 ) ), to_prot(prot));
	if ( er < E_OK ) {
		errno = to_errno(er);
		return -1;
	}
	return 0;
}

/* The pages given up: they read as zeros afterwards and keep their protection */
int madvise( void *addr, size_t len, int advice )
{
	SZ	n = (SZ)( ( len + PAGE - 1 ) & ~( PAGE - 1 ) );

	if ( advice == MADV_DONTNEED || advice == MADV_FREE || advice == MADV_REMOVE ) {
		if ( ts_ctl_mem(addr, n, TS_MEM_DISCARD) < E_OK ) {
			errno = ENOMEM;
			return -1;
		}
	}
	return 0;
}

int posix_madvise( void *addr, size_t len, int advice )
{
	return ( madvise(addr, len, advice) == 0 ) ? 0 : errno;
}

int msync( void *addr, size_t len, int flags )
{
	(void)addr; (void)len; (void)flags;
	return 0;
}

/* Pages are made when their memory is made accessible and are not paged
   out; every page asked about is counted present */
int mincore( void *addr, size_t len, unsigned char *vec )
{
	size_t	i, n;

	if ( ( (uintptr_t)addr & ( PAGE - 1 ) ) != 0 || vec == NULL ) {
		errno = EINVAL;
		return -1;
	}
	n = ( len + PAGE - 1 ) / PAGE;
	for ( i = 0; i < n; i++ ) vec[i] = 1;
	return 0;
}

int mlock( const void *addr, size_t len )
{
	(void)addr; (void)len;
	return 0;
}

int munlock( const void *addr, size_t len )
{
	(void)addr; (void)len;
	return 0;
}
