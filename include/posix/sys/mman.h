/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	sys/mman.h
 *	Memory a program maps (design 5.8.3): anonymous memory, and a file
 *	read into memory (MAP_PRIVATE); writing a mapping back to its file
 *	is not done.
 */

#ifndef _SYS_MMAN_H_
#define _SYS_MMAN_H_

#include <sys/types.h>

#define PROT_NONE	0x00
#define PROT_READ	0x01
#define PROT_WRITE	0x02
#define PROT_EXEC	0x04

#define MAP_SHARED	0x0001
#define MAP_PRIVATE	0x0002
#define MAP_FIXED	0x0010
#define MAP_FILE	0x0000
#define MAP_ANON	0x1000
#define MAP_ANONYMOUS	MAP_ANON
#define MAP_FAILED	((void *)-1)

#define MS_ASYNC	0x01
#define MS_INVALIDATE	0x02
#define MS_SYNC		0x04

#define MADV_NORMAL	0
#define MADV_RANDOM	1
#define MADV_SEQUENTIAL	2
#define MADV_WILLNEED	3
#define MADV_DONTNEED	4

#ifdef __cplusplus
extern "C" {
#endif

void	*mmap( void *, size_t, int, int, int, off_t );
int	munmap( void *, size_t );
int	mprotect( void *, size_t, int );
int	msync( void *, size_t, int );
int	madvise( void *, size_t, int );
int	mlock( const void *, size_t );
int	munlock( const void *, size_t );

#ifdef __cplusplus
}
#endif

#endif /* _SYS_MMAN_H_ */
