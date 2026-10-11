/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/extattr.h
 *	Extended attributes (design 5.8.3): an object's attributes are its
 *	metadata, not given out this way. There are none to copy.
 */

#ifndef _SYS_EXTATTR_H_
#define _SYS_EXTATTR_H_

#include <sys/types.h>

#define EXTATTR_NAMESPACE_EMPTY		0
#define EXTATTR_NAMESPACE_USER		1
#define EXTATTR_NAMESPACE_SYSTEM	2

#ifdef __cplusplus
extern "C" {
#endif

int	fcpxattr( int, int );		/* no file has extended attributes: nothing to copy */

#ifdef __cplusplus
}
#endif

#endif /* _SYS_EXTATTR_H_ */
