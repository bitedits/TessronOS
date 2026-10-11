/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	part.c
 *	Partition tables: GPT with an MBR fallback (design 10.6).
 */

#include <sys/machine.h>
#include "kernel.h"
#include <ts/blk.h>

/* 54465331-0000-4000-8000-5446532d5631 in GPT mixed-endian form */
EXPORT CONST UB knl_tsfs_type_guid[16] = {
	0x31, 0x53, 0x46, 0x54, 0x00, 0x00, 0x00, 0x40,
	0x80, 0x00, 0x54, 0x46, 0x53, 0x2d, 0x56, 0x31
};

/* EBD0A0A2-B9E5-4433-87C0-68B6B72699C7 (basic data, used for FAT) */
EXPORT CONST UB knl_basic_data_guid[16] = {
	0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
	0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7
};

#define GPT_SIGNATURE		0x5452415020494645ULL	/* "EFI PART" */
#define MBR_SIGNATURE		0xAA55
#define MBR_TYPE_PROTECTIVE	0xEE
#define MBR_TYPE_EXTENDED	0x05
#define MBR_TYPE_EXTENDED_LBA	0x0F
#define MBR_TYPE_EXTENDED_LNX	0x85
#define MBR_EBR_MAX		( BLK_MAX_PART + 1 )	/* links of an extended partition followed */
#define GPT_MAX_ENT		4096	/* entries of a GPT looked at: the table as it is made, whatever its size */

LOCAL UH le16( CONST UB *p )
{
	return (UH)((UW)p[0] | ((UW)p[1] << 8));
}

LOCAL UW le32( CONST UB *p )
{
	return (UW)p[0] | ((UW)p[1] << 8) | ((UW)p[2] << 16) | ((UW)p[3] << 24);
}

LOCAL UD le64( CONST UB *p )
{
	return (UD)le32(p) | ((UD)le32(p + 4) << 32);
}

LOCAL BOOL guid_is_zero( CONST UB *g )
{
	INT	i;

	for ( i = 0; i < 16; i++ ) {
		if ( g[i] != 0 ) return FALSE;
	}
	return TRUE;
}

LOCAL void guid_copy( UB *dst, CONST UB *src )
{
	INT	i;

	for ( i = 0; i < 16; i++ ) {
		dst[i] = src[i];
	}
}

/*
 * GPT: header at sector 1, entry array at the sector it names.
 *	The CRCs are not checked; a wrong signature or an out of range
 *	entry array makes the scanner fall back to the MBR.
 */
LOCAL INT read_gpt( BLK_READFN readfn, void *exinf, UD nsect_total, T_PARTTBL *tbl, UB *sect )
{
	UD	entry_lba, first, last;
	UW	nent, entsz, i, per_sect, s;
	INT	n = 0;

	if ( readfn(exinf, 1, 1, sect) != E_OK ) {
		return E_IO;
	}
	if ( le64(sect) != GPT_SIGNATURE ) {
		return E_NOEXS;
	}

	entry_lba = le64(sect + 72);
	nent      = le32(sect + 80);
	entsz     = le32(sect + 84);
	if ( entsz < 128 || entsz > BLK_SECTOR_SIZE || nent == 0 || nent > GPT_MAX_ENT
	  || entry_lba == 0 || entry_lba >= nsect_total ) {
		return E_NOEXS;
	}

	per_sect = BLK_SECTOR_SIZE / entsz;
	for ( i = 0; i < nent && n < BLK_MAX_PART; i += per_sect ) {
		if ( readfn(exinf, entry_lba + (i / per_sect), 1, sect) != E_OK ) {
			return E_IO;
		}
		for ( s = 0; s < per_sect && i + s < nent && n < BLK_MAX_PART; s++ ) {
			CONST UB *e = sect + s * entsz;

			if ( guid_is_zero(e) ) {
				continue;		/* unused entry */
			}
			first = le64(e + 32);
			last  = le64(e + 40);
			if ( last < first || last >= nsect_total ) {
				continue;		/* outside the device */
			}
			tbl->part[n].start    = first;
			tbl->part[n].nsect    = last - first + 1;
			tbl->part[n].mbr_type = 0;
			tbl->part[n].valid    = TRUE;
			guid_copy(tbl->part[n].type_guid, e);
			guid_copy(tbl->part[n].part_guid, e + 16);
			n++;
		}
	}
	tbl->gpt = TRUE;
	tbl->n = n;

	return n;
}

/*
 * MBR: four entries at offset 446, and the logical partitions of an
 * extended one after them.
 *
 * A medium formatted without a partition table (a "superfloppy": many
 * USB sticks and small cards) has the boot sector of its file system at
 * sector 0, which ends in the same signature. Its code and messages may
 * reach into where the entries would be. An entry whose first byte is
 * neither 0x00 nor 0x80 cannot be one, and then the sector is not taken
 * for an MBR: the whole device is the one volume.
 */
LOCAL BOOL mbr_extended( UW type )
{
	return (BOOL)( type == MBR_TYPE_EXTENDED || type == MBR_TYPE_EXTENDED_LBA
		       || type == MBR_TYPE_EXTENDED_LNX );
}

LOCAL INT mbr_add( T_PARTTBL *tbl, INT n, UD start, UD nsect, UW type, UD nsect_total )
{
	if ( n >= BLK_MAX_PART || nsect == 0 || start >= nsect_total || start + nsect > nsect_total ) {
		return n;
	}
	tbl->part[n].start    = start;
	tbl->part[n].nsect    = nsect;
	tbl->part[n].mbr_type = type;
	tbl->part[n].valid    = TRUE;
	return n + 1;
}

/*
 * The chain of an extended partition: each link (an EBR, laid out as
 * an MBR) holds one logical partition, placed from the link itself,
 * and where the next link is, placed from the start of the extended
 * partition. A chain that turns back on itself or runs on is cut.
 */
LOCAL INT read_ebr( BLK_READFN readfn, void *exinf, UD nsect_total, T_PARTTBL *tbl, UB *sect,
		    UD base, INT n )
{
	UD	at = base;
	INT	hops;

	for ( hops = 0; hops < MBR_EBR_MAX && n < BLK_MAX_PART; hops++ ) {
		CONST UB *e0 = sect + 446, *e1 = sect + 446 + 16;
		UD	next;

		if ( at >= nsect_total || readfn(exinf, at, 1, sect) != E_OK
		  || le16(sect + 510) != MBR_SIGNATURE ) {
			break;
		}
		if ( e0[4] != 0 && !mbr_extended(e0[4]) ) {
			n = mbr_add(tbl, n, at + le32(e0 + 8), le32(e0 + 12), e0[4], nsect_total);
		}
		if ( !mbr_extended(e1[4]) || le32(e1 + 8) == 0 ) {
			break;
		}
		next = base + le32(e1 + 8);
		if ( next <= at ) {
			break;			/* a link may only point onwards */
		}
		at = next;
	}
	return n;
}

LOCAL INT read_mbr( BLK_READFN readfn, void *exinf, UD nsect_total, T_PARTTBL *tbl, UB *sect )
{
	UD	ext[4];
	INT	i, n = 0, next = 0;

	if ( readfn(exinf, 0, 1, sect) != E_OK ) {
		return E_IO;
	}
	if ( le16(sect + 510) != MBR_SIGNATURE ) {
		return E_NOEXS;
	}
	for ( i = 0; i < 4; i++ ) {
		UB	status = sect[446 + i * 16];

		if ( status != 0x00 && status != 0x80 ) {
			return E_NOEXS;
		}
	}

	for ( i = 0; i < 4; i++ ) {
		CONST UB *e = sect + 446 + i * 16;
		UW	type = e[4];
		UD	start = le32(e + 8), nsect = le32(e + 12);

		if ( type == 0 || nsect == 0 || type == MBR_TYPE_PROTECTIVE ) {
			continue;
		}
		if ( mbr_extended(type) ) {
			if ( start != 0 && start < nsect_total ) ext[next++] = start;
			continue;
		}
		n = mbr_add(tbl, n, start, nsect, type, nsect_total);
	}
	/* the logical partitions after the primary ones; the sector is read over */
	for ( i = 0; i < next; i++ ) {
		n = read_ebr(readfn, exinf, nsect_total, tbl, sect, ext[i], n);
	}
	tbl->gpt = FALSE;
	tbl->n = n;

	return n;
}

EXPORT INT knl_read_parttbl( BLK_READFN readfn, void *exinf, UD nsect_total, T_PARTTBL *tbl )
{
	UB	*sect;
	INT	n;

	knl_memset(tbl, 0, sizeof(*tbl));
	if ( nsect_total < 2 ) {
		return 0;
	}

	sect = (UB *)Kmalloc(BLK_SECTOR_SIZE);
	if ( sect == NULL ) {
		return E_NOMEM;
	}

	n = read_gpt(readfn, exinf, nsect_total, tbl, sect);
	if ( n < 0 ) {
		n = read_mbr(readfn, exinf, nsect_total, tbl, sect);
	}
	if ( n < 0 ) {
		knl_memset(tbl, 0, sizeof(*tbl));
		n = 0;			/* no partition table: the whole device */
	}

	Kfree(sect);

	return n;
}
