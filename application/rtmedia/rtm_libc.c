/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	rtm_libc.c
 *	What the C library (newlib) needs from the system, for 実時間メディア
 *
 *	The heap is a range of addresses reserved once with ts_map_mem;
 *	_sbrk commits its pages as the break moves up and gives whole pages
 *	back as it moves down, so the sound of a long file returns to the
 *	system once it is let go. The loader and the main task both ask
 *	for memory, so malloc is taken one at a time (newlib takes the lock
 *	again from inside, hence the count).
 */

#include "rtm.h"
#include <ts/umem.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <reent.h>
#include <sys/stat.h>

#define HEAP_MAX	( (SZ)512 * 1024 * 1024 )
#define PAGE		4096

static char	*heap_base = NULL;
static size_t	heap_brk = 0;
static size_t	heap_top = 0;

void *_sbrk( ptrdiff_t incr )
{
	char	*p;

	if ( heap_base == NULL ) {
		void	*a = NULL;

		if ( ts_map_mem(&a, HEAP_MAX, PAGE, TS_MEM_NONE) < E_OK ) {
			errno = ENOMEM;
			return (void *)-1;
		}
		heap_base = (char *)a;
	}
	if ( incr < 0 ? (size_t)( -incr ) > heap_brk : heap_brk + (size_t)incr > (size_t)HEAP_MAX ) {
		errno = ENOMEM;
		return (void *)-1;
	}
	p = heap_base + heap_brk;
	if ( incr > 0 ) {
		size_t	need = ( heap_brk + (size_t)incr + PAGE - 1 ) & ~(size_t)( PAGE - 1 );

		if ( need > heap_top ) {
			if ( ts_ctl_mem(heap_base + heap_top, (SZ)( need - heap_top ), TS_MEM_RW) < E_OK ) {
				errno = ENOMEM;
				return (void *)-1;
			}
			heap_top = need;
		}
	}
	heap_brk += (size_t)incr;
	if ( incr < 0 ) {
		size_t	keep = ( heap_brk + PAGE - 1 ) & ~(size_t)( PAGE - 1 );

		if ( keep < heap_top && ts_ctl_mem(heap_base + keep, (SZ)( heap_top - keep ), TS_MEM_NONE) >= E_OK ) {
			heap_top = keep;
		}
	}
	return p;
}

static ID		mlk_sem = -1;
static volatile ID	mlk_owner = 0;
static INT		mlk_depth = 0;

void rtm_libc_init( void )
{
	T_CSEM	s;

	memset(&s, 0, sizeof(s));
	s.sematr = TA_TFIFO;
	s.isemcnt = 1;
	s.maxsem = 1;
	mlk_sem = tk_cre_sem(&s);
}

void __malloc_lock( struct _reent *r )
{
	ID	me;

	(void)r;
	if ( mlk_sem <= 0 ) return;
	me = tk_get_tid();
	if ( mlk_owner == me ) {
		mlk_depth++;
		return;
	}
	(void)tk_wai_sem(mlk_sem, 1, TMO_FEVR);
	mlk_owner = me;
	mlk_depth = 1;
}

void __malloc_unlock( struct _reent *r )
{
	(void)r;
	if ( mlk_sem <= 0 ) return;
	if ( --mlk_depth > 0 ) return;
	mlk_owner = 0;
	(void)tk_sig_sem(mlk_sem, 1);
}

int _write( int fd, const void *buf, size_t n )
{
	char	tmp[128];
	size_t	k, done = 0;

	if ( fd != 1 && fd != 2 ) {
		errno = EBADF;
		return -1;
	}
	while ( done < n ) {
		k = n - done;
		if ( k > sizeof(tmp) - 1 ) k = sizeof(tmp) - 1;
		memcpy(tmp, (const char *)buf + done, k);
		tmp[k] = 0;
		tm_putstring((const UB *)tmp);
		done += k;
	}
	return (int)n;
}

int _read( int fd, void *buf, size_t n ) { (void)fd; (void)buf; (void)n; return 0; }
int _close( int fd ) { (void)fd; return -1; }
int _fstat( int fd, struct stat *st ) { (void)fd; memset(st, 0, sizeof(*st)); st->st_mode = S_IFCHR; return 0; }
int _isatty( int fd ) { return fd <= 2; }
off_t _lseek( int fd, off_t off, int whence ) { (void)fd; (void)off; (void)whence; return 0; }
int _open( const char *path, int flags, ... ) { (void)path; (void)flags; errno = ENOENT; return -1; }
void _exit( int code ) { ts_ext_prc(code); for ( ;; ) ; }
int _kill( int pid, int sig ) { (void)pid; (void)sig; errno = EINVAL; return -1; }
int _getpid( void ) { return (int)ts_get_pid(); }
