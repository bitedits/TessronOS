/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	grp.h
 *	newlib's, with BSD's names of groups kept by their numbers (design
 *	5.8.3)
 */

#ifndef __POSIX_GRP_H__
#define __POSIX_GRP_H__

#include_next <grp.h>

#ifdef __cplusplus
extern "C" {
#endif

const char	*group_from_gid( gid_t, int );
int		gid_from_group( const char *, gid_t * );

#ifdef __cplusplus
}
#endif

#endif /* __POSIX_GRP_H__ */
