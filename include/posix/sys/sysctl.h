/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/sysctl.h
 *	The few names of the system a command reads with sysctl (design
 *	5.8.3): the machine, its processors, the system's name. lib/libposix
 *	answers them; any other is ENOENT.
 */

#ifndef _SYS_SYSCTL_H_
#define _SYS_SYSCTL_H_

#include <sys/types.h>

#define CTL_KERN		1
#define CTL_HW			6
#define CTL_USER		8

#define USER_CS_PATH		1		/* the PATH that finds the standard commands */

#define KERN_OSTYPE		1
#define KERN_OSRELEASE		2
#define KERN_ARGMAX		8
#define KERN_HOSTNAME		10

#define HW_MACHINE		1
#define HW_MODEL		2
#define HW_NCPU			3
#define HW_PAGESIZE		7
#define HW_MACHINE_ARCH		10

#ifdef __cplusplus
extern "C" {
#endif

int	sysctl( const int *, unsigned int, void *, size_t *, const void *, size_t );
int	sysctlbyname( const char *, void *, size_t *, const void *, size_t );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SYSCTL_H_ */
