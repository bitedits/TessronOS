/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	psx_time.c
 *	Time and memory (lib/libposix, design 5.8.3)
 *
 *	The monotonic clock is the kernel's (ts_get_mono), the calendar the
 *	clock device's (tk_get_utc), read again once a second so that a clock
 *	set meanwhile is seen. The local zone comes from TZ, or, without it,
 *	from the system's offset (dt_getsystz), given to newlib as TZ.
 *
 *	The heap is one reservation of PSX_HEAP_MAX bytes of addresses, made
 *	on first use, its pages made HEAP_STEP at a time as the break goes
 *	up. mmap gives anonymous memory, and a file's bytes read into memory
 *	for MAP_PRIVATE.
 */

#include "psx_int.h"
#include <ts/umem.h>
#include <ts/time.h>
#include <ts/dt.h>
#include <tk/syscall.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---------------------------------------------------------------- clocks */

static int64_t	rt_offset;
static uint64_t	rt_taken;
static int	rt_ok;

static uint64_t mono_ns( void )
{
	UD	ns = 0;

	(void)ts_get_mono(&ns);
	return (uint64_t)ns;
}

static void rt_refresh( uint64_t now )
{
	SYSTIM	t;

	if ( tk_get_utc(&t) >= E_OK ) {
		int64_t	ms = ( (int64_t)t.hi << 32 ) | t.lo;

		rt_offset = ms * 1000000LL - (int64_t)now;
		rt_taken = now;
		rt_ok = 1;
	}
}

int clock_gettime( clockid_t clk, struct timespec *ts )
{
	uint64_t now = mono_ns();
	int64_t	t = (int64_t)now;

	if ( clk == CLOCK_REALTIME ) {
		if ( !rt_ok || now - rt_taken > 1000000000ULL ) rt_refresh(now);
		t += rt_offset;
	}
	ts->tv_sec = (time_t)( t / 1000000000LL );
	ts->tv_nsec = (long)( t % 1000000000LL );
	return 0;
}

int clock_getres( clockid_t clk, struct timespec *ts )
{
	(void)clk;
	if ( ts != NULL ) {
		ts->tv_sec = 0;
		ts->tv_nsec = 1000;
	}
	return 0;
}

int _gettimeofday( struct timeval *tv, void *tz )
{
	struct timespec	ts;

	(void)tz;
	clock_gettime(CLOCK_REALTIME, &ts);
	if ( tv != NULL ) {
		tv->tv_sec = ts.tv_sec;
		tv->tv_usec = ts.tv_nsec / 1000;
	}
	return 0;
}

int settimeofday( const struct timeval *tv, const struct timezone *tz )
{
	(void)tv;
	(void)tz;
	errno = EPERM;
	return -1;
}

/* The clock is not set from a program: it is not slowed or hurried either */
int adjtime( const struct timeval *delta, struct timeval *old )
{
	if ( old != NULL ) {
		old->tv_sec = 0;
		old->tv_usec = 0;
	}
	if ( delta == NULL ) return 0;
	errno = EPERM;
	return -1;
}

clock_t _times( struct tms *buf )
{
	clock_t	c = (clock_t)( mono_ns() / ( 1000000000ULL / 100 ) );

	if ( buf != NULL ) {
		buf->tms_utime = c;
		buf->tms_stime = 0;
		buf->tms_cutime = 0;
		buf->tms_cstime = 0;
	}
	return c;
}

clock_t times( struct tms *buf ) __attribute__((alias("_times")));

/* TZ for newlib from the system's offset, when the program was not given one */
void __psx_tz_init( void )
{
	INT	min = 0;
	char	tz[32];
	int	h, m;

	if ( getenv("TZ") != NULL ) {
		tzset();
		return;
	}
	if ( dt_getsystz(&min) < E_OK ) min = 0;
	h = ( min < 0 ? -min : min ) / 60;
	m = ( min < 0 ? -min : min ) % 60;
	/* POSIX counts the other way: west of Greenwich is positive */
	if ( min == 540 ) {
		snprintf(tz, sizeof(tz), "JST-9");
	} else if ( m == 0 ) {
		snprintf(tz, sizeof(tz), "LT%c%d", ( min > 0 ) ? '-' : '+', h);
	} else {
		snprintf(tz, sizeof(tz), "LT%c%d:%02d", ( min > 0 ) ? '-' : '+', h, m);
	}
	(void)setenv("TZ", tz, 0);
	tzset();
}

/* ---------------------------------------------------------------- the heap */

#define PSX_HEAP_MAX	( 16ULL * 1024 * 1024 * 1024 )
#define HEAP_STEP	( 1024 * 1024 )

static char	*heap_base, *heap_brk, *heap_made;

void *_sbrk( ptrdiff_t incr )
{
	char	*old;

	if ( heap_base == NULL ) {
		void	*p = NULL;

		if ( ts_map_mem(&p, (SZ)PSX_HEAP_MAX, 0, TS_MEM_NONE) < E_OK ) {
			errno = ENOMEM;
			return (void *)-1;
		}
		heap_base = heap_brk = heap_made = (char *)p;
	}
	old = heap_brk;
	if ( incr < 0 ) {
		if ( (size_t)( -incr ) > (size_t)( heap_brk - heap_base ) ) {
			errno = EINVAL;
			return (void *)-1;
		}
		heap_brk += incr;
		return old;
	}
	if ( (size_t)incr > (size_t)( heap_base + PSX_HEAP_MAX - heap_brk ) ) {
		errno = ENOMEM;
		return (void *)-1;
	}
	if ( heap_brk + incr > heap_made ) {
		size_t	need = (size_t)( heap_brk + incr - heap_made );

		need = ( need + HEAP_STEP - 1 ) & ~(size_t)( HEAP_STEP - 1 );
		if ( ts_ctl_mem(heap_made, (SZ)need, TS_MEM_RW) < E_OK ) {
			errno = ENOMEM;
			return (void *)-1;
		}
		heap_made += need;
	}
	heap_brk += incr;
	return old;
}

void *sbrk( ptrdiff_t incr ) __attribute__((alias("_sbrk")));

int getpagesize( void )
{
	return 4096;
}

/* ---------------------------------------------------------------- mappings */

void *mmap( void *addr, size_t len, int prot, int flags, int fd, off_t off )
{
	void	*p = NULL;
	size_t	n = ( len + 4095 ) & ~(size_t)4095;
	UINT	pr = TS_MEM_RW;

	(void)addr;
	if ( len == 0 ) {
		errno = EINVAL;
		return MAP_FAILED;
	}
	if ( prot == PROT_NONE ) pr = TS_MEM_NONE;
	if ( ts_map_mem(&p, (SZ)n, 0, pr) < E_OK ) {
		errno = ENOMEM;
		return MAP_FAILED;
	}
	if ( ( flags & MAP_ANON ) == 0 ) {
		/* a file: its bytes read in; what is written there stays in memory */
		off_t	was = lseek(fd, 0, SEEK_CUR);
		size_t	got = 0;
		ssize_t	r;

		if ( was < 0 || lseek(fd, off, SEEK_SET) < 0 ) {
			(void)ts_unm_mem(p, (SZ)n);
			return MAP_FAILED;
		}
		while ( got < len && ( r = read(fd, (char *)p + got, len - got) ) > 0 ) {
			got += (size_t)r;
		}
		(void)lseek(fd, was, SEEK_SET);
		if ( ( prot & PROT_WRITE ) == 0 ) {
			(void)ts_ctl_mem(p, (SZ)n, TS_MEM_READ);
		}
	}
	return p;
}

int munmap( void *addr, size_t len )
{
	size_t	n = ( len + 4095 ) & ~(size_t)4095;

	return ( ts_unm_mem(addr, (SZ)n) < E_OK ) ? ( errno = EINVAL, -1 ) : 0;
}

int mprotect( void *addr, size_t len, int prot )
{
	size_t	n = ( len + 4095 ) & ~(size_t)4095;
	UINT	pr = ( prot == PROT_NONE ) ? TS_MEM_NONE : ( prot & PROT_WRITE ) ? TS_MEM_RW : TS_MEM_READ;

	return ( ts_ctl_mem(addr, (SZ)n, pr) < E_OK ) ? ( errno = EINVAL, -1 ) : 0;
}

int msync( void *addr, size_t len, int flags )		{ (void)addr; (void)len; (void)flags; return 0; }
int madvise( void *addr, size_t len, int advice )	{ (void)addr; (void)len; (void)advice; return 0; }
int mlock( const void *addr, size_t len )		{ (void)addr; (void)len; return 0; }
int munlock( const void *addr, size_t len )		{ (void)addr; (void)len; return 0; }
