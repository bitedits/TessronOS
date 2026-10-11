/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	machine/_types.h
 *	The types of include/posix (design 5.8.3): newlib's, with the
 *	numbers of a file and of its owner as wide as the system gives them.
 *
 *	newlib makes ino_t, dev_t, uid_t and gid_t 16 bits wide; an object's
 *	file number is 64 bits (objfs) and a user's up to 62000. The C
 *	library's own code never sees struct stat as the commands have it
 *	(lib/libposix hands newlib its own form), so the commands may count
 *	them wider.
 */

#ifndef __POSIX_MACHINE_TYPES_H__
#define __POSIX_MACHINE_TYPES_H__

#include_next <machine/_types.h>

#define __machine_ino_t_defined
typedef __uint64_t	__ino_t;
#define __machine_dev_t_defined
typedef __uint32_t	__dev_t;
#define __machine_uid_t_defined
typedef __uint32_t	__uid_t;
#define __machine_gid_t_defined
typedef __uint32_t	__gid_t;

#endif /* __POSIX_MACHINE_TYPES_H__ */
