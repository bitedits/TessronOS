/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	btfs.h
 *	The BTRON file system read from a disk image, and the BTRON CD-ROM
 *	that carries one (design 11.9)
 *
 *	A BTRON CD-ROM is an ISO 9660 disc with one volume partition
 *	descriptor (type 3) whose system identifier is "BTRON SPECIFIED
 *	FILE SYSTEM": its extent, in blocks of 2048 bytes, is the image of a
 *	hard disk partition of the BTRON file system, which the rest of the
 *	disc shows as one file.
 *
 *	The file system is divided into logical blocks. Logical block 0 is
 *	the boot area, the system block starts at block 1 (its first 128
 *	bytes the system header, then the bitmaps of the blocks used and
 *	bad), the file ID table and the table of the files' short names
 *	follow it, and the files fill the rest, the root file (file ID 0)
 *	first. Each entry of the file ID table holds a file's reference
 *	count and the block of its header. A file is a header block -- the
 *	file header of 192 bytes, the fragment table, then the record
 *	indexes or the indirect indexes -- and the blocks its records are
 *	in. The records are indexed directly in the header block (index
 *	level 0), through index blocks (1), or through indirect blocks and
 *	then index blocks (2). A record index of 16 bytes is a record of
 *	data (its type, subtype, offset in its first block, size, and the
 *	first run of blocks), a link record (the target's file ID and five
 *	words of attributes), a connection index that carries four more runs
 *	of the record before it, or unused.
 *
 *	The standard form writes every number of more than a byte with the
 *	high byte first; the semi-standard form, low byte first, and the two
 *	are told apart by the byte order of the TRON disk ID (0x42FE, or
 *	0x52FE for the extended forms). Both are read. The extended form 2
 *	moves where the record indexes start (ridxofs) and widens the
 *	fragment table's entries; the standard and extended form 1 have them
 *	at byte 384.
 *
 *	Nothing here makes a system call: whoever reads the image gives a
 *	function that reads bytes from it, and memory comes from malloc.
 */

#ifndef __TS_BTFS_H__
#define __TS_BTFS_H__

#include <tk/typedef.h>
#include <tk/errno.h>
#include <ts/bpk.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------- the CD-ROM */

#define BTFS_CD_BLOCK		2048		/* a logical block of ISO 9660 */
#define BTFS_CD_VDSTART		16		/* the first volume descriptor */
#define BTFS_CD_VDMAX		64		/* descriptors looked at, at most */

/* The volume partition descriptor (type 3), byte offsets */
#define CDVPD_TYPE		0x00		/* 3 */
#define CDVPD_STDID		0x01		/* "CD001" */
#define CDVPD_VERSION		0x06		/* 1 */
#define CDVPD_SYSID		0x08		/* 32 bytes */
#define CDVPD_VOLID		0x28		/* 32 bytes */
#define CDVPD_VOLLOC		0x48		/* the first block: 32 bits low first, then high first */
#define CDVPD_VOLSZ		0x50		/* blocks: the same */
#define CDVPD_T_VPD		3
#define CDVPD_T_END		255
#define CDVPD_SYSID_BTRON	"BTRON SPECIFIED FILE SYSTEM"
#define CDVPD_VOLID_BOOT	"BOOTABLE VOLUME"

/* Reads len bytes at byte off of the disk or image; E_OK, or an error */
typedef ER (*BTFS_READ)( void *ctx, D off, void *buf, UINT len );

/*
 * The BTRON partition of a CD-ROM: where it starts and how long it is,
 * in bytes, and whether it says it boots. E_NOEXS when the disc is not
 * ISO 9660 or has no such descriptor.
 */
IMPORT ER btfs_cd_find( BTFS_READ rd, void *ctx, D *p_off, D *p_len, BOOL *p_boot );

/*
 * The files of the disc on its ISO 9660 side, read only. The names are
 * Joliet's when a supplementary volume descriptor of Joliet is there
 * (UCS-2), else the primary's; the version (";1") is left off. An
 * associated file is left out. Records do not cross a block of 2048
 * bytes.
 */
typedef struct {
	char	name[256];		/* UTF-8 */
	BOOL	dir;
	BOOL	hidden;			/* the existence bit */
	BOOL	joliet;			/* its names, and those under it, are Joliet's */
	UINT	extent;			/* its first block of 2048 bytes */
	UINT	size;			/* bytes */
	UB	date[7];		/* years from 1900, month, day, hour, minute, second,
					   offset from UTC in quarters of an hour */
} BTFS_ISOENT;

/* Called for each entry of a directory; below 0 stops the listing */
typedef INT (*BTFS_ISOPUT)( void *arg, const BTFS_ISOENT *e );

/* The root directory: Joliet's when there is one. E_NOEXS when the disc is not ISO 9660 */
IMPORT ER btfs_iso_root( BTFS_READ rd, void *ctx, BTFS_ISOENT *root );

/* The entries of a directory, but itself and the one above it */
IMPORT ER btfs_iso_list( BTFS_READ rd, void *ctx, const BTFS_ISOENT *dir, BTFS_ISOPUT put, void *arg );

/* The entry of a path of names from the root ('/' between; "" the root), A to Z in either case */
IMPORT ER btfs_iso_find( BTFS_READ rd, void *ctx, const char *path, BTFS_ISOENT *e );

/* ---------------------------------------------------------------- the file system */

#define BTFS_DISKID		0x42FE		/* the standard form */
#define BTFS_DISKID_EXT		0x52FE		/* the extended forms */
#define BTFS_FMT_STD		0x6400
#define BTFS_FMT_EXT1		0x6401
#define BTFS_FMT_EXT2		0x6402

/* The system header, byte offsets */
#define SH_DISKID		0
#define SH_FORMAT		2
#define SH_SSYS			4		/* the system block's logical blocks */
#define SH_SFIDT		6		/* the file ID table's */
#define SH_SFNMT		8		/* the short name table's */
#define SH_NBMP			10		/* the bitmap: bytes, or words of 4 in the extended forms */
#define SH_BSIZE		24		/* bytes of a logical block */
#define SH_NFMAX		26		/* files at most */
#define SH_LANG			28
#define SH_LEVEL		30
#define SH_NLB			32		/* logical blocks in all */
#define SH_NFREE		36
#define SH_MTIME		40
#define SH_CTIME		44
#define SH_NAME			48		/* 40 bytes: the file system's name */
#define SH_LOCAT		88		/* 40 bytes: the device's place */
#define BTFS_SYSHEAD		128

/* The file header, byte offsets */
#define FH_STARTID		0		/* 0x54726F6E */
#define FH_FTYPE		4		/* TTTT xxxx BAPO xRWE */
#define FH_ATYPE		6
#define FH_OWN			8		/* 28 bytes */
#define FH_GRP			36
#define FH_GACCLV		64
#define FH_PACCLV		66
#define FH_NLNK			68
#define FH_IDXLV		70
#define FH_SIZE			72
#define FH_NBLK			76
#define FH_NREC			80
#define FH_LTIME		92
#define FH_ATIME		96
#define FH_MTIME		100
#define FH_CTIME		104
#define FH_NAME			108		/* 40 bytes */
#define FH_PSWD			148
#define FH_REFCNT		172		/* the extended forms */
#define FH_RIDXOFS		176		/* the extended form 2 */
#define FH_FID			186
#define FH_ENDID		188		/* 0x82DDE96B */
#define BTFS_FILEHEAD		192
#define BTFS_STARTID		0x54726F6EU
#define BTFS_ENDID		0x82DDE96BU
#define BTFS_RIDXOFS		384		/* where the record indexes start but in the extended form 2 */

/* A link file's location data, after its header */
#define FL_FID			0
#define FL_CTIME		2
#define FL_FSNAME		10		/* 40 bytes */
#define FL_LOCAT		50		/* 40 bytes */

#define BTFS_FT_LINK		0		/* the file type (ftype >> 12): a link file */
#define BTFS_FT_FILE		1		/* an ordinary file */

#define BTFS_RECIDX		16		/* bytes of a record index */
#define BTFS_INDIR		8		/* of an indirect index */
#define BTFS_NAMELEN		20		/* TRON characters of a name */

typedef struct {
	BTFS_READ	rd;
	void		*ctx;
	D		part;		/* where the file system's image starts, bytes */
	D		plen;		/* how long it is, 0 not known */
	D		base;		/* where logical block 0 is */
	UINT		bsize;		/* bytes of a logical block */
	BOOL		le;		/* the semi-standard form: the low byte first */
	UINT		format;		/* BTFS_FMT_* */
	UINT		ssys, sfidt, sfnmt;
	UINT		nfmax;		/* files at most */
	UINT		nlb, nfree;
	UINT		mtime, ctime;
	UH		name[BTFS_NAMELEN];
	char		uname[128];	/* the name, UTF-8 */
	char		ulocat[128];	/* the device's place, UTF-8 */
	BOOL		fidhigh;	/* a file ID entry is a word with the count in its top byte */
	UB		*fidt;		/* the file ID table, nfmax entries of 4 bytes */
	UB		*blk;		/* a block's room */
} BTFS;

/*
 * The file system in the image from byte off (len bytes, 0 not known):
 * its system header looked for at each 512 bytes of the first 64 KB,
 * since the boot area before logical block 1 may be longer than one
 * logical block. E_OK, or E_NOEXS when there is none there.
 */
IMPORT ER   btfs_open( BTFS *fs, BTFS_READ rd, void *ctx, D off, D len );
IMPORT void btfs_close( BTFS *fs );

/* A record of a file as its index says */
typedef struct {
	UH		type;		/* 0 a link record, 1..31 */
	UH		sub;
	UINT		size;		/* bytes; 0 for a link record */
	UINT		off;		/* where it starts in its first block */
	INT		nrun;
	UINT		*blk;		/* the runs: their first blocks */
	UH		*cnt;		/* and their lengths */
	INT		lfid;		/* a link record: its target's file ID, -1 none */
	UH		latr[5];	/* its attribute data */
} BTFSREC;

typedef struct {
	UINT		fid;
	UINT		ftype, atype;	/* the file header's, as they are */
	INT		idxlv;
	UINT		size, nblk;
	UINT		refcnt;		/* from the file ID table, or the header past 254 */
	UINT		ltime, atime, mtime, ctime;
	UH		name[BTFS_NAMELEN];
	char		uname[128];	/* UTF-8 */
	BOOL		islink;		/* a link file: */
	INT		target;		/* its target's file ID here, -1 elsewhere */
	char		tfsname[128];	/* the file system the target is in */
	INT		nrec;		/* records, link records among them */
	BTFSREC		*rec;
	INT		nlink;
} BTFSFILE;

/*
 * A file's header and its records' indexes read; E_NOEXS for a file ID
 * no file has, E_OBJ for a header or index that is not one.
 */
IMPORT ER   btfs_file( BTFS *fs, UINT fid, BTFSFILE *f );
IMPORT void btfs_file_free( BTFSFILE *f );

/* The file a link record names, through a link file to this same file system; -1 none */
IMPORT INT  btfs_link_target( BTFS *fs, const BTFSREC *r );

/* A record's bytes into buf (r->size of them) */
IMPORT ER   btfs_rec_read( BTFS *fs, const BTFSREC *r, UB *buf );

/* Whether a file has a file ID: the table's entry is in use */
IMPORT BOOL btfs_fid_used( BTFS *fs, UINT fid );

/*
 * A file and every file it links to, in turn, made one BTRON archive
 * (bpk_write, stored): the file first, as the root, then the others as
 * they were reached. A link record goes to the archive's form, naming
 * its target by its place in the archive (0xFFFF when it is not
 * there: a link file to another file system, or an ID with no file);
 * every other record goes as it is. *p_out is malloc'd; the count of
 * files in *p_nfiles. E_OK, or an error and what was wrong (in
 * Japanese) in err.
 */
IMPORT ER   btfs_book( BTFS *fs, UINT fid, UB **p_out, UINT *p_len, INT *p_nfiles, char *err, INT errmax );

#ifdef __cplusplus
}
#endif
#endif /* __TS_BTFS_H__ */
