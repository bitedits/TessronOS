/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/ioctl.h
 *	ioctl, for terminals and pipes (design 5.8.4)
 */

#ifndef _SYS_IOCTL_H_
#define _SYS_IOCTL_H_

#include <sys/ttycom.h>

#ifdef __cplusplus
extern "C" {
#endif

int	ioctl( int, unsigned long, ... );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_IOCTL_H_ */
