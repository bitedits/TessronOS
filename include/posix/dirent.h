/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	dirent.h
 *	Directories of the file layer, read as POSIX's (design 5.8.3)
 */

#ifndef _DIRENT_H_
#define _DIRENT_H_
#define _SYS_DIRENT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MAXNAMLEN	767

#define DT_UNKNOWN	0
#define DT_FIFO		1
#define DT_CHR		2
#define DT_DIR		4
#define DT_BLK		6
#define DT_REG		8
#define DT_LNK		10
#define DT_SOCK		12
#define DT_WHT		14

#define IFTODT(m)	( ( (m) & 0170000 ) >> 12 )
#define DTTOIF(d)	( (d) << 12 )

struct dirent {
	ino_t		d_fileno;
	unsigned short	d_reclen;
	unsigned short	d_namlen;
	unsigned char	d_type;
	char		d_name[MAXNAMLEN + 1];
};
#define d_ino		d_fileno

typedef struct __psx_dir DIR;

DIR		*opendir( const char * );
DIR		*fdopendir( int );
struct dirent	*readdir( DIR * );
int		readdir_r( DIR *, struct dirent *, struct dirent ** );
int		closedir( DIR * );
void		rewinddir( DIR * );
int		dirfd( DIR * );
long		telldir( DIR * );
void		seekdir( DIR *, long );
int		scandir( const char *, struct dirent ***, int (*)( const struct dirent * ),
			 int (*)( const struct dirent **, const struct dirent ** ) );
int		alphasort( const struct dirent **, const struct dirent ** );

#ifdef __cplusplus
}
#endif

#endif /* _DIRENT_H_ */
