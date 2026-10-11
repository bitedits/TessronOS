/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/cdefs.h
 *	newlib's, and the words a BSD program's sources are written with
 *	(design 5.8.3): the marks of the file's version, of a function that
 *	does not return or formats like printf, and the like.
 */

#ifndef __POSIX_SYS_CDEFS_H__
#define __POSIX_SYS_CDEFS_H__

#include_next <sys/cdefs.h>

#ifndef __RCSID
#define __RCSID(s)		struct __rcsid_unused
#endif
#ifndef __COPYRIGHT
#define __COPYRIGHT(s)		struct __copyright_unused
#endif
#ifndef __SCCSID
#define __SCCSID(s)		struct __sccsid_unused
#endif
#ifndef __KERNEL_RCSID
#define __KERNEL_RCSID(n, s)	struct __rcsid_unused
#endif
#ifndef __IDSTRING
#define __IDSTRING(n, s)	struct __idstring_unused
#endif

#ifndef __dead
#define __dead			__attribute__((__noreturn__))
#endif
#ifndef __printflike
#define __printflike(f, a)	__attribute__((__format__(__printf__, f, a)))
#endif
#ifndef __scanflike
#define __scanflike(f, a)	__attribute__((__format__(__scanf__, f, a)))
#endif
#ifndef __format_arg
#define __format_arg(f)		__attribute__((__format_arg__(f)))
#endif
#ifndef __sysloglike
#define __sysloglike(f, a)
#endif
#ifndef __UNCONST
#define __UNCONST(a)		((void *)(unsigned long)(const void *)(a))
#endif
#ifndef __UNVOLATILE
#define __UNVOLATILE(a)		((void *)(unsigned long)(volatile void *)(a))
#endif
#ifndef __arraycount
#define __arraycount(a)		( sizeof(a) / sizeof((a)[0]) )
#endif
#ifndef __USE
#define __USE(a)		((void)(a))
#endif
#ifndef __constfunc
#define __constfunc		__attribute__((__const__))
#endif
#ifndef __pure
#define __pure			__attribute__((__pure__))
#endif
#ifndef __used
#define __used			__attribute__((__used__))
#endif
#ifndef __diagused
#define __diagused		__attribute__((__unused__))
#endif
#ifndef __debugused
#define __debugused		__attribute__((__unused__))
#endif
#ifndef __nothing
#define __nothing		((void)0)
#endif
#ifndef __BIT
#define __BIT(n)		( (unsigned long long)1 << (n) )
#endif
#ifndef __returns_twice
#define __returns_twice		__attribute__((__returns_twice__))
#endif
#ifndef __noinline
#define __noinline		__attribute__((__noinline__))
#endif
#ifndef __always_inline
#define __always_inline		__inline __attribute__((__always_inline__))
#endif
#ifndef __warn_references
#define __warn_references(sym, msg)
#endif
#ifndef __type_fit
#define __type_fit(t, a)	1
#endif
#ifndef __NetBSD_Prereq__
#define __NetBSD_Prereq__(a, b, c)	0
#endif
#ifndef __GNUC_PREREQ__
#define __GNUC_PREREQ__(ma, mi)	( __GNUC__ > (ma) || ( __GNUC__ == (ma) && __GNUC_MINOR__ >= (mi) ) )
#endif
#ifndef __aligned
#define __aligned(x)		__attribute__((__aligned__(x)))
#endif
#ifndef __packed
#define __packed		__attribute__((__packed__))
#endif
#ifndef __section
#define __section(x)		__attribute__((__section__(x)))
#endif
#ifndef __unreachable
#define __unreachable()		__builtin_unreachable()
#endif
#ifndef __fallthrough
#define __fallthrough		__attribute__((__fallthrough__))
#endif
#ifndef __nonnull
#define __nonnull(x)
#endif
#ifndef _DIAGASSERT
#define _DIAGASSERT(e)		((void)0)
#endif
#ifndef CTASSERT
#define CTASSERT(x)		_Static_assert(x, #x)
#endif
#ifndef __RENAME
#define __RENAME(x)
#endif
#ifndef __c99inline
#define __c99inline		inline
#endif

#endif /* __POSIX_SYS_CDEFS_H__ */
