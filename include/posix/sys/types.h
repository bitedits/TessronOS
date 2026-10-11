/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/types.h
 *	newlib's, with the BSD names it leaves out (design 5.8.3)
 */

#ifndef __POSIX_SYS_TYPES_H__
#define __POSIX_SYS_TYPES_H__

#include_next <sys/types.h>
#include <stdint.h>			/* the integers of fixed width and their limits, as BSD's */
#include <sys/endian.h>		/* the byte orders come with the types, as BSD's do */

#ifndef __POSIX_QUAD_T
#define __POSIX_QUAD_T
typedef long long		quad_t;
typedef unsigned long long	u_quad_t;
typedef quad_t			*qaddr_t;
#endif

/* The number of a device: its kind in bits 8-19, the one of its kind in the rest */
#ifndef major
typedef int			devmajor_t;
typedef int			devminor_t;
#define major(x)	((devmajor_t)( ( (unsigned int)(x) & 0x000fff00u ) >> 8 ))
#define minor(x)	((devminor_t)( ( ( (unsigned int)(x) & 0xfff00000u ) >> 12 ) \
				     | ( (unsigned int)(x) & 0x000000ffu ) ))
#define makedev(x, y)	((dev_t)( ( ( (dev_t)(x) << 8 ) & 0x000fff00u )		\
				| ( ( (dev_t)(y) << 12 ) & 0xfff00000u )	\
				| ( (dev_t)(y) & 0x000000ffu ) ))
#endif


#endif /* __POSIX_SYS_TYPES_H__ */
