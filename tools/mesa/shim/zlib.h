/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	zlib.h
 *	The inflating calls the V3D control list decoder makes to unpack the
 *	packet descriptions it carries. They are used only to print control
 *	lists for debugging; TessronOS builds Mesa without zlib, and
 *	lib/libgl/gl_os.c answers that the data cannot be unpacked, so the
 *	decoder has no descriptions and prints nothing.
 */

#ifndef TS_SHIM_ZLIB_H
#define TS_SHIM_ZLIB_H

#ifdef __cplusplus
extern "C" {
#endif

#define Z_OK		0
#define Z_STREAM_END	1
#define Z_NEED_DICT	2
#define Z_DATA_ERROR	(-3)
#define Z_MEM_ERROR	(-4)
#define Z_BUF_ERROR	(-5)
#define Z_SYNC_FLUSH	2
#define Z_FINISH	4

typedef unsigned char	Bytef;
typedef unsigned int	uInt;
typedef unsigned long	uLong;

typedef struct z_stream_s {
	const Bytef	*next_in;
	uInt		avail_in;
	uLong		total_in;
	Bytef		*next_out;
	uInt		avail_out;
	uLong		total_out;
	const char	*msg;
	void		*state;
	void		*zalloc;
	void		*zfree;
	void		*opaque;
	int		data_type;
	uLong		adler;
	uLong		reserved;
} z_stream;

int	inflateInit( z_stream *strm );
int	inflate( z_stream *strm, int flush );
int	inflateEnd( z_stream *strm );

#ifdef __cplusplus
}
#endif

#endif /* TS_SHIM_ZLIB_H */
