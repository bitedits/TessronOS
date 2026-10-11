/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/utsname.h
 *	The name of the system (design 5.8.3)
 */

#ifndef _SYS_UTSNAME_H_
#define _SYS_UTSNAME_H_

#define _SYS_NMLN	256
#define SYS_NMLN	_SYS_NMLN

struct utsname {
	char	sysname[_SYS_NMLN];
	char	nodename[_SYS_NMLN];
	char	release[_SYS_NMLN];
	char	version[_SYS_NMLN];
	char	machine[_SYS_NMLN];
};

#ifdef __cplusplus
extern "C" {
#endif

int	uname( struct utsname * );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_UTSNAME_H_ */
