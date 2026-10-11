/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	psxid.h
 *	The numbers a POSIX program sees for users and groups (design 5.8.1)
 *
 *	A user or a group is an object named by its UUID (design 18.10).
 *	POSIX wants a small number: the fixed ones have numbers of their
 *	own, any other is folded into PSX_ID_HASHED and up from its UUID, so
 *	that the kernel (stat) and the C library (getuid, getpwuid) give the
 *	same number for it without asking each other.
 */

#ifndef __TS_PSXID_H__
#define __TS_PSXID_H__

#include <ts/uuid.h>

#define PSX_UID_SYSTEM		0	/* the system, init: root */
#define PSX_UID_ADMIN		1	/* 管理者 */
#define PSX_UID_PERSON		1000	/* 利用者, the ordinary user */
#define PSX_GID_ADMIN		0	/* the administrators: wheel */
#define PSX_GID_USERS		100	/* 利用者 the group */
#define PSX_ID_HASHED		2000
#define PSX_ID_SPAN		60000

/* The fixed objects' UUIDs (include/ts/ob.h), as bytes */
#define PSX_U_SYSTEM	{ 0x01, 0xa0, 0xd6, 0x80, 0x3a, 0xa0, 0x75, 0xb3, 0xbd, 0x7d, 0x07, 0x5e, 0x14, 0x21, 0x7c, 0xd2 }
#define PSX_G_ADMIN	{ 0x01, 0xa0, 0xd6, 0x80, 0x3a, 0xa1, 0x79, 0x22, 0xae, 0x79, 0xba, 0x5c, 0xbb, 0xaa, 0x20, 0x37 }
#define PSX_U_PERSON	{ 0x01, 0xa0, 0xd6, 0x80, 0x3a, 0xaa, 0x7a, 0x61, 0x8d, 0x2c, 0x5e, 0x91, 0x07, 0xb3, 0x4f, 0x6a }
#define PSX_G_USERS	{ 0x01, 0xa0, 0xd6, 0x80, 0x3a, 0xab, 0x7c, 0x05, 0x9e, 0x47, 0x13, 0xd8, 0x6a, 0x2f, 0xc1, 0x58 }
#define PSX_U_ADMIN	{ 0x01, 0xa0, 0xd6, 0x80, 0x3a, 0xac, 0x75, 0x3e, 0xa2, 0x91, 0x6f, 0x0b, 0xd4, 0x58, 0x2e, 0x97 }

static __inline__ int psx_uuid_is( const TS_UUID *u, const unsigned char *b )
{
	int	i;

	for ( i = 0; i < 16; i++ ) {
		if ( u->b[i] != b[i] ) return 0;
	}
	return 1;
}

/* The number of a user or a group object; a UUID of zeroes (no owner) is the system's */
static __inline__ unsigned int psx_id_of( const TS_UUID *u )
{
	static const unsigned char sys[16] = PSX_U_SYSTEM, adm[16] = PSX_G_ADMIN;
	static const unsigned char per[16] = PSX_U_PERSON, usr[16] = PSX_G_USERS;
	static const unsigned char uadm[16] = PSX_U_ADMIN;
	unsigned int	h = 2166136261u;
	int		i, zero = 1;

	for ( i = 0; i < 16; i++ ) {
		if ( u->b[i] != 0 ) zero = 0;
	}
	if ( zero || psx_uuid_is(u, sys) ) return PSX_UID_SYSTEM;
	if ( psx_uuid_is(u, adm) ) return PSX_GID_ADMIN;
	if ( psx_uuid_is(u, per) ) return PSX_UID_PERSON;
	if ( psx_uuid_is(u, usr) ) return PSX_GID_USERS;
	if ( psx_uuid_is(u, uadm) ) return PSX_UID_ADMIN;
	for ( i = 0; i < 16; i++ ) {
		h = ( h ^ u->b[i] ) * 16777619u;
	}
	return PSX_ID_HASHED + h % PSX_ID_SPAN;
}

#endif /* __TS_PSXID_H__ */
