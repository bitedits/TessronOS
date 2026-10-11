/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	arc_arg.h
 *	書庫管理: the start-up argument that asks for work without the
 *	window (design 11.20)
 *
 *	The program is started with nothing (it says what it is for and
 *	ends), with the UUID of an archive object (the window opens on it),
 *	or with this: done at once, said on the console in lines that begin
 *	"archive: ", ending with 0 when it went well.
 *
 *   ARC_OP_PACK	the objects reachable from obj packed into the
 *			empty archive object archive (method: BPK_LH5 or
 *			BPK_LH0); 4 when it has a book already
 *   ARC_OP_UNPACK	the book of archive taken out onto the volume of
 *			obj, and a link to its root put in obj's figure;
 *			7 when its CRC is wrong
 *   ARC_OP_LIST	the names of the book's objects
 *   ARC_OP_INFO	the figures of 書籍情報
 *   ARC_OP_DELETE	the book taken out of archive: it is empty again
 *   ARC_OP_COPY	the book of the archive object obj (a file taken
 *			in, say) put into the empty archive; 4 when it is
 *			not empty
 * A book that cannot be read is 6, an archive that is empty 2.
 */

#ifndef __ARC_ARG_H__
#define __ARC_ARG_H__

#define ARC_PROG_ID	"archive-manager"
#define ARC_ARG_MAGIC	0x4B435241	/* "ARCK" */
#define ARC_OP_PACK	1
#define ARC_OP_UNPACK	2
#define ARC_OP_LIST	3
#define ARC_OP_INFO	4
#define ARC_OP_DELETE	5
#define ARC_OP_COPY	6

typedef struct {
	UINT	magic;
	UINT	op;			/* ARC_OP_* */
	TS_UUID	archive;
	TS_UUID	obj;
	UINT	method;			/* ARC_OP_PACK: BPK_LH5 or BPK_LH0 */
} T_ARCARG;

#endif /* __ARC_ARG_H__ */
