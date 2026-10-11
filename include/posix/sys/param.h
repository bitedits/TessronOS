/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/param.h
 *	newlib's, with the sizes and macros a BSD program counts on
 *	(design 5.8.3)
 */

#ifndef __POSIX_SYS_PARAM_H__
#define __POSIX_SYS_PARAM_H__

#include_next <sys/param.h>
#include <sys/types.h>
#include <inttypes.h>
#include <limits.h>

/* the calls of 4.4BSD are there (wait, the signals, the terminals), as programs ask with BSD */
#ifndef BSD
#define BSD		199506
#define BSD4_3		1
#define BSD4_4		1
#endif

#undef NOFILE
#define NOFILE		256
#ifndef MAXNAMLEN
#define MAXNAMLEN	767
#endif
#ifndef MAXBSIZE
#define MAXBSIZE	65536
#endif
#ifndef DEV_BSIZE
#define DEV_BSIZE	512
#define DEV_BSHIFT	9
#endif
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN	256
#endif
#ifndef MAXLOGNAME
#define MAXLOGNAME	16
#endif
#ifndef MAXCOMLEN
#define MAXCOMLEN	16
#endif
#ifndef MAXSYMLINKS
#define MAXSYMLINKS	32
#endif
#ifndef MAXINTERP
#define MAXINTERP	PATH_MAX
#endif
#ifndef NGROUPS
#define NGROUPS		16
#endif
#ifndef MAXPHYS
#define MAXPHYS		( 64 * 1024 )
#endif
#ifndef PAGE_SIZE
#define PAGE_SIZE	4096
#endif
#ifndef roundup
#define roundup(x, y)	( ( ( (x) + ( (y) - 1 ) ) / (y) ) * (y) )
#endif
#ifndef rounddown
#define rounddown(x, y)	( ( (x) / (y) ) * (y) )
#endif
#ifndef roundup2
#define roundup2(x, m)	( ( (x) + (m) - 1 ) & ~( (m) - 1 ) )
#endif
#ifndef powerof2
#define powerof2(x)	( ( ( (x) - 1 ) & (x) ) == 0 )
#endif
#ifndef setbit
#define setbit(a, i)	( ((unsigned char *)(a))[(i) / NBBY] |= 1 << ( (i) % NBBY ) )
#define clrbit(a, i)	( ((unsigned char *)(a))[(i) / NBBY] &= ~( 1 << ( (i) % NBBY ) ) )
#define isset(a, i)	( ((const unsigned char *)(a))[(i) / NBBY] & ( 1 << ( (i) % NBBY ) ) )
#define isclr(a, i)	( ( ((const unsigned char *)(a))[(i) / NBBY] & ( 1 << ( (i) % NBBY ) ) ) == 0 )
#endif
#ifndef ctob
#define ctob(x)		( (x) * PAGE_SIZE )
#endif
#ifndef dbtob
#define dbtob(x)	( (x) << DEV_BSHIFT )
#define btodb(x)	( (x) >> DEV_BSHIFT )
#endif
#ifndef __NetBSD_Version__
#define __NetBSD_Version__	1100000000
#endif

#endif /* __POSIX_SYS_PARAM_H__ */
